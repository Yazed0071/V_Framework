#include "pch.h"
#include "FoxHashes.h"
#include "HookUtils.h"
#include "AddressSet.h"

#include <Windows.h>
#include <algorithm>
#include <cstdint>
#include <string>

namespace
{
    using FoxStrHash32_t = uint32_t(__fastcall*)(char* str);
    using FoxStrHash64_t = uint64_t(__fastcall*)(char* str);
    using PathHashCode_t = uint64_t(__fastcall*)(char* str);
    using FNVHash32_t    = uint32_t(__fastcall*)(const char* str);

    static FoxStrHash32_t g_FoxStrHash32 = nullptr;
    static FoxStrHash64_t g_FoxStrHash64 = nullptr;
    static PathHashCode_t g_PathHashCode = nullptr;
    static FNVHash32_t    g_FNVHash32    = nullptr;

    static uint32_t Fnv1Hash32Lower(const char* text)
    {
        uint32_t hash = 0x811C9DC5u;
        for (const char* p = text; *p; ++p)
        {
            unsigned char c = static_cast<unsigned char>(*p);
            if (c >= 'A' && c <= 'Z')
                c = static_cast<unsigned char>(c - 'A' + 'a');
            hash *= 0x01000193u;
            hash ^= c;
        }
        return hash;
    }
}

namespace FoxHashes
{
    bool Resolve()
    {
        if (g_FoxStrHash32 && g_FoxStrHash64 && g_PathHashCode)
            return true;

        if (!g_FoxStrHash32)
        {
            g_FoxStrHash32 = reinterpret_cast<FoxStrHash32_t>(
                ResolveGameAddress(gAddr.FoxStrHash32));
        }

        if (!g_FoxStrHash64)
        {
            g_FoxStrHash64 = reinterpret_cast<FoxStrHash64_t>(
                ResolveGameAddress(gAddr.FoxStrHash64));
        }

        if (!g_PathHashCode)
        {
            g_PathHashCode = reinterpret_cast<PathHashCode_t>(
                ResolveGameAddress(gAddr.PathHashCode));
        }

        return g_FoxStrHash32 && g_FoxStrHash64 && g_PathHashCode;
    }

    std::string NormalizeAssetPath(const std::string& path)
    {
        std::string out = path;
        std::replace(out.begin(), out.end(), '\\', '/');

        if (out.empty())
            return out;

        if (out.rfind("Assets/", 0) == 0)
            out = "/" + out;

        if (!out.empty() && out.front() != '/')
            out = "/" + out;

        return out;
    }

    uint32_t StrCode32(const char* text)
    {
        if (!Resolve() || !text || !*text)
            return 0;

        std::string temp(text);
        return g_FoxStrHash32(&temp[0]);
    }

    uint32_t StrCode32(const std::string& text)
    {
        if (!Resolve() || text.empty())
            return 0;

        std::string temp(text);
        return g_FoxStrHash32(&temp[0]);
    }

    uint32_t FNVHash32(const char* text)
    {
        if (!text || !*text)
            return 0;

        if (!g_FNVHash32 && gAddr.FNVHash32)
            g_FNVHash32 = reinterpret_cast<FNVHash32_t>(ResolveGameAddress(gAddr.FNVHash32));

        if (g_FNVHash32)
        {
            std::string temp(text);
            return g_FNVHash32(&temp[0]);
        }

        return Fnv1Hash32Lower(text);
    }

    uint32_t FNVHash32(const std::string& text)
    {
        return text.empty() ? 0u : FNVHash32(text.c_str());
    }

    uint64_t StrCode64(const char* text)
    {
        if (!Resolve() || !text || !*text)
            return 0;

        std::string temp(text);
        return g_FoxStrHash64(&temp[0]);
    }

    uint64_t StrCode64(const std::string& text)
    {
        if (!Resolve() || text.empty())
            return 0;

        std::string temp(text);
        return g_FoxStrHash64(&temp[0]);
    }

    uint64_t PathCode64Ext(const char* path)
    {
        if (!Resolve() || !path || !*path)
            return 0;

        std::string normalized = NormalizeAssetPath(path);
        return g_PathHashCode(&normalized[0]);
    }

    uint64_t PathCode64Ext(const std::string& path)
    {
        if (!Resolve() || path.empty())
            return 0;

        std::string normalized = NormalizeAssetPath(path);
        return g_PathHashCode(&normalized[0]);
    }
}