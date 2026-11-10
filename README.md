# W3CrashFix

Fixes the frequent crashes of **The Witcher 3: Wild Hunt Remastered** (**DX12**) on PC.

It patches two bugs in the game's code, in memory only, while the game runs. No game files are changed.

| | Bug | Symptom |
|---|---|---|
| **Fix 1** | The DX12 renderer leaks shader-visible descriptor heaps. After about 4,000 heaps, `CreateDescriptorHeap` fails with `E_OUTOFMEMORY`. The game doesn't check for this and uses a NULL pointer. | Crash to desktop after 20 minutes to a few hours. Access violation at `witcher3.exe+0x280ad3d`. |
| **Fix 2** | After fast travel, a game system can look up objects with stale or invalid handle IDs. The lookup only rejects ID `-1`, so it reads far outside its table. | Crash shortly after fast travel. Access violation at `witcher3.exe+0x1eecf31` or `+0x1eece9d`. |

**Results on the author's PC (RTX 5090, path tracing, DLSS RR + frame generation):**
- **Before:** over 30 crashes, all at `+0x280ad3d`.
- **With the fix:** the live heap count stays flat at 7–9 heaps instead of climbing to about 4,000. Many hours of play, including the spots that used to crash every time, heavy fast travel and combat: no crashes.

## Requirements

- The Witcher 3: Wild Hunt Remastered **5.0.16.41566** (`witcher3.exe` in `bin\x64_dx12`). It was tested on the Steam version. GOG and Epic should work if their executable is identical.

**Version numbers** have the form `<mod version>-<game version>`. For example, `1.0.0-5.0.16` is mod version 1.0.0 for game patch 5.0.16. A release for a newer game patch will have a different game part.
- **DX12 mode only.** The DX11 executable (`bin\x64`) is not patched.

**Safe after game updates.** Before patching anything, the mod checks the exact bytes of every function it touches. If the game gets updated and the code changes, each fix turns itself off and logs that. It never patches code it doesn't recognise.

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

1. Download `W3CrashFix-<version>-Installer.exe` from the [latest release](https://github.com/Havoc104/W3CrashFix/releases/latest) and run it. On Nexus Mods, the installer comes as a zip; extract it first.
2. It looks for the game in Steam, GOG and Epic. If it doesn't find it, click **Browse** and select the game folder.
3. Choose a version and click **Install**. You can use **Uninstall** later to remove it.

The installer checks your game version and won't overwrite another mod's `dinput8.dll`. For the ASI version, it checks that an ASI loader is installed. If none is found, it links you to Ultimate ASI Loader, which is a separate project and isn't included.

**Windows will show a warning** ("Windows protected your PC") when you start the installer. That's normal: the installer isn't code-signed, because a signing certificate costs money. It is safe to run. It only copies the W3CrashFix file into your game folder, and its full source code is in this repository (`src\installer.c`). Click **More info**, then **Run anyway**.

**Compatibility.** It works alongside ReShade (`dxgi.dll`), RenoDX and frame generation / DLSS mods. These were all in use during testing.

### Check that it works

Start the game and load a save. Then open `<game folder>\bin\x64_dx12\W3CrashFix.log`. It should contain:

```
==== W3CrashFix 1.0.0-5.0.16 (...) loaded, pid ... ====
fix: descriptor page recycling fix ACTIVE (w3+0x2809760 detoured)
fix2: invalid-id guard ACTIVE (vtable w3+0x3828918, 9 methods, ids 1..234276)
hooks installed: yes (...)
```

Every 30 seconds the mod adds a status line to the log, including live descriptor-heap counts. The previous session's log is kept as `W3CrashFix.previous.log`, which helps after a crash.

### Uninstall

Use **Uninstall** in the installer, or delete `dinput8.dll` (standalone) or `W3CrashFix.asi` (ASI) from `bin\x64_dx12`.

## Technical details

### Fix 1: descriptor page leak

The DX12 renderer hands out shader-visible CBV/SRV/UAV descriptors from 1024-entry "pages". Each page is its own `ID3D12DescriptorHeap`. The allocator works like this:

- When the current page is full, the function at `+0x2809760` moves it to a *retired* list and takes a page from the *free* list. If the free list is empty, it creates a new heap.
- Pages only move back from *retired* to *free* when a GPU-fence entry for them completes while they are no longer the current page.

The bug is in the case where the GPU keeps up with the CPU. A page's last fence entry often completes while that page is still the current page. When it's retired later, it has `pending == 0` and `usesThisFrame == 0`. It never gets another fence entry, so it stays in the retired list forever.

About 1 in 5 retirements is lost this way. That's roughly 4 new heaps per second, until the driver refuses to create more. `+0x2809760` then stores a NULL page without checking, and the next allocation dereferences it at `+0x280ad3d`.

**The fix** replaces `+0x2809760` with an identical version that has one change: a retired page that is already idle goes straight to the free list. "Idle" uses the game's own definition: no uses this frame, no pending GPU work. All other paths (in-flight pages, fence recycling, cached-table invalidation, heap creation) call the game's original code.

### Fix 2: invalid handle IDs after fast travel

One game object (vtable `+0x3828918`) keeps a fixed table of `0x1c0`-byte entries, addressed by a 1-based ID: `entry = obj + (id - 1 + 0x1345) * 0x1c0`. Its methods only reject `id == -1`.

After fast travel, a caller (`+0x1d27043`) was observed passing ID `0` and ID `0x40666AEE`. The second value is the float `3.6`, which means the ID was read from memory that had already been freed and reused. Both lookups read far outside the table and crashed.

**The fix** puts a small check in front of the 9 methods that take such an ID. Any ID outside `1..234276` becomes `-1`, so the method takes the game's own "no such object" early return. 234,276 is the most entries that fit before the next field of the object.

The log counts how often this happens: `fix2: blocked ...`. In normal play the count is 0.

## Building from source

You need [Zig](https://ziglang.org/download/). Only its C compiler is used; no Visual Studio is needed.

```
.\build.ps1            # builds build\dinput8.dll + build\W3CrashFix.asi and runs the unit test
.\build.ps1 -Package   # also creates the release zips in dist\
.\Install-W3CrashFix.ps1 [-Asi] [-Uninstall] [-GamePath <game folder>]
```

**Source files:**
- `src\w3crashfix.c`: the mod.
- `src\idstub.h`: the fix-2 guard code generator.
- `src\test_stub.c`: unit tests for the guard code.
- `src\test_harness.c`: loads the DLL outside the game and exercises the D3D12 hooks.
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
