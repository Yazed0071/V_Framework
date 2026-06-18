<p align="center">
  <img src="guide/img/header.png" alt="V_Framework" width="100%">
</p>

# V Framework

V Framework is a DLL that loads into the running game, installs function-entry hooks across the FoxEngine via [MinHook](https://github.com/TsudaKageyu/minhook), and exposes the hooked behavior to game-side Lua through a set of custom native libraries. It is built to sit on top of **Infinite Heaven**.
More details can be found in the Wiki
https://mgsvmoddingwiki.github.io/V_Framework/
---

## Installing

> The repository ships no injector or loader. V_Framework is consumed as a native Lua extension under an Infinite Heaven setup.

1. Build `V_FrameWork.dll`.
2. Place the .dll next to the game's .exe.
3. The most important part, `V_FrameWork_Core.lua`, place it in mod/modules.
4. Packed assets are referenced from Lua as `/Assets/tpp/pack/V_FrameWork/...fpk` (you can download V Framework on nexus and take them from there.
5. Ensure `version_info.txt` in the module directory identifies your build (see the table above) so the correct address set is selected.
---

## Writing a mod

An example of adding a brand new custom cassette tape!
```lua
local this = {}

function this.LoadLibraries()
    V_TppCassette.RegisterCustomCassetteAlbum(
        { albumId = "GZ_bgm_03", langId = "GZ_bgm_03", type = "PREINSTALL_MUSIC" },
        {
            {
                langId     = "GZ_tp_bgm_03_01",
                fileName   = "GZ_tp_bgm_03_01",
                dataTimeEn = 188e3,
                dataTimeJp = 188e3,
                important  = 0,
                special    = 0,
                unlocked   = 1,
            },
        }
    )
end

return this
```
---

## Third-party

- [MinHook](https://github.com/TsudaKageyu/minhook) — function-entry hooking library (vendored).
- Lua 5.1 — headers for the FoxEngine's embedded Lua (vendored).
