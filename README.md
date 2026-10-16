# MGSV ReShade Fix

A small [ReShade](https://reshade.me) add-on that lets ReShade run natively (DirectX 11, no DXVK)
in **Metal Gear Solid V: The Phantom Pain**.

## The problem

Install ReShade for MGSV the normal way (`dxgi.dll` next to `mgsvtpp.exe`) and the game starts,
runs for about two seconds and then closes. There's no error message, no crash report and nothing in
the Windows event log. The usual workaround has been to run the game through DXVK and load ReShade
as a Vulkan layer.

It isn't a crash, and it isn't the Fox Engine "blocking hooks". The game shuts itself down
deliberately.

## Root cause

While it sets up its renderer, MGSV checks whether its Direct3D 11 device has been wrapped
(`mgsvtpp.exe+0x2B9BA0` in 1.0.15.4):

1. It lists its loaded modules (`EnumProcessModules` + `GetModuleBaseNameA`) and finds the one named
   `d3d11.dll`.
2. It asks for that module's address range (`GetModuleInformation`).
3. It checks that 97 entries of its D3D11 object's vtable point inside that range.

ReShade works by handing the game wrapper objects whose vtables live in ReShade's own DLL. The check
fails, renderer init returns false, the main loop never starts, and `WinMain` returns 0. The game
exits cleanly before its window is ever shown.

This also explains the older observations:

- **DXVK passes the check**, because its device objects live in a module that is itself called
  `d3d11.dll`.
- **Only the ancient ReShade 0.10 used to work.** It hooked the real D3D11 objects instead of
  wrapping them, so the vtables still pointed into the system `d3d11.dll`.

## How the fix works

The game calls `GetModuleInformation` from this check and nowhere else. The add-on redirects the
game's import of that function. While ReShade is wrapping a D3D11 device, a query for `d3d11.dll`
reports ReShade's module range instead, and the check passes.

What it doesn't touch:

- No game code is patched. The executable is protected, and only one import-table entry is changed.
- The import is looked up by name, not by a hardcoded address, so the fix should survive game updates
  unless the check itself changes.
- It only acts inside `mgsvtpp.exe` (and `MgsGroundZeroes.exe`, see below). In any other game it
  loads and does nothing.
- If ReShade isn't wrapping D3D11, for example in a DXVK + Vulkan-layer setup, it stays idle.

## Install

1. Install ReShade **with full add-on support** (6.5 or newer) for MGSV, choosing DirectX 10/11/12.
   This puts `dxgi.dll` next to `mgsvtpp.exe`.
2. Download `mgsv_reshade_fix.addon64` from [Releases](../../releases) and put it in the game folder
   next to `mgsvtpp.exe`. If you've set a custom `AddonPath` in `ReShade.ini`, put it in that folder
   instead.
3. Launch the game. The ReShade overlay's **Add-ons** tab should list **MGSV ReShade Fix**.

If you previously used DXVK as a workaround, remove DXVK's `d3d11.dll` and `dxgi.dll` from the game
folder before installing ReShade's `dxgi.dll`.

**Troubleshooting:** `ReShade.log` should contain
`[MGSV ReShade Fix] Reporting ReShade's module range for d3d11.dll to the game's wrapper check.`
If the game still closes, open an issue and attach your `ReShade.log`.

## Compatibility

| | |
|---|---|
| Tested | Steam, `mgsvtpp.exe` 1.0.15.4, ReShade 6.8.0, Windows 11 |
| Ground Zeroes | Enabled for `MgsGroundZeroes.exe` in case it has the same check. **Untested.** |
| MGO (`mgsvmgo.exe`) | Not touched |

The add-on only answers that one module lookup and doesn't read or write any game memory. Even so,
using ReShade or any add-on in online modes such as FOB is at your own risk.

## Building

You need Visual Studio 2022 (with the C++ workload) and CMake 3.20 or newer.

```bash
cmake -S . -B build -A x64
```

```bash
cmake --build build --config Release
```

The output is `build/Release/mgsv_reshade_fix.addon64`. Every push builds it in GitHub Actions, and
pushing a `v*` tag publishes it as a release.

The bundled headers in `third_party/reshade` come from the ReShade 6.5.0 add-on SDK (API version 17).
They're kept at that version on purpose: ReShade refuses to load add-ons built against a newer API
than the one it runs.

## License

MIT. See [LICENSE](LICENSE). The ReShade SDK headers are BSD 3-Clause, see
[third_party/reshade/LICENSE.md](third_party/reshade/LICENSE.md).
