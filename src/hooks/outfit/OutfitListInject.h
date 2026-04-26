#pragma once

namespace outfit
{
    // Hook on tpp::ui::menu::impl::ItemSelectorCallbackImpl::SetupPrefabListElement
    // (gAddr.ItemSelectorCallbackImpl_SetupPrefabListElement = 0x1416A9B80).
    //
    // SetupPrefabListElement is the function that builds the
    // sortie UNIFORMS panel row list. It iterates a flowIndex array,
    // filters via row[0x36]==0x14 && row[0x37]!='[', and calls
    // AddListSuit (0x1416A1AA0) for each passing entry. Two AddListSuit
    // call sites at 0x1416AA904 and 0x1416AAEA4 (verified
    // mgsvtpp.exe_Addresses.txt).
    //
    // Strategy: post-hook (call orig first, then append). After the
    // vanilla pass adds all developed vanilla suits, walk our outfit
    // registry filtered by current playerType. For each matching
    // outfit, invoke AddListSuit with the same arg shape the vanilla
    // call sites use:
    //   AddListSuit(this, &counter_local, flowIndex_u16, &entry_local)
    //
    // The two stack-locals (counter, entry) are passed as scratch
    // buffers; AddListSuit reads/writes through them but doesn't
    // retain pointers past return. Allocating them on our hook's
    // stack frame is safe.
    bool Install_OutfitListInject_Hook();
    void Uninstall_OutfitListInject_Hook();
}
