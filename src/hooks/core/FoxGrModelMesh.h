#pragma once
#include <cstdint>

namespace FoxGrModelMesh
{
    bool IsReady();
    bool SetVisible(void* model, std::uint32_t meshId, bool visible);
    bool IsVisible(void* model, std::uint32_t meshId, bool& visible);
}
