# W3CrashFix

Fixes the frequent crashes of **The Witcher 3: Wild Hunt Remastered** (**DX12**) on PC.

It patches a bug in the game's code, in memory only, while the game runs. No game files are changed.

**Also a diagnostic tool for the game's other crashes.** Whatever crashes the game, W3CrashFix saves a crash report on your PC: where the game crashed and how it got there, which files were involved and their versions, which mods and add-ons were loaded, the state of the graphics card, and what happened just before (a loading screen, an alt-tab). If you send it in, it helps find the causes of those crashes too, and the ones found so far are recognised on the report page with tips. Nothing is ever sent by the mod itself.

**Steam and GOG versions.** The fix was made and tested on the Steam version. On the GOG version it switches itself on too, and players' sessions of over an hour on GOG ran with the fix active. On the Epic version it is not confirmed: an older version of the mod turned itself off there. Since 1.8.0 the mod also finds the game's code when it sits at a different place, so it may work now. Any other store version is untested.

**Playing on Epic? Your log can settle it.** Start the game once with the mod installed, then quit. Open `W3CrashFix-send-log.html` (next to the mod, in `bin\x64_dx12`) and click the send button. The log shows whether the fix switched on, also if your game never crashes. Nothing is sent unless you click the button.

| Bug | Symptom |
|---|---|
| The DX12 renderer leaks shader-visible descriptor heaps. After about 4,000 heaps, `CreateDescriptorHeap` fails with `E_OUTOFMEMORY`. The game doesn't check for this and uses a NULL pointer. | Crash to desktop after playing for a while. Access violation at `witcher3.exe+0x280ad3d`. |

**Video (3 minutes):** [how the crash works, and the fix](https://youtu.be/0h5bh2ZFebU).

**Results on the author's PC (see "Tested with" below for the full setup):**
- **Before:** over 30 crashes, all at `+0x280ad3d`.
- **With the fix:** the live heap count stays flat at 7–9 heaps instead of climbing to about 4,000. Many hours of play, including the spots that used to crash every time, heavy fast travel and combat: no crashes.

### Known issue: "Gpu Crash for unknown reasons"

We know about this crash and we're working to track it down. It is a different crash from the one W3CrashFix fixes: the game stops itself with "Gpu Crash for unknown reasons" (at `witcher3.exe+0x1F103A3`). Thanks to the crash logs you sent, we know it happens inside the game's DX12 renderer when the graphics card drops out, also on a game without any mods. Six players have reported it so far, three of them on graphics cards with 6 GB of memory. The cause is not known, and W3CrashFix can't prevent this crash. W3CrashFix is highly unlikely to cause it: where a report had a call stack, W3CrashFix's own code wasn't in it, and in every report its fix had done little or nothing in that session (0 to 2 pages rescued in the whole session).

Things that may help:
- Update your graphics driver.
- Remove any overclock or undervolt of your graphics card.
- On a card with 8 GB of memory or less, lower texture quality and ray tracing.

If you get this crash, please send your crash log with the button on the report page. Every log helps.

If the cause turns out to be the graphics driver or the graphics card itself, a mod can't fix it. Then the logs can still show what triggers it, so it can be avoided or reported to the right people.

### What W3CrashFix can't fix

W3CrashFix fixes one bug in the game's code: the descriptor heap leak. Your crash logs help to understand other crashes too, but many of those are out of reach for a mod like this:

- **Graphics driver and hardware problems.** The graphics card dropping out because of the driver, an overclock or undervolt, heat or power. A mod can't repair a driver or a card.
- **Crashes caused by other mods.** A conflict between mods, an outdated script extender, or a mod that doesn't fit this game version. The crash log can point to the mod involved, but the fix belongs in that mod, or in removing it.
- **Running out of memory.** When the graphics card or the PC simply has too little memory for the chosen settings and mods, lower settings help; a mod can't add memory.
- **Freezes and hangs.** When the game stops responding without crashing, W3CrashFix sees no crash and writes no crash log.
- **Other bugs in the game.** Each one would need its own careful patch, and many can't be patched safely from outside the game. Those are for CD PROJEKT RED to fix.

## Requirements

- The Witcher 3: Wild Hunt Remastered **5.0.16.41566** (`witcher3.exe` in `bin\x64_dx12`), **Steam or GOG version**. On the Epic version it may work, but that is not confirmed; see the top of this page.

**Version numbers** have the form `<mod version>-<game version>`. For example, `2.1.0-5.0.16` is mod version 2.1.0 for game patch 5.0.16. A release for a newer game patch will have a different game part.
- **DX12 mode only.** The DX11 executable (`bin\x64`) is not patched.

**Safe on other versions and after game updates.** Before patching anything, the mod checks every function it uses, from the first byte to the last. If a game update only moves that code to other places, the mod finds it there and the fix keeps working without a new version: it switches on only when each function turns up exactly once with exactly the same code, and the functions call each other as they should. If the code itself is different (another store version, or an update that changed it), the fix turns itself off and logs `FIX DISABLED`. It never patches code it doesn't recognise.

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

**A crash report names your mods.** In-game mods are the ones the game itself loads: the folders in the game's `Mods` folder, and add-ons in its `DLC` folder. The mod notes their folder names at the start of its log, and a crash report lists them, so that whoever reads the report can rule them in or out. Nothing is switched off because of a mod. Since 2.1.0 the log and the crash report also list the other DLL and ASI files loaded from the game folder that are not the game's own (an ASI loader, ReShade, RenoDX, DLSS add-ons, a script extender, other ASI plugins), with their versions.

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

**Scanned before every release.** Every release file is uploaded to [VirusTotal](https://www.virustotal.com), which checks it with about 70 virus scanners, and the results are in the release notes on GitHub. An installer that isn't code-signed is sometimes flagged by one or two lesser-known scanners; the full source code is here, so you can check what it does.

ReShade (`dxgi.dll`), RenoDX and frame generation / DLSS mods were all in use during testing. See also [Other mods](#other-mods).

### Check that it works

Start the game, accept the license if the window appears (see below), and load a save. Then open the mod's activity log, `<game folder>\bin\x64_dx12\W3CrashFix.activity.log`. It should contain:

```
==== W3CrashFix 2.1.0-5.0.16 (...) loaded, pid ... ====
fix: descriptor page recycling fix ACTIVE (w3+0x2809760 detoured)
hooks installed: yes (...)
```

Every 30 seconds the mod adds a status line to the log, including live descriptor-heap counts. The previous session's log is kept as `W3CrashFix.activity.previous.log`.

### Settings

The mod keeps its few settings in one small file next to itself, `W3CrashFix.ini`, which you can open with Notepad. Since 2.1.0 the file itself explains every setting, in a short note above its line.

```ini
[W3CrashFix]
LicenseAccepted=MIT-2
ReportId=0123456789ABCDEF
OpenReportPage=1
GpuCrashDetails=0
CrashDump=0
```

- `LicenseAccepted`: the license text you accepted. See [The license window](#the-license-window).
- `ReportId`: a random number, used only in crash reports. See [If the game crashes](#if-the-game-crashes).
- `OpenReportPage`: set it to `0` if nothing should open after a crash. The crash log is still saved.
- `GpuCrashDetails`: set it to `1` if your game crashes with "Gpu Crash for unknown reasons". The crash log then also says which work your graphics card had not finished, and where on the card a bad memory access happened, if any. In a test on the author's PC it had no measurable effect on the frame rate; on other PCs it may cost a little. It is off by default. It takes effect at the next game start. Every crash log says whether the graphics card was still working and, if not, why Windows stopped it, also with this setting off.
- `CrashDump`: set it to `1` to also save a crash dump, `W3CrashFix.crash.dmp`, next to the crash log. A dump shows what the game was working on when it crashed, much more than the log can; the leak this mod fixes was found in dumps. It can contain bits of the game's memory, such as file paths with your Windows user name, so it is off by default and you choose whether to send it. Expect a few MB up to a few tens of MB.

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

**A crash report from W3CrashFix doesn't mean W3CrashFix caused the crash.** The mod notices every crash of the game, also crashes it has nothing to do with, and saves a report about it. The report's verdict says which crash it was, and whether there is any sign that W3CrashFix was involved: it checks the crash address and the whole call stack for W3CrashFix's own code, and says whether its fix did anything in that session. When a mod, script extender, overlay or other add-on is in the call stack, the verdict names it, because it may be involved. In the crash reports sent so far, W3CrashFix's code was never part of the crash.

The mod notices a crash and helps you report it:

- It writes what crashed (file and offset) into its activity log.
- It saves the crashed session as `W3CrashFix.crash.log`, in a folder of its own next to the mod: `W3CrashFix crash report`. The file starts with a short summary: the date and time of the crash, mod version, a random report id, the in-game mods that were enabled and the other DLL and ASI files that were loaded, whether the fix was active, the crash address with the call stack and the processor's registers, the graphics card with its driver and state, the game's graphics settings, heap counts, memory, and a verdict that says whether this is the crash W3CrashFix fixes. Only a newer crash overwrites that file. The report page shows the date and time of the crash too; opened again more than an hour later, it says it belongs to an older crash, so an old log isn't mistaken for a new one.
- It opens a small folder window on that folder, in the lower right corner of your screen, and then a report page, `W3CrashFix-report.html`, in your browser. The page shows the summary and has one button, **Send the crash log**.

**Sending a report.** The button opens an upload page of the author's online storage (pCloud) and copies the crash log's path at the same time. On that page, click **Click Here to Add Files**, press Ctrl+V in the window that opens, and then Enter. Or drag the crash log from the small folder window onto the page; that window asks Windows to keep it on top. Then type any name you like (a made-up one is fine) and click Upload. You don't need an account. Sent the wrong file by mistake? No problem: anything that isn't a W3CrashFix log or crash dump is deleted. You can just send the right file afterwards.

**The report page is a file on your own PC, not a website, and the mod itself never connects to the internet.** Only your browser does, and only when you click the button. Nothing leaves your PC unless you upload the file yourself. The reason the report is shown in your browser is explained at the bottom of that page.

**What a report contains.** The crash log holds the summary and the mod's activity log of the crashed session: technical data. It can include the folder the game is installed in, the folder names of your in-game mods and the file names of other DLL and ASI mods in the game folder (see [Other mods](#other-mods)). Only the author can see uploaded files. A report sent this way can't be answered; for questions, use the Posts tab on Nexus Mods or the issues here on GitHub.

**The report id** is a random number made on your PC the first time the mod works. Its only use is that several reports from the same machine/installation can be recognised as such. It is not derived from your hardware, Windows or your name, and it says nothing about you. It is kept as `ReportId` in `W3CrashFix.ini`; remove that line to get a new one.

**Crashes with another cause are welcome too.** The crash log is made for every crash, not only for the one W3CrashFix fixes. Such reports might be analysed in an attempt to provide a fix. That is not a promise.

**Your report is always welcome, also without a test.** It helps even more if you also check whether the crash happens on a clean game: move your other mods (in-game mods, ASI and DLL mods, ReShade) out of the game folder for a while, keep W3CrashFix, and play the same part again. If it still crashes, send that crash log too; the log shows by itself which mods were there. A crash on a clean game points at the game itself; a crash that only happens with mods points at one of them. Logs of both, with and without mods, from the same PC are especially helpful: the report id shows they belong together, and the two can be compared side by side.

**No crash? Your log is welcome too.** A session without a crash, with or without other mods, shows how the game behaves when all goes well, and can be compared with sessions that crashed. To send it, open `W3CrashFix-send-log.html` in the game's `bin\x64_dx12` folder (the mod writes it at every game start). It has the same **Send** button as the report page, for `W3CrashFix.activity.log`. Send it after you close the game and before you start it again; at the next start the log becomes `W3CrashFix.activity.previous.log`, which you can send instead. Logs from the **Epic version** are especially helpful: it isn't confirmed that the fix switches on there, and the log shows it. Long sessions on the **GOG version** help too, since there is no long-term result from GOG yet.

The game's own crash reporter may also appear after a crash. That one reports to the game's developers; the W3CrashFix page is only about this mod. You can use both.

If you don't want anything to open after a crash, set `OpenReportPage=0` in `W3CrashFix.ini` (see [Settings](#settings)). The crash log is still saved.

### Uninstall

Use **Uninstall** in the installer, or delete `dinput8.dll` (standalone) or `W3CrashFix.asi` (ASI) from `bin\x64_dx12`. You can also delete `W3CrashFix.ini` and, if they exist, `W3CrashFix.removed`, `W3CrashFix-report.html`, `W3CrashFix-send-log.html`, `W3CrashFix.activity.log`, `W3CrashFix.activity.previous.log` and the folder `W3CrashFix crash report`. The installer's Uninstall removes all of these, so no traces of the mod are left.

## Technical details

### The descriptor page leak

The DX12 renderer hands out shader-visible CBV/SRV/UAV descriptors from 1024-entry "pages". Each page is its own `ID3D12DescriptorHeap`. The allocator works like this:

- When the current page is full, the function at `+0x2809760` moves it to a *retired* list and takes a page from the *free* list. If the free list is empty, it creates a new heap.
- Pages only move back from *retired* to *free* when a GPU-fence entry for them completes while they are no longer the current page.

The bug is in the case where the GPU keeps up with the CPU. A page's last fence entry often completes while that page is still the current page. When it's retired later, it has `pending == 0` and `usesThisFrame == 0`. It never gets another fence entry, so it stays in the retired list forever.

About 1 in 5 retirements is lost this way. That's roughly 4 new heaps per second, until the driver refuses to create more. `+0x2809760` then stores a NULL page without checking, and the next allocation dereferences it at `+0x280ad3d`.

**The fix** replaces `+0x2809760` with an identical version that has one change: a retired page that is already idle goes straight to the free list. "Idle" uses the game's own definition: no uses this frame, no pending GPU work. All other paths (in-flight pages, fence recycling, cached-table invalidation, heap creation) call the game's original code.

## Changelog

The name after a line says whose idea it was: Havoc's, Claude's, or something a player's log showed.

### 2.1.0-5.0.16

A crash report now shows by itself what else was running in the game, so you no longer have to tell us. So far twenty-six crash reports from sixteen players have arrived; thank you to everyone who sent one.

- Crash report and activity log: the **other DLL and ASI files in the game folder** that are not the game's own, with their versions: an ASI loader, ReShade, a script extender, ASI mods and so on. In-game mods were already listed. - Claude
- Crash report: when a file that is **not part of the game or Windows** is in the call stack (a mod, script extender, overlay or other add-on), the verdict names it, because it may be involved. - Havoc
- Report page and send page: no more requests to put "clean" or "no crash" in the name of your upload. The log shows it. - Havoc
- `W3CrashFix.ini`: **every setting has a short explanation** above its line. They are added at the next game start; your values stay as they are. - Havoc
- Activity log: "game settings changed" is only written when a setting really changed. On some PCs it was written every 30 seconds. - Claude, from a player's log
- The descriptor page fix is unchanged.

**Do I need to update?** The fix itself is the same. If you send crash logs, 2.1.0's logs tell more about what else was loaded, so it's recommended; otherwise there is no hurry.

### 2.0.0-5.0.16

W3CrashFix is now also a **diagnostic tool for the game's other crashes**. Thanks to everyone who sent a crash log: twenty-one reports from thirteen players have come in since 1.7.0, and they showed what a crash report needs to say. They also showed a second crash that six players run into, "Gpu Crash for unknown reasons" (see [Known issue](#known-issue-gpu-crash-for-unknown-reasons)).

- Crash report: **the state of the graphics card.** Every report says whether the graphics card was still working and, if not, why Windows stopped it. - Claude
- New setting **`GpuCrashDetails`** (off by default): with `1`, a GPU crash report also says which work the graphics card had not finished and where a bad memory access happened. See [Settings](#settings). - Claude
- New setting **`CrashDump`** (off by default): with `1`, a crash also saves a crash dump next to the crash log. The report page lists it as an optional second file to send. - Claude
- Crash report: the **graphics driver version**, the game's **graphics settings** (ray tracing, path tracing, upscaler, frame generation, resolution), the **files in the call stack with their versions**, and **what happened just before** the crash (a loading screen, an alt-tab). - Claude
- Crash report: a crash at an empty address now has a call stack too. - Claude
- Crash report: the verdict says whether there is **any sign that W3CrashFix was involved**: it checks the crash address and the whole call stack for W3CrashFix's own code. - Havoc
- Report page: **known crashes** are recognised, with tips that may help. The first one is the GPU crash. - Claude
- Report page: the Send button always stays in view, and a wrongly sent file is no problem. - Havoc
- `W3CrashFix.ini`: a tip for players with more than one installation of the game. - Havoc
- Installer: Uninstall also removes the crash dump. - Claude
- The descriptor page fix is unchanged.

**Why 2.0.0?** Confession time: we broke our own versioning rule. W3CrashFix 1.0.0 was just the fix. After 1.2.0 I set a rule: the first digit changes when the mod changes in a big way. Then 1.4.1 added crash reports, the versions after it added much more, and the first digit stayed a 1. It should have become a 2 back then, and neither of us noticed: not me, and not Claude, who proposes the version numbers. The published versions keep their numbers, so nothing gets confusing; this release, which makes W3CrashFix a diagnostic tool, is where the 2 finally lands.

**Do I need to update?** The fix itself is the same. If your game crashes in other ways, 2.0.0's crash reports tell much more about why, so it's recommended.

### 1.8.1-5.0.16

A small correction release: internal corrections in the source and the settings file's note. The fix and everything you see are unchanged.

**Do I need to update?** No, 1.8.0 works just as well. If you update anyway, nothing changes for you.

### 1.8.0-5.0.16

Thanks to everyone who sent a crash log. Twelve logs from nine players have come in so far, and they shaped this release. None of them was the crash this fix is made for: in every one the fix was active and the heaps stayed low. One of them was a 97-minute session on the GOG version.

- Crash report: the **call stack has no limit** any more. Until now it stopped after 16 places; in one report a script extender showed up at place 13, and what led to it was cut off. - Havoc
- Crash report: the **date and time of the crash** are at the top of the crash log and on the report page. If you open the report page again later, it says that it belongs to an older crash. One player sent the same log twice, and there was no way to tell. - Claude, after a player's upload
- New: **send your log without a crash.** The mod writes `W3CrashFix-send-log.html` next to itself at every game start. Open it, and the same Send button uploads your `W3CrashFix.activity.log`. Every log is welcome, with or without other mods; logs from the **Epic version** are especially helpful. See [If the game crashes](#if-the-game-crashes). - Havoc
- Report page: every crash log is welcome, also without a test. It helps even more if you also try the same part on a clean game, without your other mods. - Havoc
- **Ready for game patches that only move code.** The mod now checks every function it uses from the first byte to the last. If a game update only moves them to other places, it finds them there and the fix keeps working without a new version. If the code itself changed, the fix turns itself off as before. - Claude
- Installer: Uninstall also removes the send page. - Claude
- The descriptor page fix is unchanged.

**Do I need to update?** It's recommended. The fix itself is the same, but 1.8.0 keeps working after a game patch that only moves code, and its crash reports tell more.

And no, we're not playing GTA 6. We've got better things to do. 😉

### 1.7.0-5.0.16

Thanks to everyone who sent a crash log. The first logs that came in through the report page already led to this release: they showed where the crash report could tell more. So far seven crash reports from six players have arrived. None of them was the crash this fix is made for, and every one of them helped.

- Crash report: it now lists **all** your in-game mods, with their number. Before, a long list was cut off after about 600 characters, so with many mods it didn't show which ones were enabled. - Havoc
- Crash report: it now also shows the **call stack**, a short list of places in the game's code that led to the crash. Two players' crashes stopped at the same place, where the game halts itself when something goes wrong; only the code that led there can show what went wrong. - Havoc
- Crash report: the verdict also recognises the descriptor heap crash when it shows up one step deeper in the game's code. - Claude
- You can now **personalise your report id**. `W3CrashFix.ini` explains how, right above the `ReportId` line: add your own text before or after the id, for example `MyName_0123456789ABCDEF`, and keep the 16 characters of the id itself, so your new crash logs can still be matched with the ones you sent before. Before this release, the mod replaced any id it didn't make itself with a new random one. - Havoc
- Installer: **Uninstall leaves no traces**: it now also removes the activity logs. - Havoc
- After a crash, the small folder window gets a little more time and up to four requests to stay on top of the browser. Windows can still refuse it, but less often. - Havoc
- Clearer wording about the standalone version: only one `dinput8.dll` can be in the folder. - Havoc
- The descriptor page fix is unchanged.

**Do I need to update?** The fix itself is the same, so if your game runs fine, you can keep your version. But if you ever send a crash log, a report from 1.7.0 tells much more. Every log counts, also when it isn't the crash this mod fixes: send it with the button on the report page.

**Video:** [how the crash works, and the fix](https://youtu.be/0h5bh2ZFebU) (3 minutes).

### 1.6.1-5.0.16

- Crash report, sending: the **Send the crash log** button now also copies the crash log's path. On the upload page you click **Click Here to Add Files**, paste with Ctrl+V and press Enter, so the folder window is no longer needed. In 1.6.0 that window went behind the browser as soon as you clicked there. Dragging still works. - Claude
- The small folder window now asks Windows to keep it on top of other windows. Windows doesn't always allow that; the activity log says whether it did. - Havoc
- The report page is shorter: it shows only whether the fix was active and the verdict. The full summary stays at the top of the crash log, which is the file you send. - Havoc
- The descriptor page fix is unchanged.

**Do I need to update?** No, the fix itself is the same. 1.6.1 only makes sending a crash report easier.

### 1.6.0-5.0.16

- Crash report, sending: the report page now has one button, **Send the crash log**. It opens an upload page of the author's online storage, where you drop the crash log; no account is needed and only the author sees the file. It replaces the two buttons for GitHub and Nexus Mods. The mod itself still never connects to the internet. See [If the game crashes](#if-the-game-crashes). - Havoc and Claude
- Crash report, easier to use: the crash log now has a folder of its own, `W3CrashFix crash report`, and after a crash a small folder window opens on it, so the file is ready to drag. - Havoc
- Crash report, more useful: the summary now also shows the processor's registers, the memory situation, whether the game build is supported, the in-game mods that were enabled, and a random report id that lets several reports from one installation be recognised. - Havoc and Claude
- Settings: one readable file, `W3CrashFix.ini`, replaces the small files `W3CrashFix.accepted` and `W3CrashFix.noreport`. Existing ones are taken over automatically. See [Settings](#settings). - Havoc
- The mod's log is now called `W3CrashFix.activity.log`, so it can't be mixed up with the crash log. Logs under the old name are tidied up at the first start. - Havoc
- License: the copyright line now names one copyright holder, and the license has a new section, "Additional permission", which lets CD PROJEKT RED S.A. use the fix without the usual notices. See [LICENSE](LICENSE). Because the license text changed, the license window appears once more after updating, and says why. - Havoc
- Installer: it now has an icon, is called installer/uninstaller, shows the license in a window that scrolls, names Ultimate ASI Loader as the one supported loader, and on Uninstall also removes the settings file and what a crash report left. - Havoc
- The descriptor page fix is unchanged.

**Do I need to update?** No. If an earlier version works for you, you can keep it: the fix itself is the same.

Updating does help the project, though. If you ever run into a crash, the newest version is the one built to analyse it: it records more about what crashed and makes sending the report a matter of one drag. A report from the newest version is the most useful one I can get.

### 1.5.0-5.0.16

- New: a license window at the first game start. It shows the MIT License and asks you to accept it before the fix is switched on; declining removes the mod from the game. See [The license window](#the-license-window). - Havoc
- Installer: the license is shown on Install, a new **Run game** button, and Uninstall now also removes the files the license window leaves behind. - Havoc and Claude
- The descriptor page fix is unchanged.

### 1.4.1-5.0.16

- New: crash report. When the game crashes, the mod writes what crashed into the log, saves the session as `W3CrashFix.crash.log` with a summary and a verdict, and opens a local page with two buttons to report it (GitHub or Nexus Mods). The mod itself sends nothing. See [If the game crashes](#if-the-game-crashes). - Havoc and Claude
- The descriptor page fix is unchanged.

### 1.3.0-5.0.16

- The GOG version is now listed as supported: a user's log shows the fix switches itself on there, at the same addresses as on Steam. There is no long-term test result from GOG yet. - from a player's log
- In the log, NVIDIA's downloaded DLSS files now show their feature folder, for example `dlssg\160_E658700.bin` instead of `160_E658700.bin`. Several of those files share one file name, so the name alone didn't say which one created a heap. - Claude
- The descriptor page fix is unchanged.

### 1.2.0-5.0.16

- The log now names the file that created each descriptor heap, for example `caller=witcher3.exe+0x2809F44` instead of a bare address. The first time a file shows up, its full path is logged once (a path inside your user profile is written as `%USERPROFILE%`). This shows whether heaps come from the game itself or from another DLL, such as a DLSS add-on. - Claude
- On a game build the mod doesn't recognise (another store version or a game update), the log now gets `diag:` lines that describe where the game's code differs. Posting that log is what is needed to add support for that build. As before, nothing is patched in that case. - Claude
- Added a "Tested with" section that names the DLSS 5 setup used during testing. - Havoc
- The descriptor page fix is unchanged.

### 1.1.0-5.0.16

- Removed "fix 2", the guard against invalid handle IDs after fast travel. It was added after two crashes at `witcher3.exe+0x1eecf31` and `+0x1eece9d` on the author's PC. In all play sessions since then, the guard never had to block a single lookup, and a test session with heavy fast travel and without the guard didn't crash. There is no sign that those two crashes came from a bug in the game itself, so the mod no longer touches that code. - Havoc
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

**Does an fps cap help?** It delays the crash but doesn't stop it. The leak grows with every frame drawn, so fewer frames per second only means it takes longer to run out. W3CrashFix stops the leak itself, so you can play uncapped.

**Can this get me banned?** The Witcher 3: Wild Hunt Remastered is single-player and has no anti-cheat. The mod uses the same loading method as many popular Witcher 3 mods.

**Will CD Projekt RED fix this?** Hopefully. Once the game is updated with a fix, this mod disables itself automatically.

## About the author

> I'm Havoc, just a guy trying out vibe coding for the first time: I describe what I want, and an AI writes the code. W3CrashFix is a side project that I made with Claude, and it works far better than I ever expected.

The crash analysis and the code were made with Claude (by Anthropic): the original fix with the Claude Opus 5.5 model, the updates up to 1.6.0 with the newer Claude Fable 5.2 model, and 1.6.1 to 2.1.0 with Claude Opus 5.5. Everything was tested over many hours of play before publishing.

**Who did what.** The bug was only found because I let Claude analyse my crash dumps, and the technical explanations in this README were drafted by Claude. Since version 1.0.0, most changes were my ideas and every decision was mine; Claude worked out the details and wrote the code. For example, I wanted the mod to open the crash log after a crash and let players send it to me; showing that in a single browser page was Claude's idea, so a crashing game opens one window instead of several.

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
