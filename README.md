# W3CrashFix

Fixes the frequent crashes of **The Witcher 3: Wild Hunt Remastered** (**DX12**) on PC.

It patches a bug in the game's code, in memory only, while the game runs. No game files are changed.

**Steam and GOG versions.** The fix was made and tested on the Steam version. On the GOG version it also switches itself on (reported by a user), but there is no long-term test result from GOG yet. On the Epic version it currently turns itself off and does nothing (reported by a user). Any other store version is untested.

**Playing on Epic? Please help to make it work for you.** Start the game once with the mod installed, then post `W3CrashFix.activity.log` (next to the mod, in `bin\x64_dx12`) in a [GitHub issue](https://github.com/Havoc104/W3CrashFix/issues) or in the Posts tab on Nexus Mods. Logs from the Epic version are what's needed to add support for it.

| Bug | Symptom |
|---|---|
| The DX12 renderer leaks shader-visible descriptor heaps. After about 4,000 heaps, `CreateDescriptorHeap` fails with `E_OUTOFMEMORY`. The game doesn't check for this and uses a NULL pointer. | Crash to desktop after playing for a while. Access violation at `witcher3.exe+0x280ad3d`. |

**Video (3 minutes):** [how the crash works, and the fix](https://youtu.be/0h5bh2ZFebU).

**Results on the author's PC (see "Tested with" below for the full setup):**
- **Before:** over 30 crashes, all at `+0x280ad3d`.
- **With the fix:** the live heap count stays flat at 7–9 heaps instead of climbing to about 4,000. Many hours of play, including the spots that used to crash every time, heavy fast travel and combat: no crashes.

## Requirements

- The Witcher 3: Wild Hunt Remastered **5.0.16.41566** (`witcher3.exe` in `bin\x64_dx12`), **Steam or GOG version**. The Epic version is not supported yet: its game executable differs, so the fix turns itself off there. If you play on Epic or another store version, posting your `W3CrashFix.activity.log` in an issue helps to add support.

**Version numbers** have the form `<mod version>-<game version>`. For example, `1.7.0-5.0.16` is mod version 1.7.0 for game patch 5.0.16. A release for a newer game patch will have a different game part.
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

### Other mods

W3CrashFix changes no game files and no scripts, so it should work alongside other mods. That can't be promised for every mod: only what is listed under "Tested with" has actually been played with.

**A crash report names your in-game mods.** In-game mods are the ones the game itself loads: the folders in the game's `Mods` folder, and add-ons in its `DLC` folder. The mod notes their folder names at the start of its log, and a crash report lists them, so that whoever reads the report can rule them in or out. Nothing is switched off because of a mod. DLL and ASI mods (ReShade, RenoDX, DLSS add-ons, other ASI plugins) are not listed.

## Installation

Installing by hand is always recommended: it's one file, and you can see exactly what goes where. Choose **one** of the two versions. The ASI version is recommended.

### A. ASI plugin (`W3CrashFix.asi`) - recommended

It works alongside your other ASI mods.

1. Install [Ultimate ASI Loader](https://github.com/ThirteenAG/Ultimate-ASI-Loader/releases/latest) (x64) in `<game folder>\bin\x64_dx12\`, saved as `dinput8.dll`. Skip this if you already have it. W3CrashFix is made for and tested with Ultimate ASI Loader only. You are free to try another ASI loader, but that is untested and not supported.
2. Copy `bin\x64_dx12\W3CrashFix.asi` from the zip into the same folder.

### B. Standalone (`dinput8.dll`)

Use this if you don't want to install Ultimate ASI Loader. The folder can hold only one `dinput8.dll`: if another mod already uses `dinput8.dll` (Ultimate ASI Loader, for example), this one replaces it and that mod stops working, and a mod that puts its own `dinput8.dll` there later switches W3CrashFix off in the same way. If another mod uses `dinput8.dll`, choose the ASI version.

1. Copy `bin\x64_dx12\dinput8.dll` from the zip into `<game folder>\bin\x64_dx12\`.

If you accidentally install both versions, only the one that loads first patches the game. The other one does nothing.

### Optional: the installer/uninstaller

A small program that installs either version for you and can remove it again. The installer only exists for ease of use; manual installation is still recommended.

1. Download `W3CrashFix-<version>-Installer.exe` from the [latest release](https://github.com/Havoc104/W3CrashFix/releases/latest) on GitHub and run it. The installer is not offered on Nexus Mods.
2. It looks for the game in Steam, GOG and Epic. If it doesn't find it, click **Browse** and select the game folder.
3. Choose a version and click **Install**. The installer shows the license first; see [The license window](#the-license-window). You can use **Uninstall** later to remove the mod.
4. **Run game** starts the game once W3CrashFix is installed. A Steam copy is started through Steam. If the ASI version is installed without Ultimate ASI Loader, the installer first reminds you that other ASI loaders are untested and not supported.

**Uninstall** leaves a clean folder: besides the mod's own file it removes `W3CrashFix.ini` (the mod's settings: your license acceptance and the report id) and `W3CrashFix.removed` (left after declining), as well as the crash log with its folder, the report page and the activity logs. It leaves no traces of the mod behind.

The installer checks your game version and won't overwrite another mod's `dinput8.dll`. For the ASI version, it checks that Ultimate ASI Loader is installed. If it isn't found, it links you to it. Ultimate ASI Loader is a separate project and isn't included. Without it the installer warns you and asks before it installs the ASI version, so you can still try another ASI loader at your own risk.

**Windows will show a warning** ("Windows protected your PC") when you start the installer. That's normal: the installer isn't code-signed, because a signing certificate costs money. It is safe to run. It only copies the W3CrashFix file into your game folder, and its full source code is in this repository (`src\installer.c`). Click **More info**, then **Run anyway**.

ReShade (`dxgi.dll`), RenoDX and frame generation / DLSS mods were all in use during testing. See also [Other mods](#other-mods).

### Check that it works

Start the game, accept the license if the window appears (see below), and load a save. Then open the mod's activity log, `<game folder>\bin\x64_dx12\W3CrashFix.activity.log`. It should contain:

```
==== W3CrashFix 1.7.0-5.0.16 (...) loaded, pid ... ====
fix: descriptor page recycling fix ACTIVE (w3+0x2809760 detoured)
hooks installed: yes (...)
```

Every 30 seconds the mod adds a status line to the log, including live descriptor-heap counts. The previous session's log is kept as `W3CrashFix.activity.previous.log`.

### Settings

The mod keeps its few settings in one small file next to itself, `W3CrashFix.ini`, which you can open with Notepad:

```ini
[W3CrashFix]
LicenseAccepted=MIT-2
ReportId=0123456789ABCDEF
OpenReportPage=1
```

- `LicenseAccepted`: the license text you accepted. See [The license window](#the-license-window).
- `ReportId`: a random number, used only in crash reports. See [If the game crashes](#if-the-game-crashes).
- `OpenReportPage`: set it to `0` if nothing should open after a crash. The crash log is still saved.

### The license window

The first time you start the game with W3CrashFix, a small window appears before the game does. It shows the MIT License, under which the mod is published, and says in plain words that the mod comes without any warranty.

- **OK (Apply this mod)** accepts the license and switches the fix on. The window does not appear again unless the license itself changes; when it does, the window says so.
- To decline, tick **I decline. Remove W3CrashFix from this game.** The button then reads **OK (Remove this mod)**. The fix stays off, and the mod renames its own file to `W3CrashFix.removed`, so that it never loads again.
- **View source code** opens this repository in your browser.

The game waits until you have chosen. The window has no close button on purpose. The Enter key works as OK once the window has been open for three seconds, so you can also answer it without a mouse.

Until you accept, the mod does nothing at all: the fix is off, and there is no crash report and no logging of heap statistics.

**Why is a file left behind after declining?** Windows does not allow a program to delete a file that is in use, and the game has the mod loaded for as long as it runs. Renaming is allowed, so that is what the mod does. It does not start a helper program to delete the file afterwards, because that is the kind of behaviour antivirus software is suspicious of. You can delete `W3CrashFix.removed` yourself, or let the installer's **Uninstall** do it.

If a mod manager (Vortex, for example) installed the mod for you, remove it in the mod manager instead of declining. A mod manager keeps track of the files it installed and may put the renamed file back.

If you use the installer, the license is shown there when you click **Install**, and the game starts without the window.

On a game version the mod does not support, the window does not appear: the fix is off there anyway.

### If the game crashes

The mod notices a crash and helps you report it:

- It writes what crashed (file and offset) into its activity log.
- It saves the crashed session as `W3CrashFix.crash.log`, in a folder of its own next to the mod: `W3CrashFix crash report`. The file starts with a short summary: mod version, a random report id, the in-game mods that were enabled, whether the fix was active, the crash address and the processor's registers, heap counts, memory, and a verdict that says whether this is the crash W3CrashFix fixes. Only a newer crash overwrites that file.
- It opens a small folder window on that folder, in the lower right corner of your screen, and then a report page, `W3CrashFix-report.html`, in your browser. The page shows the summary and has one button, **Send the crash log**.

**Sending a report.** The button opens an upload page of the author's online storage (pCloud) and copies the crash log's path at the same time. On that page, click **Click Here to Add Files**, press Ctrl+V in the window that opens, and then Enter. Or drag the crash log from the small folder window onto the page; that window asks Windows to keep it on top. Then type any name you like (a made-up one is fine) and click Upload. You don't need an account.

**The report page is a file on your own PC, not a website, and the mod itself never connects to the internet.** Only your browser does, and only when you click the button. Nothing leaves your PC unless you upload the file yourself. The reason the report is shown in your browser is explained at the bottom of that page.

**What a report contains.** The crash log holds the summary and the mod's activity log of the crashed session: technical data. It can include the folder the game is installed in and the folder names of your in-game mods (see [Other mods](#other-mods)). Only the author can see uploaded files. A report sent this way can't be answered; for questions, use the Posts tab on Nexus Mods or the issues here on GitHub.

**The report id** is a random number made on your PC the first time the mod works. Its only use is that several reports from the same machine/installation can be recognised as such. It is not derived from your hardware, Windows or your name, and it says nothing about you. It is kept as `ReportId` in `W3CrashFix.ini`; remove that line to get a new one.

**Crashes with another cause are welcome too.** The crash log is made for every crash, not only for the one W3CrashFix fixes. Such reports might be analysed in an attempt to provide a fix. That is not a promise.

The game's own crash reporter may also appear after a crash. That one reports to the game's developers; the W3CrashFix page is only about this mod. You can use both.

If you don't want anything to open after a crash, set `OpenReportPage=0` in `W3CrashFix.ini` (see [Settings](#settings)). The crash log is still saved.

### Uninstall

Use **Uninstall** in the installer, or delete `dinput8.dll` (standalone) or `W3CrashFix.asi` (ASI) from `bin\x64_dx12`. You can also delete `W3CrashFix.ini` and, if they exist, `W3CrashFix.removed`, `W3CrashFix-report.html`, `W3CrashFix.activity.log`, `W3CrashFix.activity.previous.log` and the folder `W3CrashFix crash report`. The installer's Uninstall removes all of these, so no traces of the mod are left.

## Technical details

### The descriptor page leak

The DX12 renderer hands out shader-visible CBV/SRV/UAV descriptors from 1024-entry "pages". Each page is its own `ID3D12DescriptorHeap`. The allocator works like this:

- When the current page is full, the function at `+0x2809760` moves it to a *retired* list and takes a page from the *free* list. If the free list is empty, it creates a new heap.
- Pages only move back from *retired* to *free* when a GPU-fence entry for them completes while they are no longer the current page.

The bug is in the case where the GPU keeps up with the CPU. A page's last fence entry often completes while that page is still the current page. When it's retired later, it has `pending == 0` and `usesThisFrame == 0`. It never gets another fence entry, so it stays in the retired list forever.

About 1 in 5 retirements is lost this way. That's roughly 4 new heaps per second, until the driver refuses to create more. `+0x2809760` then stores a NULL page without checking, and the next allocation dereferences it at `+0x280ad3d`.

**The fix** replaces `+0x2809760` with an identical version that has one change: a retired page that is already idle goes straight to the free list. "Idle" uses the game's own definition: no uses this frame, no pending GPU work. All other paths (in-flight pages, fence recycling, cached-table invalidation, heap creation) call the game's original code.

## Changelog

### 1.7.0-5.0.16

Thanks to everyone who sent a crash log. The first logs that came in through the report page already led to this release: they showed where the crash report could tell more. So far seven crash reports from six players have arrived. None of them was the crash this fix is made for, and every one of them helped.

- Crash report: it now lists **all** your in-game mods, with their number. Before, a long list was cut off after about 600 characters, so with many mods it didn't show which ones were enabled.
- Crash report: it now also shows the **call stack**, a short list of places in the game's code that led to the crash. Two players' crashes stopped at the same place, where the game halts itself when something goes wrong; only the code that led there can show what went wrong.
- Crash report: the verdict also recognises the descriptor heap crash when it shows up one step deeper in the game's code.
- You can now **personalise your report id**. `W3CrashFix.ini` explains how, right above the `ReportId` line: add your own text before or after the id, for example `MyName_5E19AF37C0802875`, and keep the 16 characters of the id itself, so your new crash logs can still be matched with the ones you sent before. Before this release, the mod replaced any id it didn't make itself with a new random one.
- Installer: **Uninstall leaves no traces**: it now also removes the activity logs.
- After a crash, the small folder window gets a little more time and up to four requests to stay on top of the browser. Windows can still refuse it, but less often.
- Clearer wording about the standalone version: only one `dinput8.dll` can be in the folder.
- The descriptor page fix is unchanged.

**Do I need to update?** The fix itself is the same, so if your game runs fine, you can keep your version. But if you ever send a crash log, a report from 1.7.0 tells much more. Every log counts, also when it isn't the crash this mod fixes: send it with the button on the report page.

**Video:** [how the crash works, and the fix](https://youtu.be/0h5bh2ZFebU) (3 minutes).

### 1.6.1-5.0.16

- Crash report, sending: the **Send the crash log** button now also copies the crash log's path. On the upload page you click **Click Here to Add Files**, paste with Ctrl+V and press Enter, so the folder window is no longer needed. In 1.6.0 that window went behind the browser as soon as you clicked there. Dragging still works.
- The small folder window now asks Windows to keep it on top of other windows. Windows doesn't always allow that; the activity log says whether it did.
- The report page is shorter: it shows only whether the fix was active and the verdict. The full summary stays at the top of the crash log, which is the file you send.
- The descriptor page fix is unchanged.

**Do I need to update?** No, the fix itself is the same. 1.6.1 only makes sending a crash report easier.

### 1.6.0-5.0.16

- Crash report, sending: the report page now has one button, **Send the crash log**. It opens an upload page of the author's online storage, where you drop the crash log; no account is needed and only the author sees the file. It replaces the two buttons for GitHub and Nexus Mods. The mod itself still never connects to the internet. See [If the game crashes](#if-the-game-crashes).
- Crash report, easier to use: the crash log now has a folder of its own, `W3CrashFix crash report`, and after a crash a small folder window opens on it, so the file is ready to drag.
- Crash report, more useful: the summary now also shows the processor's registers, the memory situation, whether the game build is supported, the in-game mods that were enabled, and a random report id that lets several reports from one installation be recognised.
- Settings: one readable file, `W3CrashFix.ini`, replaces the small files `W3CrashFix.accepted` and `W3CrashFix.noreport`. Existing ones are taken over automatically. See [Settings](#settings).
- The mod's log is now called `W3CrashFix.activity.log`, so it can't be mixed up with the crash log. Logs under the old name are tidied up at the first start.
- License: the copyright line now names one copyright holder, and the license has a new section, "Additional permission", which lets CD PROJEKT RED S.A. use the fix without the usual notices. See [LICENSE](LICENSE). Because the license text changed, the license window appears once more after updating, and says why.
- Installer: it now has an icon, is called installer/uninstaller, shows the license in a window that scrolls, names Ultimate ASI Loader as the one supported loader, and on Uninstall also removes the settings file and what a crash report left.
- The descriptor page fix is unchanged.

**Do I need to update?** No. If an earlier version works for you, you can keep it: the fix itself is the same.

Updating does help the project, though. If you ever run into a crash, the newest version is the one built to analyse it: it records more about what crashed and makes sending the report a matter of one drag. A report from the newest version is the most useful one I can get.

### 1.5.0-5.0.16

- New: a license window at the first game start. It shows the MIT License and asks you to accept it before the fix is switched on; declining removes the mod from the game. See [The license window](#the-license-window).
- Installer: the license is shown on Install, a new **Run game** button, and Uninstall now also removes the files the license window leaves behind.
- The descriptor page fix is unchanged.

### 1.4.1-5.0.16

- New: crash report. When the game crashes, the mod writes what crashed into the log, saves the session as `W3CrashFix.crash.log` with a summary and a verdict, and opens a local page with two buttons to report it (GitHub or Nexus Mods). The mod itself sends nothing. See [If the game crashes](#if-the-game-crashes).
- The descriptor page fix is unchanged.

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
.\build.ps1 -CrashTest # test build with keys that crash the game on purpose; never packaged or released
.\Install-W3CrashFix.ps1 [-Asi] [-Uninstall] [-GamePath <game folder>]
```

**Source files:**
- `src\w3crashfix.c`: the mod.
- `src\w3cf_license.h`: the license text as shown to the user, shared by the mod and the installer.
- `src\test_harness.c`: loads the DLL outside the game and exercises the D3D12 hooks.
- `src\test_diag.c`: test for the unsupported-build diagnostic, the caller names in the log, the crash report, the settings file and the license window. `build.ps1` builds and runs it.
- `src\installer.c`, `src\installer.rc`, `src\installer.manifest`: the installer. Both mod files are embedded in it.
- `src\installer-icon.svg`, `src\installer.ico`: the installer's icon, the cat from the project's banner.

## FAQ

**Does it improve performance?** No. It only stops the crashes. In a frame-time comparison with and without the fix, the difference was within noise.

**Can this get me banned?** The Witcher 3: Wild Hunt Remastered is single-player and has no anti-cheat. The mod uses the same loading method as many popular Witcher 3 mods.

**Will CD Projekt RED fix this?** Hopefully. Once the game is updated with a fix, this mod disables itself automatically.

## About the author

> I'm Havoc, just a guy trying out vibe coding for the first time: I describe what I want, and an AI writes the code. W3CrashFix is a side project that I made with Claude, and it works far better than I ever expected.

The crash analysis and the code were made with Claude (by Anthropic): the original fix with the Claude Opus 5.5 model, the updates up to 1.6.0 with the newer Claude Fable 5.2 model, and 1.6.1 and 1.7.0 with Claude Opus 5.5. Everything was tested over many hours of play before publishing.

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
