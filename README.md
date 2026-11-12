# W3CrashFix

Fixes the frequent crashes of **The Witcher 3: Wild Hunt Remastered** (**DX12**) on PC.

It patches a bug in the game's code, in memory only, while the game runs. No game files are changed.

**Steam and GOG versions.** The fix was made and tested on the Steam version. On the GOG version it also switches itself on (reported by a user), but there is no long-term test result from GOG yet. On the Epic version it currently turns itself off and does nothing (reported by a user). Any other store version is untested.

| Bug | Symptom |
|---|---|
| The DX12 renderer leaks shader-visible descriptor heaps. After about 4,000 heaps, `CreateDescriptorHeap` fails with `E_OUTOFMEMORY`. The game doesn't check for this and uses a NULL pointer. | Crash to desktop after playing for a while. Access violation at `witcher3.exe+0x280ad3d`. |

**Results on the author's PC (see "Tested with" below for the full setup):**
- **Before:** over 30 crashes, all at `+0x280ad3d`.
- **With the fix:** the live heap count stays flat at 7–9 heaps instead of climbing to about 4,000. Many hours of play, including the spots that used to crash every time, heavy fast travel and combat: no crashes.

## Requirements

- The Witcher 3: Wild Hunt Remastered **5.0.16.41566** (`witcher3.exe` in `bin\x64_dx12`), **Steam or GOG version**. The Epic version is not supported yet: its game executable differs, so the fix turns itself off there. If you play on Epic or another store version, posting your `W3CrashFix.log` in an issue helps to add support.

**Version numbers** have the form `<mod version>-<game version>`. For example, `1.3.0-5.0.16` is mod version 1.3.0 for game patch 5.0.16. A release for a newer game patch will have a different game part.
- **DX12 mode only.** The DX11 executable (`bin\x64`) is not patched.

**Safe on other versions and after game updates.** Before patching anything, the mod checks the exact bytes of every function it touches. If that code is different (another store version or a game update), the fix turns itself off and logs `FIX DISABLED`. It never patches code it doesn't recognise.

### Tested with

- The Witcher 3: Wild Hunt Remastered, Steam version, 5.0.16.41566, DirectX 12.
- Ryzen 9 9950X3D, 64 GB RAM, RTX 5090 (32 GB), Windows 11, NVIDIA driver 32.0.16.2114.
- 4K, borderless window, HDR, VSync on, 60 fps limit (applies to the base frame rate).
- All graphics settings at maximum, path tracing on, NVIDIA HairWorks on.
- DLSS 5 in Quality mode with Ray Reconstruction and dynamic frame generation, through the RenoDX DLSS add-on by ShortFuse (build 2026-10-16) with ReShade 6.8.0, in DLSS-NR mode, with `nvngx_dlssnr.dll` 310.8.SF. Installed with DLSS5oneclick.
- Other mods: RT Shadow Fixes.

Other ways of installing DLSS 5 (for example DLSS Manager) are untested.

## Installation

Installing by hand is always recommended: it's one file, and you can see exactly what goes where. Choose **one** of the two versions. The ASI version is recommended.

### A. ASI plugin (`W3CrashFix.asi`) - recommended

It works alongside your other ASI mods.

1. Install an ASI loader in `<game folder>\bin\x64_dx12\`: [Ultimate ASI Loader](https://github.com/ThirteenAG/Ultimate-ASI-Loader/releases/latest) (x64), saved as `dinput8.dll`. Skip this if you already have one.
2. Copy `bin\x64_dx12\W3CrashFix.asi` from the zip into the same folder.

### B. Standalone (`dinput8.dll`)

Use this if you don't want an ASI loader. It can't be used if another mod already uses `dinput8.dll`.

1. Copy `bin\x64_dx12\dinput8.dll` from the zip into `<game folder>\bin\x64_dx12\`.

If you accidentally install both versions, only the one that loads first patches the game. The other one does nothing.

### Optional: the installer

The installer only exists for ease of use; manual installation is still recommended.

1. Download `W3CrashFix-<version>-Installer.exe` from the [latest release](https://github.com/Havoc104/W3CrashFix/releases/latest) on GitHub and run it. The installer is not offered on Nexus Mods.
2. It looks for the game in Steam, GOG and Epic. If it doesn't find it, click **Browse** and select the game folder.
3. Choose a version and click **Install**. You can use **Uninstall** later to remove it.

The installer checks your game version and won't overwrite another mod's `dinput8.dll`. For the ASI version, it checks that an ASI loader is installed. If none is found, it links you to Ultimate ASI Loader, which is a separate project and isn't included.

**Windows will show a warning** ("Windows protected your PC") when you start the installer. That's normal: the installer isn't code-signed, because a signing certificate costs money. It is safe to run. It only copies the W3CrashFix file into your game folder, and its full source code is in this repository (`src\installer.c`). Click **More info**, then **Run anyway**.

**Compatibility.** It works alongside ReShade (`dxgi.dll`), RenoDX and frame generation / DLSS mods. These were all in use during testing.

### Check that it works

Start the game and load a save. Then open `<game folder>\bin\x64_dx12\W3CrashFix.log`. It should contain:

```
==== W3CrashFix 1.3.0-5.0.16 (...) loaded, pid ... ====
fix: descriptor page recycling fix ACTIVE (w3+0x2809760 detoured)
hooks installed: yes (...)
```

Every 30 seconds the mod adds a status line to the log, including live descriptor-heap counts. The previous session's log is kept as `W3CrashFix.previous.log`, which helps after a crash.

### Uninstall

Use **Uninstall** in the installer, or delete `dinput8.dll` (standalone) or `W3CrashFix.asi` (ASI) from `bin\x64_dx12`.

## Technical details

### The descriptor page leak

The DX12 renderer hands out shader-visible CBV/SRV/UAV descriptors from 1024-entry "pages". Each page is its own `ID3D12DescriptorHeap`. The allocator works like this:

- When the current page is full, the function at `+0x2809760` moves it to a *retired* list and takes a page from the *free* list. If the free list is empty, it creates a new heap.
- Pages only move back from *retired* to *free* when a GPU-fence entry for them completes while they are no longer the current page.

The bug is in the case where the GPU keeps up with the CPU. A page's last fence entry often completes while that page is still the current page. When it's retired later, it has `pending == 0` and `usesThisFrame == 0`. It never gets another fence entry, so it stays in the retired list forever.

About 1 in 5 retirements is lost this way. That's roughly 4 new heaps per second, until the driver refuses to create more. `+0x2809760` then stores a NULL page without checking, and the next allocation dereferences it at `+0x280ad3d`.

**The fix** replaces `+0x2809760` with an identical version that has one change: a retired page that is already idle goes straight to the free list. "Idle" uses the game's own definition: no uses this frame, no pending GPU work. All other paths (in-flight pages, fence recycling, cached-table invalidation, heap creation) call the game's original code.

## Changelog

### 1.3.0-5.0.16

- The GOG version is now listed as supported: a user's log shows the fix switches itself on there, at the same addresses as on Steam. There is no long-term test result from GOG yet.
- In the log, NVIDIA's downloaded DLSS files now show their feature folder, for example `dlssg\160_E658700.bin` instead of `160_E658700.bin`. Several of those files share one file name, so the name alone didn't say which one created a heap.
- The descriptor page fix is unchanged.

### 1.2.0-5.0.16

- The log now names the file that created each descriptor heap, for example `caller=witcher3.exe+0x2809F44` instead of a bare address. The first time a file shows up, its full path is logged once (a path inside your user profile is written as `%USERPROFILE%`). This shows whether heaps come from the game itself or from another DLL, such as a DLSS add-on.
- On a game build the mod doesn't recognise (another store version or a game update), the log now gets `diag:` lines that describe where the game's code differs. Posting that log is what is needed to add support for that build. As before, nothing is patched in that case.
- Added a "Tested with" section that names the DLSS 5 setup used during testing.
- The descriptor page fix is unchanged.

### 1.1.0-5.0.16

- Removed "fix 2", the guard against invalid handle IDs after fast travel. It was added after two crashes at `witcher3.exe+0x1eecf31` and `+0x1eece9d` on the author's PC. In all play sessions since then, the guard never had to block a single lookup, and a test session with heavy fast travel and without the guard didn't crash. There is no sign that those two crashes came from a bug in the game itself, so the mod no longer touches that code.
- The descriptor page fix is unchanged.

### 1.0.0-5.0.16

- First release.

## Building from source

You need [Zig](https://ziglang.org/download/). Only its C compiler is used; no Visual Studio is needed.

```
.\build.ps1            # builds build\dinput8.dll, build\W3CrashFix.asi and the installer
.\build.ps1 -Package   # also creates the release zips in dist\
.\Install-W3CrashFix.ps1 [-Asi] [-Uninstall] [-GamePath <game folder>]
```

**Source files:**
- `src\w3crashfix.c`: the mod.
- `src\test_harness.c`: loads the DLL outside the game and exercises the D3D12 hooks.
- `src\test_diag.c`: test for the unsupported-build diagnostic and the caller names in the log. `build.ps1` builds and runs it.
- `src\installer.c`, `src\installer.rc`, `src\installer.manifest`: the installer. Both mod files are embedded in it.

## FAQ

**Does it improve performance?** No. It only stops the crashes. In a frame-time comparison with and without the fix, the difference was within noise.

**Can this get me banned?** The Witcher 3: Wild Hunt Remastered is single-player and has no anti-cheat. The mod uses the same loading method as many popular Witcher 3 mods.

**Will CD Projekt RED fix this?** Hopefully. Once the game is updated with a fix, this mod disables itself automatically.

## Support

W3CrashFix is free. If it saved your playthrough and you'd like to say thanks, you can buy me a coffee on Ko-fi:

[![Support on Ko-fi](https://img.shields.io/badge/Support%20on-Ko--fi-FF5E5B?logo=ko-fi&logoColor=white)](https://ko-fi.com/havoc104/tip)

## License and credits

MIT. See [LICENSE](LICENSE).

**Authors:** Onno Struijk (Havoc104) and Claude.

- Nexus Mods: [104Havoc](https://www.nexusmods.com/profile/104Havoc)
- GitHub: [Havoc104](https://github.com/Havoc104)
- Reddit: [u/Havoc_-_](https://www.reddit.com/user/Havoc_-_)
- Ko-fi: [havoc104](https://ko-fi.com/havoc104/tip)
