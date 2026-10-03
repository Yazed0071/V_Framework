#pragma once

#include <string>

namespace CustomScopeUi
{
    bool Install();
    void Uninstall();
    bool IsCreating();
    bool Configure(int scopeId, const std::string& factoryName, const std::string& uilbPath);
    void BindAlias(int scopeId, int alias);
    bool TryGetOverlay(unsigned int scopeId, unsigned int& overlayId);
}
