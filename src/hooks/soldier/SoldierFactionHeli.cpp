#include <pch.h>
#include "SoldierFactionHeli.h"

#include <Windows.h>
#include <array>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <mutex>

#include "log.h"
#include "SoldierFaction.h"
#include "../heli/CpControllerImpl_GetTargetPosition.h"
#include "../heli/HeliInterest_SetLookAt.h"
#include "../heli/HeliKey.h"
#include "../heli/HeliLineOfSight.h"

namespace
{
    using GetPlayerPosition_t = float*(__fastcall*)(void* playerInfo, float* out);

    constexpr std::uint32_t kMaxBrains    = 16u;   // as many as SoldierFaction keeps faction helis
    constexpr std::uint32_t kSlotCount    = 12u;   // the support heli's target list holds 12
    constexpr std::uint16_t kNoTarget     = 0xFFFFu;
    constexpr std::uint16_t kPlayerId     = 0x0000u;
    constexpr std::uint32_t kSoldierCount = 0x200u;

    constexpr std::uint64_t kRefreshMs = 500u;
    constexpr std::uint64_t kLineMs    = 150u;
    constexpr std::uint64_t kHoldMs    = 10000u;   // the support heli keeps a target 10 s before it picks again
    constexpr std::uint64_t kFreshMs   = 1000u;
    constexpr std::uint64_t kAlertMs   = 5000u;    // after a hit it looks around through its wide sight
    constexpr float         kMaxStep   = 0.5f;
    constexpr float         kThreatTier = 1.5f;    // the support heli's first pick: a threat scoring this much
    constexpr float         kPlayerGateSq = 20.0f * 20.0f;   // the attack check only shoots this close to the player
    constexpr std::uint32_t kWideSight  = 0u;

    constexpr float kSeenFade    = 0.2f;    // per second, the support heli's own rates
    constexpr float kHitFade     = 0.2f;
    constexpr float kThreatFade  = 0.05f;
    constexpr float kRiderThreat = 0.2f;
    constexpr float kHeavyThreat = 0.1f;
    constexpr float kScoreRange  = 200.0f;
    constexpr float kScoreSlope  = 0.005f;
    constexpr float kListRange   = 300.0f;

    constexpr float kChest       = 1.0f;
    constexpr float kSoldierStop = 0.5f;   // rays stop short of what they look at
    constexpr float kHeliStop    = 4.0f;
    constexpr float kOriginSq    = 1.0f;   // unrealized soldiers sit at the world origin

    constexpr std::size_t   kHeliType               = 0x130;
    constexpr std::size_t   kHeliSight              = 0x98;
    constexpr std::size_t   kHeliCpController       = 0xC0;
    constexpr std::size_t   kCpPlayerInfo           = 0x58;
    constexpr std::size_t   kPlayerInfoPositionSlot = 0x18 / sizeof(void*);
    constexpr std::size_t   kSightChecker           = 0x08;
    constexpr std::size_t   kCheckerViewers         = 0x08;
    constexpr std::size_t   kCheckerActive          = 0x28;
    constexpr std::size_t   kCheckerStride          = 0x44;
    constexpr std::size_t   kCheckerRanges          = 0x80;
    constexpr std::size_t   kViewerStride           = 0x20;
    constexpr std::size_t   kRangeStride            = 0x2C;
    constexpr std::uint32_t kSightsPerViewer        = 3u;

    struct Eye
    {
        float position[3];
        float forward[3];
        float up[3];
        float range;
        float upLimit;   // sin of half the first sight angle
        float coneCos;   // cos of half the cone angle
    };

    struct Candidate
    {
        std::uint16_t id       = kNoTarget;
        float         distance = 0.0f;   // the support heli's four score parts
        float         seen     = 0.0f;
        float         hit      = 0.0f;
        float         threat   = 0.0f;
        bool          present  = false;
        bool          riding   = false;
        float         point[3] = {};
    };

    struct Brain
    {
        std::uint16_t heliId = kNoTarget;
        Candidate     slots[kSlotCount];
        std::uint32_t probe          = 0;
        std::uint16_t lock           = kNoTarget;
        std::uint64_t lockMs         = 0;
        std::uint64_t lastMs         = 0;
        std::uint64_t refreshMs      = 0;
        std::uint64_t lineMs         = 0;
        std::uint64_t blockedSinceMs = 0;
        std::uint64_t hitMs          = 0;
        bool          lineClear      = false;
        bool          eyeValid       = false;
        bool          playerValid    = false;
        float         player[3]      = {};
        Eye           eye{};
    };

    std::mutex                                              g_mtx;
    std::array<Brain, kMaxBrains>                           g_brains{};
    std::array<std::atomic<std::uint64_t>, HeliKey::kCount> g_fightingMs{};   // last frame it held a target, read without the lock

    bool IsSoldierId(std::uint16_t id)
    {
        return (id >> 9) == 2u;
    }

    float Dot(const float* a, const float* b)
    {
        return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
    }

    float DistanceSq(const float* a, const float* b)
    {
        const float d[3] = { a[0] - b[0], a[1] - b[1], a[2] - b[2] };
        return Dot(d, d);
    }

    void Describe(std::uint16_t id, char* out, std::size_t size)
    {
        if (id == kPlayerId)
            std::snprintf(out, size, "the player");
        else if (IsSoldierId(id))
            std::snprintf(out, size, "soldier %u", id & 0x1FFu);
        else
            std::snprintf(out, size, "heli 0x%04X", id);
    }

    bool ReadEyeSEH(const std::uint8_t* heli, std::uint32_t heliIndex, bool wide, Eye* eye)
    {
        __try
        {
            const std::uint8_t* sightController = *reinterpret_cast<const std::uint8_t* const*>(heli + kHeliSight);
            const std::uint8_t* checker = sightController ? *reinterpret_cast<const std::uint8_t* const*>(sightController + kSightChecker) : nullptr;
            if (!checker)
                return false;
            const std::uint8_t* viewers = *reinterpret_cast<const std::uint8_t* const*>(checker + kCheckerViewers);
            const std::uint8_t* active  = *reinterpret_cast<const std::uint8_t* const*>(checker + kCheckerActive);
            const std::uint8_t* ranges  = *reinterpret_cast<const std::uint8_t* const*>(checker + kCheckerRanges);
            const std::uint32_t stride  = *reinterpret_cast<const std::uint32_t*>(checker + kCheckerStride);
            if (!viewers || !active || !ranges || stride < kSightsPerViewer)
                return false;
            std::uint32_t sight = wide ? kWideSight : kSightsPerViewer;
            for (std::uint32_t s = 0; s < kSightsPerViewer && sight == kSightsPerViewer; ++s)
            {
                const std::uint32_t bit = heliIndex * stride + s;
                if ((active[bit >> 3] & (1u << (bit & 7u))) != 0)
                    sight = s;
            }
            if (sight == kSightsPerViewer)   // no sight open: it sees nothing
                return false;
            const float* range  = reinterpret_cast<const float*>(ranges + sight * kRangeStride);
            const float* viewer = reinterpret_cast<const float*>(viewers + heliIndex * kViewerStride);
            const float x = viewer[0], y = viewer[1], z = viewer[2], w = viewer[3];
            eye->forward[0] = 2.0f * (x * z + y * w);   // viewer-local +Z, the cone's axis
            eye->forward[1] = 2.0f * (y * z - x * w);
            eye->forward[2] = 1.0f - 2.0f * (x * x + y * y);
            eye->up[0]      = 2.0f * (x * y - z * w);
            eye->up[1]      = 1.0f - 2.0f * (x * x + z * z);
            eye->up[2]      = 2.0f * (y * z + x * w);
            eye->position[0] = viewer[4];
            eye->position[1] = viewer[5];
            eye->position[2] = viewer[6];
            eye->range   = range[0];
            eye->upLimit = range[2];
            eye->coneCos = range[3];
            return eye->range > 1.0f && eye->range < 2000.0f && eye->coneCos >= -1.0f && eye->coneCos <= 1.0f;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    bool ReadPlayerSEH(const std::uint8_t* heli, float* position)
    {
        __try
        {
            const std::uint8_t* cpController = *reinterpret_cast<const std::uint8_t* const*>(heli + kHeliCpController);
            void* playerInfo = cpController ? *reinterpret_cast<void* const*>(cpController + kCpPlayerInfo) : nullptr;
            if (!playerInfo)
                return false;
            alignas(16) float out[4] = {};
            reinterpret_cast<GetPlayerPosition_t>((*reinterpret_cast<void* const* const*>(playerInfo))[kPlayerInfoPositionSlot])(playerInfo, out);
            position[0] = out[0];
            position[1] = out[1];
            position[2] = out[2];
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    bool InCone(const Eye& eye, const float* point)
    {
        const float d[3] = { point[0] - eye.position[0], point[1] - eye.position[1], point[2] - eye.position[2] };
        const float dist = std::sqrt(Dot(d, d));
        if (dist > eye.range)
            return false;
        if (dist <= 1e-5f)
            return true;
        if (Dot(d, eye.up) / dist > eye.upLimit)
            return false;
        return Dot(d, eye.forward) / dist >= eye.coneCos;
    }

    bool ClearLine(const float* from, const float* point, float stop)
    {
        const float d[3] = { point[0] - from[0], point[1] - from[1], point[2] - from[2] };
        const float dist = std::sqrt(Dot(d, d));
        if (dist <= stop)
            return true;
        const float t = (dist - stop) / dist;
        const float to[3] = { from[0] + d[0] * t, from[1] + d[1] * t, from[2] + d[2] * t };
        bool blocked = true;
        return HeliLineOfSight::Blocked(from, to, HeliLineOfSight::kRayMask, &blocked) && !blocked;
    }

    float StopFor(std::uint16_t id)
    {
        return SoldierFaction_IsEnemyHeliId(id) ? kHeliStop : kSoldierStop;
    }

    float Score(const Candidate& c)
    {
        return c.distance + c.seen + c.hit + c.threat;
    }

    bool HostileTo(std::uint16_t heliId, std::uint16_t id)
    {
        return id == kPlayerId || SoldierFaction_AreHostileIds(heliId, id);
    }

    bool HeavyAttack(std::uint16_t attackId)   // the support heli raises threat for these
    {
        const std::uint32_t fromHeavy = static_cast<std::uint32_t>(attackId) - 0x105u;
        return (fromHeavy < 0x30u && ((0xFF00000000ADull >> fromHeavy) & 1u) != 0)
            || static_cast<std::uint32_t>(attackId) - 0x0Cu < 3u;
    }

    bool ReadTargetPoint(const std::uint8_t* heli, std::uint16_t id, float* point, bool* riding)
    {
        *riding = false;
        if (id == kPlayerId)
        {
            if (!ReadPlayerSEH(heli, point))
                return false;
            point[1] += kChest;
            return true;
        }
        if (SoldierFaction_IsEnemyHeliId(id))
            return SoldierFaction_ReadHeliPoint(id, point);
        bool standing = false;
        if (!SoldierFaction_ReadSoldierTarget(id, point, &standing, riding) || !standing || Dot(point, point) < kOriginSq)
            return false;
        point[1] += kChest;
        return true;
    }

    Brain* FindBrain(std::uint16_t heliId, bool create)
    {
        Brain* empty = nullptr;
        for (Brain& brain : g_brains)
        {
            if (brain.heliId == heliId)
                return &brain;
            if (!empty && brain.heliId == kNoTarget)
                empty = &brain;
        }
        if (!create || !empty)
            return nullptr;
        *empty = Brain{};
        empty->heliId = heliId;
        return empty;
    }

    Candidate* FindSlot(Brain& brain, std::uint16_t id, bool create)
    {
        Candidate* empty = nullptr;
        for (Candidate& slot : brain.slots)
        {
            if (slot.id == id)
                return &slot;
            if (!empty && slot.id == kNoTarget)
                empty = &slot;
        }
        if (!create || !empty)
            return nullptr;
        *empty = Candidate{};
        empty->id = id;
        return empty;
    }

    void Note(Brain& brain, std::uint16_t id, const float* point, bool riding, float distance)
    {
        Candidate* slot = FindSlot(brain, id, true);
        if (!slot)   // the list is full, like the support heli it ignores the rest
            return;
        slot->present  = true;
        slot->riding   = riding;
        slot->distance = 1.0f - (distance < kScoreRange ? distance : kScoreRange) * kScoreSlope;
        std::memcpy(slot->point, point, sizeof(slot->point));
    }

    void Refresh(Brain& brain, const std::uint8_t* heli, const float* from, float range)
    {
        for (Candidate& slot : brain.slots)
        {
            slot.present  = false;
            slot.distance = 0.0f;
        }
        const float rangeSq = range * range;
        float point[3];
        bool riding = false;
        if (ReadTargetPoint(heli, kPlayerId, point, &riding))   // first, so a crowd of soldiers never pushes the player off the list
        {
            const float distSq = DistanceSq(from, point);
            if (distSq <= rangeSq)
                Note(brain, kPlayerId, point, false, std::sqrt(distSq));
        }
        const std::uint32_t helis = SoldierFaction_FactionHeliCount();
        for (std::uint32_t i = 0; i < helis; ++i)
        {
            const std::uint16_t id = SoldierFaction_FactionHeliAt(i);
            if (id == brain.heliId || !SoldierFaction_AreHostileIds(brain.heliId, id) || !ReadTargetPoint(heli, id, point, &riding))
                continue;
            const float distSq = DistanceSq(from, point);
            if (distSq <= rangeSq)
                Note(brain, id, point, false, std::sqrt(distSq));
        }
        for (std::uint32_t i = 0; i < kSoldierCount; ++i)
        {
            const std::uint16_t id = static_cast<std::uint16_t>(0x400u | i);
            if (!SoldierFaction_AreHostileIds(brain.heliId, id) || !ReadTargetPoint(heli, id, point, &riding))
                continue;
            const float distSq = DistanceSq(from, point);
            if (distSq <= rangeSq)
                Note(brain, id, point, riding, std::sqrt(distSq));
        }
        for (Candidate& slot : brain.slots)
        {
            if (slot.id == kNoTarget)
                continue;
            if (slot.present && slot.riding)
                slot.threat = (std::fmin)(1.0f, slot.threat + kRiderThreat * (kRefreshMs / 1000.0f));
            if (!slot.present && slot.seen + slot.hit + slot.threat <= 0.0f)
                slot = Candidate{};
        }
    }

    void Fade(Brain& brain, float dt)
    {
        for (Candidate& slot : brain.slots)
        {
            if (slot.id == kNoTarget)
                continue;
            slot.seen   = (std::fmax)(0.0f, slot.seen - kSeenFade * dt);
            slot.hit    = (std::fmax)(0.0f, slot.hit - kHitFade * dt);
            slot.threat = (std::fmax)(0.0f, slot.threat - kThreatFade * dt);
        }
    }

    void Probe(Brain& brain)   // one ray per frame, round-robin over the list, as the support heli does
    {
        for (std::uint32_t n = 0; n < kSlotCount; ++n)
        {
            Candidate& slot = brain.slots[(brain.probe + n) % kSlotCount];
            if (slot.id == kNoTarget || !slot.present)
                continue;
            brain.probe = (brain.probe + n + 1) % kSlotCount;
            if (InCone(brain.eye, slot.point) && ClearLine(brain.eye.position, slot.point, StopFor(slot.id)))
                slot.seen = 1.0f;
            return;
        }
    }

    const Candidate* Best(const Brain& brain, bool threat, float minScore)
    {
        const Candidate* best = nullptr;
        for (const Candidate& slot : brain.slots)   // only what it saw in the last 5 s counts
        {
            if (slot.id == kNoTarget || !slot.present || slot.seen <= 0.0f || (slot.threat > 0.0f) != threat)
                continue;
            if (Score(slot) >= minScore && (!best || Score(slot) > Score(*best)))
                best = &slot;
        }
        return best;
    }

    void Pick(Brain& brain, std::uint64_t now)
    {
        const Candidate* best = Best(brain, true, kThreatTier);   // the support heli's order: a big threat, else a non-threat
        if (!best)
            best = Best(brain, false, 0.0f);
        const std::uint16_t pick = best ? best->id : kNoTarget;
        if (pick == kNoTarget)
            return;
        brain.lockMs = now;
        if (pick == brain.lock)
            return;
        brain.lock = pick;
        brain.lineMs = 0;
        brain.blockedSinceMs = 0;
        char text[24];
        Describe(pick, text, sizeof(text));
    }

    void Drop(Brain& brain, const char* reason)
    {
        char text[24];
        Describe(brain.lock, text, sizeof(text));
        brain.lock = kNoTarget;
        brain.lineClear = false;
        brain.blockedSinceMs = 0;
    }

    void Think(Brain& brain, const std::uint8_t* heli, std::uint64_t now)
    {
        const float dt = brain.lastMs ? (std::fmin)(kMaxStep, (now - brain.lastMs) / 1000.0f) : 0.0f;
        brain.lastMs = now;
        Fade(brain, dt);

        const std::uint32_t index = brain.heliId & 0x1FFu;
        const bool alerted = brain.hitMs != 0 && now - brain.hitMs < kAlertMs;
        brain.eyeValid = ReadEyeSEH(heli, index, alerted, &brain.eye);
        brain.playerValid = ReadPlayerSEH(heli, brain.player);
        if (now >= brain.refreshMs && brain.eyeValid)
        {
            brain.refreshMs = now + kRefreshMs;
            Refresh(brain, heli, brain.eye.position, (std::fmax)(brain.eye.range, kListRange));
        }
        if (brain.eyeValid)
            Probe(brain);

        if (brain.lock != kNoTarget)
        {
            Candidate* slot = FindSlot(brain, brain.lock, false);
            bool riding = false;
            if (!slot || !slot->present || !ReadTargetPoint(heli, brain.lock, slot->point, &riding))
            {
                if (slot)
                    slot->present = false;   // so Pick skips it until the next Refresh re-validates it
                Drop(brain, "it is down, gone or out of range");
            }
            else if (slot->seen <= 0.0f)
                Drop(brain, "unseen for 5 s");
        }
        if (brain.lock == kNoTarget)
            Pick(brain, now);

        if (brain.lock != kNoTarget && brain.eyeValid && now - brain.lineMs >= kLineMs)
        {
            brain.lineMs = now;
            const Candidate* slot = FindSlot(brain, brain.lock, false);
            brain.lineClear = slot && ClearLine(brain.eye.position, slot->point, StopFor(brain.lock));
            if (brain.lineClear)
                brain.blockedSinceMs = 0;
            else if (brain.blockedSinceMs == 0)
                brain.blockedSinceMs = now;
        }
    }
}

void SoldierFactionHeli_Update(void* heli, std::uint32_t heliIndex, std::uint64_t now)
{
    if (!heli || heliIndex > 0x1FFu)
        return;
    const std::uint8_t* object = static_cast<const std::uint8_t*>(heli);
    std::uint16_t type = 0;
    std::memcpy(&type, object + kHeliType, sizeof(type));
    if (!HeliKey::IsHeliType(type))
        return;
    CpControllerImpl_GetTargetPosition_Watch(heli);
    HeliInterest_SetLookAt_Watch(heli, type);

    const std::uint16_t heliId = static_cast<std::uint16_t>((type << 9) | heliIndex);
    std::uint16_t lock = kNoTarget;
    {
        std::lock_guard<std::mutex> lk(g_mtx);
        Brain* brain = FindBrain(heliId, true);
        if (!brain)
            return;
        Think(*brain, object, now);
        lock = brain->lock;
    }
    g_fightingMs[HeliKey::Of(type, heliIndex)].store(lock != kNoTarget ? now : 0, std::memory_order_relaxed);
}

bool SoldierFactionHeli_IsFighting(std::uint16_t heliId)
{
    if (!HeliKey::IsHeliType(heliId >> 9))
        return false;
    const std::uint64_t ms = g_fightingMs[HeliKey::OfGameObjectId(heliId)].load(std::memory_order_relaxed);
    return ms != 0 && GetTickCount64() - ms <= kFreshMs;
}

bool SoldierFactionHeli_AimPoint(std::uint16_t heliId, float* point)
{
    std::lock_guard<std::mutex> lk(g_mtx);
    Brain* brain = FindBrain(heliId, false);
    const Candidate* slot = brain && brain->lock != kNoTarget ? FindSlot(*brain, brain->lock, false) : nullptr;
    if (!slot)
        return false;
    point[0] = slot->point[0];
    point[1] = slot->point[1];
    point[2] = slot->point[2];
    point[3] = 0.0f;
    return true;
}

bool SoldierFactionHeli_LineClear(std::uint16_t heliId)
{
    std::lock_guard<std::mutex> lk(g_mtx);
    const Brain* brain = FindBrain(heliId, false);
    return brain && brain->lock != kNoTarget && brain->lineClear;
}

std::uint64_t SoldierFactionHeli_BlockedForMs(std::uint16_t heliId, std::uint64_t now)
{
    std::lock_guard<std::mutex> lk(g_mtx);
    const Brain* brain = FindBrain(heliId, false);
    if (!brain || brain->lock == kNoTarget || brain->blockedSinceMs == 0 || now < brain->blockedSinceMs)
        return 0;
    return now - brain->blockedSinceMs;
}

bool SoldierFactionHeli_OutsidePlayerGate(std::uint16_t heliId)
{
    std::lock_guard<std::mutex> lk(g_mtx);
    Brain* brain = FindBrain(heliId, false);
    const Candidate* slot = brain && brain->lock != kNoTarget ? FindSlot(*brain, brain->lock, false) : nullptr;
    return slot && brain->playerValid && DistanceSq(slot->point, brain->player) > kPlayerGateSq;
}

void SoldierFactionHeli_OnDecision(std::uint16_t heliId)
{
    const std::uint64_t now = GetTickCount64();
    std::lock_guard<std::mutex> lk(g_mtx);
    Brain* brain = FindBrain(heliId, false);
    if (brain && (brain->lock == kNoTarget || now - brain->lockMs >= kHoldMs))   // re-picks only between attack runs
        Pick(*brain, now);
}

void SoldierFactionHeli_OnHit(std::uint16_t heliId, std::uint16_t attackerId, std::uint16_t attackId)
{
    if (!HostileTo(heliId, attackerId) || !(attackerId == kPlayerId || IsSoldierId(attackerId) || SoldierFaction_IsEnemyHeliId(attackerId)))
        return;
    std::lock_guard<std::mutex> lk(g_mtx);
    Brain* brain = FindBrain(heliId, false);
    if (!brain)
        return;
    brain->hitMs = GetTickCount64();
    Candidate* slot = FindSlot(*brain, attackerId, true);
    if (!slot)   // full list: whoever shoots it takes the place of the weakest
    {
        for (Candidate& other : brain->slots)
        {
            if (other.id != brain->lock && (!slot || Score(other) < Score(*slot)))
                slot = &other;
        }
        if (!slot)
            return;
        *slot = Candidate{};
        slot->id = attackerId;
    }
    slot->hit = 1.0f;
    if (HeavyAttack(attackId))
        slot->threat = (std::fmin)(1.0f, slot->threat + kHeavyThreat);
}

void SoldierFactionHeli_Clear()
{
    {
        std::lock_guard<std::mutex> lk(g_mtx);
        for (Brain& brain : g_brains)
            brain = Brain{};
    }
    for (auto& ms : g_fightingMs)
        ms.store(0, std::memory_order_relaxed);
    HeliInterest_SetLookAt_Forget();
}
