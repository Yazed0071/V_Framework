#pragma once

namespace outfit
{
    // Hooks `tpp::ui::menu::impl::CharacterSelectorCallbackImpl::ChangeDetailsWindowBuddySelect`
    // (retail 0x14163E5F0) so the SORTIE PREP > SELECT CHARACTER > UNIFORMS
    // row displays the registered outfit's `langEquipNameHash` instead of
    // blank text when wearing a custom partsType (0x40..0x7F).
    //
    // The vanilla translator returns a 0 hash for unrecognized partsTypes
    // (anything outside its switch's known case set). The hook lets orig
    // run, then for custom partsTypes overwrites the per-window hash
    // buffer (this+0xA0D0) with our outfit's stored hash and re-invokes
    // Quark's UI property setter so the text re-renders with our hash.
    bool Install_OutfitUniformsRow_Hook();
    void Uninstall_OutfitUniformsRow_Hook();
}
