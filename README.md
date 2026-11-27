# W3CrashFix

A crash reporter for **The Witcher 3: Wild Hunt Remastered** (**DX12**) on PC.

When the game crashes, W3CrashFix saves a report on your PC that says what crashed and, where it can tell, why: where the game crashed and how it got there, which files were involved and their versions, which mods and add-ons were loaded, the state of the graphics card, and what happened just before (a loading screen, an alt-tab). The report's verdict tells you in plain words when another mod is the likely cause, and a graphics card that dropped out is recognised and explained, with tips. You can send the report in with one button. Nothing is ever sent by the mod itself.

It changes no game files and no game code.

### Since 3.0.0: no longer a fix, because the game has it

W3CrashFix started as a fix for one crash: the game's DX12 renderer leaked descriptor heaps until the graphics driver refused to create more, and the game crashed after playing for a while (access violation at `witcher3.exe+0x280ad3d` in game version 5.01c).

**Game patch 5.02 (26 November 2026) fixes that leak in the game itself.** The patch changes the very code W3CrashFix used to repair: the game now reuses an idle descriptor page instead of losing it, which is what the mod's fix did. CD PROJEKT RED's patch notes don't mention it. We found it by reading the game's code, and a first long session confirms it: 76 minutes on 5.02 with path tracing on and without W3CrashFix's fix, and the number of live descriptor heaps stayed between 4 and 10 the whole time. Before 5.02 it climbed by about 4 per second towards 4,000.

So the fix is obsolete, and from 3.0.0 on W3CrashFix no longer contains it. What is left is the part that grew around it: the crash reporter.

- **On game version 5.02 or newer:** this is the version for you.
- **On an older game version:** please update the game. W3CrashFix 3.0.0 does not fix the descriptor heap crash, and its log, its crash report and the installer say so. If you can't update, [2.3.0](https://github.com/Havoc104/W3CrashFix/releases/tag/v2.3.0-5.0.16) is the last version with the fix.
- **We only look at crash reports from game version 5.02 or newer, made by W3CrashFix 3.0.0 or newer.** Logs and crash dumps from older versions of the game or of W3CrashFix are no longer accepted: they are deleted without being analysed. If you're on an older version, please update first.

**Video (3 minutes):** [how the crash worked, and the fix](https://youtu.be/0h5bh2ZFebU).

### Known issue: "Gpu Crash for unknown reasons"

The graphics card drops out, and the game stops itself with "Gpu Crash for unknown reasons". The crash logs players sent showed that it happens on games with and without mods, on eight players' PCs, four of them with a 6 GB graphics card. It is a matter of the graphics driver or the hardware. A mod can't prevent it, so we no longer look into it. W3CrashFix does not cause it: where a report had a call stack, W3CrashFix's own code wasn't in it. The crash report recognises this crash on every game version and tells you why Windows stopped the graphics card.

Things that may help:
- Update your graphics driver.
- Remove any overclock or undervolt of your graphics card.
- On a card with 8 GB of memory or less, lower texture quality and ray tracing.
- Windows keeps its own record of such a drop-out: press Win+R, run `perfmon /rel` and look for a "Hardware error" at the time of the crash.

### What W3CrashFix can't do

W3CrashFix reports crashes. It does not fix them. A report can tell you where to look:

- **Graphics driver and hardware problems.** The graphics card dropping out because of the driver, an overclock or undervolt, heat or power. A mod can't repair a driver or a card.
- **Crashes caused by other mods.** A conflict between mods, an outdated script extender, or a mod that doesn't fit this game version. The report says so when another mod is the likely cause, and names the file when it is in the call stack. The fix belongs in that mod, or in removing it; its author is the one to ask.
- **Running out of memory.** When the graphics card or the PC simply has too little memory for the chosen settings and mods, lower settings help; a mod can't add memory.
- **Freezes and hangs.** When the game stops responding without crashing, W3CrashFix sees no crash and writes no crash log.
- **Bugs in the game.** Those are for CD PROJEKT RED to fix.

## Requirements

- The Witcher 3: Wild Hunt Remastered, **game version 5.02 or newer** (`witcher3.exe` in `bin\x64_dx12`). It runs on older versions too, but see above.
- **DX12 mode only.** The DX11 executable (`bin\x64`) is not covered.
- **Any store version.** Since 3.0.0 the mod no longer looks for particular game code, so it no longer depends on the store. It was made and tested on the Steam version.

**Version numbers** have the form `<mod version>-<game version>`. The game part is the game version the release was tested with: `3.0.1-5.0.17` is mod version 3.0.1, tested with game version 5.0.17 (5.02 Steam).

### Tested with

- The Witcher 3: Wild Hunt Remastered, Steam version, 5.0.17 (5.02 Steam), DirectX 12.
- Ryzen 9 9950X3D, 64 GB RAM, RTX 5090 (32 GB), Windows 11, NVIDIA drivers 32.0.16.2114 and 32.0.16.2135.
- 4K, borderless window, HDR, VSync on, 60 fps limit (applies to the base frame rate).
- All graphics settings at maximum, path tracing on, NVIDIA HairWorks on.
- DLSS 5 in Quality mode with Ray Reconstruction and dynamic frame generation, through the RenoDX DLSS add-on by ShortFuse (build 2026-10-16) with ReShade 6.8.0, in DLSS-NR mode, with `nvngx_dlssnr.dll` 310.8.SF. Installed with DLSS5oneclick.
- Other mods: Improved Horse Controls.

Other ways of installing DLSS 5 (for example DLSS Manager) are untested.

### Other mods

W3CrashFix changes no game files, no scripts and no game code, so it should work alongside other mods. That can't be promised for every mod: only what is listed under "Tested with" has actually been played with.

**A crash report names your mods.** In-game mods are the ones the game itself loads: the folders in the game's `Mods` folder, and add-ons in its `DLC` folder. The mod notes their folder names at the start of its log, and a crash report lists them. Nothing is switched off because of a mod. The log and the crash report also list the other DLL and ASI files loaded from the game folder that are not the game's own (an ASI loader, ReShade, RenoDX, DLSS add-ons, a script extender, other ASI plugins), with their versions.

**When another mod is the likely cause, the report says so.** If a file that is not part of the game or Windows is in the call stack of the crash, the verdict names it and says the crash is most likely caused by that mod or add-on. If the game runs mods but none of them is in the call stack, the verdict says another mod is a likely cause and gives the numbers. In both cases the author of that mod is the one to ask. We don't investigate crashes of modded games.

## Installation

Installing by hand is always recommended: it's one file, and you can see exactly what goes where. Choose **one** of the two versions. The ASI version is recommended.

### A. ASI plugin (`W3CrashFix.asi`) - recommended

It works alongside your other ASI mods.

1. Install [Ultimate ASI Loader](https://github.com/ThirteenAG/Ultimate-ASI-Loader/releases/latest) (x64) in `<game folder>\bin\x64_dx12\`, saved as `dinput8.dll`. Skip this if you already have it. W3CrashFix is made for and tested with Ultimate ASI Loader only. You are free to try another ASI loader, but that is untested and not supported.
2. Copy `bin\x64_dx12\W3CrashFix.asi` from the zip into the same folder.

### B. Standalone (`dinput8.dll`)

Use this if you don't want to install Ultimate ASI Loader. The folder can hold only one `dinput8.dll`: if another mod already uses `dinput8.dll` (Ultimate ASI Loader, for example), this one replaces it and that mod stops working, and a mod that puts its own `dinput8.dll` there later switches W3CrashFix off in the same way. If another mod uses `dinput8.dll`, choose the ASI version.

1. Copy `bin\x64_dx12\dinput8.dll` from the zip into `<game folder>\bin\x64_dx12\`.

If you accidentally install both versions, only the one that loads first does the work. The other one does nothing.

### Optional: the installer/uninstaller

A small program that installs either version for you and can remove it again. The installer only exists for ease of use; manual installation is still recommended.

1. Download `W3CrashFix-<version>-Installer.exe` from the [latest release](https://github.com/Havoc104/W3CrashFix/releases/latest) on GitHub and run it. The installer is not offered on Nexus Mods.
2. It looks for the game in Steam, GOG and Epic. If it doesn't find it, click **Browse** and select the game folder.
3. Choose a version and click **Install**. The installer shows the license first; see [The license window](#the-license-window). You can use **Uninstall** later to remove the mod.
4. **Run game** starts the game once W3CrashFix is installed. A Steam copy is started through Steam. If the ASI version is installed without Ultimate ASI Loader, the installer first reminds you that other ASI loaders are untested and not supported.

**Uninstall** leaves a clean folder: besides the mod's own file it removes `W3CrashFix.ini` (the mod's settings: your license acceptance and the report id) and `W3CrashFix.removed` (left after declining), as well as the crash log with its folder, the report page and the activity logs. It leaves no traces of the mod behind.

The installer shows your game version, and says so when it is older than patch 5.02. It won't overwrite another mod's `dinput8.dll`. For the ASI version, it checks that Ultimate ASI Loader is installed. If it isn't found, it links you to it. Ultimate ASI Loader is a separate project and isn't included. Without it the installer warns you and asks before it installs the ASI version, so you can still try another ASI loader at your own risk.

**Windows will show a warning** ("Windows protected your PC") when you start the installer. That's normal: the installer isn't code-signed, because a signing certificate costs money. It is safe to run. It only copies the W3CrashFix file into your game folder, and its full source code is in this repository (`src\installer.c`). Click **More info**, then **Run anyway**.

**Scanned before every release.** Every release file is uploaded to [VirusTotal](https://www.virustotal.com), which checks it with about 70 virus scanners, and the results are in the release notes on GitHub. An installer that isn't code-signed is sometimes flagged by one or two lesser-known scanners; the full source code is here, so you can check what it does.

### Check that it works

Start the game and accept the license if the window appears (see below). Then open the mod's activity log, `<game folder>\bin\x64_dx12\W3CrashFix.activity.log`. It should contain:

```
==== W3CrashFix 3.0.1-5.0.17 (...) loaded, pid ... ====
game: exe timestamp ... (patch 5.02 or newer)
hooks installed: yes (...)
```

Every 30 seconds the mod adds a status line to the log, including live descriptor-heap counts. The previous session's log is kept as `W3CrashFix.activity.previous.log`.

### Settings

The mod keeps its few settings in one small file next to itself, `W3CrashFix.ini`, which you can open with Notepad. The file itself explains every setting, in a short note above its line.

```ini
[W3CrashFix]
LicenseAccepted=GPL-3
ReportId=0123456789ABCDEF
OpenReportPage=1
GpuCrashDetails=0
CrashDump=0
DefaultsSet=3
```

- `LicenseAccepted`: the license text you accepted. See [The license window](#the-license-window).
- `ReportId`: a random number, used only in crash reports. See [If the game crashes](#if-the-game-crashes).
- `OpenReportPage`: set it to `0` if nothing should open after a crash. The crash log is still saved.
- `GpuCrashDetails`: set it to `1` if you want more detail after a "Gpu Crash for unknown reasons": the crash log then also says which work your graphics card had not finished, and where on the card a bad memory access happened, if any. It is off by default and may cost a little frame rate on some PCs. A change takes effect at the next game start. Every crash log says whether the graphics card was still working and, if not, why Windows stopped it, also with this setting off.
- `DefaultsSet`: written by the mod to remember which default settings your file has already been given. Leave it as it is.
- `CrashDump`: set it to `1` to also save a crash dump, `W3CrashFix.crash.dmp`, next to the crash log. A dump shows what the game was working on when it crashed, much more than the log can. It can contain bits of the game's memory, such as file paths with your Windows user name, so it is off by default and you choose whether to send it. Expect a few MB up to a few tens of MB.

### The license window

The first time you start the game with W3CrashFix, a small window appears before the game does. It shows the license under which the mod is published, the GNU General Public License (version 3 or later), and says in plain words that the mod comes without any warranty.

- **OK (Apply this mod)** accepts the license and switches the mod on. The window does not appear again unless the license itself changes; when it does, the window says so.
- To decline, tick **I decline. Remove W3CrashFix from this game.** The button then reads **OK (Remove this mod)**. The mod stays off and renames its own file to `W3CrashFix.removed`, so that it never loads again.
- **View source code** opens this repository in your browser.

The game waits until you have chosen. The window has no close button on purpose. The Enter key works as OK once the window has been open for three seconds, so you can also answer it without a mouse.

Until you accept, the mod does nothing at all: no crash report and no logging.

**Why is a file left behind after declining?** Windows does not allow a program to delete a file that is in use, and the game has the mod loaded for as long as it runs. Renaming is allowed, so that is what the mod does. It does not start a helper program to delete the file afterwards, because that is the kind of behaviour antivirus software is suspicious of. You can delete `W3CrashFix.removed` yourself, or let the installer's **Uninstall** do it.

If a mod manager (Vortex, for example) installed the mod for you, remove it in the mod manager instead of declining. A mod manager keeps track of the files it installed and may put the renamed file back.

If you use the installer, the license is shown there when you click **Install**, and the game starts without the window.

### If the game crashes

**A crash report from W3CrashFix doesn't mean W3CrashFix caused the crash.** The mod notices every crash of the game and saves a report about it. The report's verdict says whether there is any sign that W3CrashFix was involved: it checks the crash address and the whole call stack for W3CrashFix's own code. When a mod, script extender, overlay or other add-on is in the call stack, the verdict names it and says that it is the most likely cause. In the crash reports sent so far, W3CrashFix's code was never part of the crash.

The mod notices a crash and helps you report it:

- It writes what crashed (file and offset) into its activity log.
- It saves the crashed session as `W3CrashFix.crash.log`, in a folder of its own next to the mod: `W3CrashFix crash report`. The file starts with a short summary: the date and time of the crash, mod version, a random report id, the in-game mods that were enabled and the other DLL and ASI files that were loaded, whether the game is patch 5.02 or newer, the crash address with the call stack and the processor's registers, the graphics card with its driver and state, the game's graphics settings, heap counts, memory, and the verdict. Only a newer crash overwrites that file. The report page shows the date and time of the crash too; opened again more than an hour later, it says it belongs to an older crash, so an old log isn't mistaken for a new one.
- It opens a small folder window on that folder, in the lower right corner of your screen, and then a report page, `W3CrashFix-report.html`, in your browser. The page shows the verdict and has one button, **Send the crash log** (**Send the crash files** when a crash dump was saved too).

**Sending a report.** The button opens an upload page of the author's online storage (pCloud) and copies the crash log's path at the same time. On that page, click **Click Here to Add Files**, press Ctrl+V in the window that opens, and then Enter. Or drag the crash log from the small folder window onto the page; that window asks Windows to keep it on top. Then type any name you like (a made-up one is fine) and click Upload. You don't need an account. Sent the wrong file by mistake? No problem: anything that isn't a W3CrashFix log or crash dump is deleted. You can just send the right file afterwards.

**The report page is a file on your own PC, not a website, and the mod itself never connects to the internet.** Only your browser does, and only when you click the button. Nothing leaves your PC unless you upload the file yourself. The reason the report is shown in your browser is explained at the bottom of that page.

**What a report contains.** The crash log holds the summary and the mod's activity log of the crashed session: technical data. It can include the folder the game is installed in, the folder names of your in-game mods and the file names of other DLL and ASI mods in the game folder (see [Other mods](#other-mods)). Only the author can see uploaded files. A report sent this way can't be answered; for questions, use the Posts tab on Nexus Mods or the issues here on GitHub.

**The report id** is a random number made on your PC the first time the mod works. Its only use is that several reports from the same machine/installation can be recognised as such. It is not derived from your hardware, Windows or your name, and it says nothing about you. It is kept as `ReportId` in `W3CrashFix.ini`; remove that line to get a new one.

**Which reports we look at.** Reports from game version 5.02 or newer, made by W3CrashFix 3.0.0 or newer. Logs and crash dumps from older versions of the game or of W3CrashFix are no longer accepted: they are deleted without being analysed. A report from a game without other mods, or with only a few, tells the most: a crash that also happens on a clean game points at the game itself, and that is what we look for. A crash that only happens with mods points at one of them, and the report says so. Sending a report is not a promise that anything comes of it.

**No crash? Your log is welcome too.** A session without a crash shows how the game behaves when all goes well, and can be compared with sessions that crashed. To send it, open `W3CrashFix-send-log.html` in the game's `bin\x64_dx12` folder (the mod writes it at every game start). It has the same **Send** button as the report page, for `W3CrashFix.activity.log`. Send it after you close the game and before you start it again; at the next start the log becomes `W3CrashFix.activity.previous.log`, which you can send instead.

The game's own crash reporter may also appear after a crash. That one reports to the game's developers; the W3CrashFix page is only about this mod. You can use both.

If you don't want anything to open after a crash, set `OpenReportPage=0` in `W3CrashFix.ini` (see [Settings](#settings)). The crash log is still saved.

### Uninstall

Use **Uninstall** in the installer, or delete `dinput8.dll` (standalone) or `W3CrashFix.asi` (ASI) from `bin\x64_dx12`. You can also delete `W3CrashFix.ini` and, if they exist, `W3CrashFix.removed`, `W3CrashFix-report.html`, `W3CrashFix-send-log.html`, `W3CrashFix.activity.log`, `W3CrashFix.activity.previous.log` and the folder `W3CrashFix crash report`. The installer's Uninstall removes all of these, so no traces of the mod are left.

## Technical details

### How the crash reporter works

The mod is loaded into the game as `dinput8.dll` or as an ASI plugin. It registers a crash handler, and it hooks the D3D12 calls that create and release descriptor heaps, so that its log can show heap counts and which file created a heap. When the game crashes, the handler writes down the exception, the registers and the call stack, asks D3D12 whether the graphics device is still there (and why not, if it is gone), and saves the report. To name the places in a call stack it reads the game's own unwind data, as a debugger does. It changes no game code.

### History: the descriptor page leak (fixed by the game in patch 5.02)

Up to game version 5.01c, the DX12 renderer handed out shader-visible CBV/SRV/UAV descriptors from 1024-entry "pages", each its own `ID3D12DescriptorHeap`, like this:

- When the current page was full, the function at `+0x2809760` moved it to a *retired* list and took a page from the *free* list. If the free list was empty, it created a new heap.
- Pages only moved back from *retired* to *free* when a GPU-fence entry for them completed while they were no longer the current page.

The bug was in the case where the GPU keeps up with the CPU. A page's last fence entry often completed while that page was still the current page. When it was retired later, it had `pending == 0` and `usesThisFrame == 0`. It never got another fence entry, so it stayed in the retired list forever. About 1 in 5 retirements was lost this way, roughly 4 new heaps per second, until the driver refused to create more after about 4,000. `+0x2809760` then stored a NULL page without checking, and the next allocation dereferenced it at `+0x280ad3d`.

**The fix in W3CrashFix 1.0.0 to 2.3.0** replaced `+0x2809760` with an identical version that had one change: a retired page that is already idle goes straight to the free list. On the author's PC that ended more than 30 crashes at the same address, and the live heap count stayed flat at 7 to 9 instead of climbing to about 4,000.

**Patch 5.02** changed that function in the game: a page whose two counters are zero is now reused on the spot, and a failed heap creation returns an error instead of storing a NULL page.

## Changelog

The name after a line says whose idea it was: Havoc's, Claude's, or something a player's log showed.

### 3.0.1-5.0.17

3.0.1 replaces 3.0.0, which was online for about an hour on 27 November 2026: the same crash reporter, under a new license.

- **New license: the GNU General Public License, version 3 or later.** Everyone may still use, change and share W3CrashFix for free. Whoever shares a changed version must share its source code under the same license. Because the license changed, the license window appears once more. W3CrashFix 3.0.0 and older were published under the MIT License, and stay under it. - Havoc
- The additional permission for CD PROJEKT RED S.A. is gone with it. It was written for the fix, which the game now has itself. - Havoc

The rest, new since 2.3.0:

W3CrashFix is now a crash reporter. Game patch 5.02 fixes the descriptor heap leak in the game itself, so the fix this mod was made for is obsolete and has been taken out. So far forty-five crash reports from twenty-eight players have arrived; thank you to everyone who sent one.

- **The fix is gone.** The mod no longer changes the game's code, and no longer looks for particular code in it. Everything around it stays: the crash report, the report page, the logs. - Havoc
- **On a game older than patch 5.02** the activity log, the crash report, the send page and the installer say that the game should be updated first. 2.3.0 stays available as the last version with the fix. - Claude
- **We only look at crash reports from game version 5.02 or newer, made by W3CrashFix 3.0.0 or newer.** Logs and crash dumps from older versions of the game or of W3CrashFix are no longer accepted: they are deleted without being analysed. - Havoc
- Crash report: the verdict says in plain words when **another mod is the likely cause**. A mod or add-on in the call stack is named as the most likely cause; a modded game without one in the call stack gets "another mod or add-on is a likely cause", with the numbers. - Havoc
- Crash report: the **"Gpu Crash for unknown reasons"** crash is recognised by what happened, no longer by an address, so it is explained on every game version. It is a matter of the graphics driver or the hardware, and we no longer look into it. A new tip points to Windows' own record of the drop-out (`perfmon /rel`). - Havoc
- Setting **`GpuCrashDetails` is off by default again**, as before 2.3.0: the details were switched on to investigate the GPU crash, and that has stopped. A `1` that 2.3.0 set by itself is switched off once, at the next game start; set it back to `1` if you want the details. See [Settings](#settings). - Havoc and Claude
- Crash report: the draw-call crash that 2.3.0 recognised (`witcher3.exe+0x1EECD86` and `+0x1EECF31`) is no longer recognised: those are addresses of the older game version. - Claude
- Crash summary: one "Game:" line says whether the game is patch 5.02 or newer. The lines about the fix are gone. - Claude
- The license window appears on every game version, and its text describes a crash reporter. - Claude
- Version numbers: the game part is now 5.0.17, the game version this release was tested with (5.02 Steam). - Havoc
- Installer: it shows the game version and warns on a game older than patch 5.02. - Claude

**Do I need to update?** On game version 5.02 or newer: yes, if you want crash reports; 2.3.0's fix is switched off there anyway, and its log says the game is "not supported", which is out of date. On an older game version: update the game first, or stay on 2.3.0.

### 2.3.0-5.0.16

Crash reports now say more without you switching anything on. So far forty-one crash reports from twenty-six players have arrived; thank you to everyone who sent one.

- Setting **`GpuCrashDetails` is on by default**. A report of the "Gpu Crash for unknown reasons" crash now also names the work the graphics card had not finished. A `0` written by an earlier version is switched on once, at the next game start; set it back to `0` to switch it off. See [Settings](#settings). - Claude
- Report page: a **second known crash** is recognised, the crash while the game prepares a draw call (`witcher3.exe+0x1EECD86` and `+0x1EECF31`), reported by three players. The page says what is known and asks for a crash dump. - Claude, from players' logs
- Installer: on a game version other than 5.0.16 it says the release was tested with 5.0.16 and that other versions may work too. It used to say the fix stays inactive there, which a player's log showed is no longer true. - Havoc and Claude
- README and description: a player's crash log showed the fix switching itself on in game version 5.0.15.65482, where the code sits at other places. - Havoc, from a player's log
- README and description: the game part of the version number is the game version the release was tested with; older or newer game versions may work too. - Havoc
- README and description: Epic players are asked to try the latest version of the mod. - Havoc
- README and description: a "Known issue" section for the second known crash. - Claude
- The descriptor page fix is unchanged.

**Do I need to update?** The fix itself is the same, so there is no hurry. If you get the GPU crash or the draw-call crash, a report from 2.3.0 says more about it.

### 2.2.0-5.0.16

A small release that tidies up what 2.1.0 added. So far thirty crash reports from eighteen players have arrived; thank you to everyone who sent one.

- Crash report and activity log: the list of other DLL and ASI files no longer includes the **game's own files in `bin\ddi`** (such as `Steam.dll`). - Claude
- `W3CrashFix.ini`: the explanations are **shorter lines** that fit in Notepad without word wrap. The long lines from 2.1.0 are replaced once, at the next game start; your values stay as they are. - Havoc
- Report page: the button says "Send the crash files" when a crash dump was saved with the log (`CrashDump=1`). - Claude
- Installer: the file's properties in Windows show the right version (the 2.1.0 installer said 2.0.0.0 there). - Claude
- The changelogs say whose idea each change was. - Havoc
- The descriptor page fix is unchanged.

**Do I need to update?** The fix itself is the same, so there is no hurry. If you send crash logs, 2.2.0's list of other files is a little cleaner.

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
- `src\test_diag.c`: test for the game version line, the caller names in the log, the crash report, the settings file and the license window. `build.ps1` builds and runs it.
- `src\installer.c`, `src\installer.rc`, `src\installer.manifest`: the installer. Both mod files are embedded in it.
- `src\installer-icon.svg`, `src\installer.ico`: the installer's icon, the cat from the project's banner.

## FAQ

**Does it fix crashes?** Not any more. Up to 2.3.0 it fixed one, the descriptor heap crash; game patch 5.02 fixes that one in the game. Since 3.0.0 W3CrashFix reports crashes and tells you what it can about the cause.

**Does it change performance?** No. It only watches; with `GpuCrashDetails=1` it may cost a little frame rate on some PCs, which is why that is off by default.

**I'm on an older game version. What now?** Update the game: patch 5.02 has the fix built in. If you can't, W3CrashFix 2.3.0 is the last version with the fix.

**Can this get me banned?** The Witcher 3: Wild Hunt Remastered is single-player and has no anti-cheat. The mod uses the same loading method as many popular Witcher 3 mods.

**Did CD PROJEKT RED fix the crash this mod was made for?** Yes, in patch 5.02. Their patch notes don't mention it; we found it by reading the game's new code. See the top of this page.

## About the author

> I'm Havoc, just a guy trying out vibe coding for the first time: I describe what I want, and an AI writes the code. W3CrashFix is a side project that I made with Claude, and it works far better than I ever expected.

The crash analysis and the code were made with Claude (by Anthropic): the original fix with the Claude Opus 5.5 model, the updates up to 1.6.0 with the newer Claude Fable 5.2 model, 1.6.1 to 2.1.0 with Claude Opus 5.5, 2.2.0 with Claude Fable 5.2, 2.3.0 with Claude Opus 5.5 and Claude Fable 5.2, and 3.0.0 and 3.0.1 with Claude Fable 5.2 and Claude Opus 5.5. Everything was tested over many hours of play before publishing.

**Who did what.** The bug was only found because I let Claude analyse my crash dumps, and the technical explanations in this README were drafted by Claude. Since version 1.0.0, most changes were my ideas and every decision was mine; Claude worked out the details and wrote the code. For example, I wanted the mod to open the crash log after a crash and let players send it to me; showing that in a single browser page was Claude's idea, so a crashing game opens one window instead of several.

## Support

W3CrashFix is free. If it helped you and you'd like to say thanks, you can buy me a coffee on Ko-fi:

[![Support on Ko-fi](https://img.shields.io/badge/Support%20on-Ko--fi-FF5E5B?logo=ko-fi&logoColor=white)](https://ko-fi.com/havoc104/tip)

## License and credits

GNU General Public License v3.0 or later. See [LICENSE](LICENSE).

W3CrashFix 3.0.0 and older were published under the MIT License (from 1.6.0 with an additional permission for CD PROJEKT RED S.A.), and stay under it.

**Authors:** Onno Struijk (Havoc104) and Claude.

- Nexus Mods: [104Havoc](https://www.nexusmods.com/profile/104Havoc)
- GitHub: [Havoc104](https://github.com/Havoc104)
- Reddit: [u/Havoc_-_](https://www.reddit.com/user/Havoc_-_)
- Ko-fi: [havoc104](https://ko-fi.com/havoc104/tip)
