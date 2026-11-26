# Builds W3CrashFix with Zig (https://ziglang.org, any recent version; only `zig cc` is used).
#   .\build.ps1            -> build\dinput8.dll, build\W3CrashFix.asi, build\W3CrashFix-Installer.exe
#   .\build.ps1 -Package   -> also creates the release files:
#        dist\github\  asi + standalone zips, Installer.exe, source zip   (author shown as on GitHub)
#        dist\nexus\   asi + standalone + installer zips                 (author shown as on Nexus Mods;
#                                                                          Nexus only accepts archives)
#   .\build.ps1 -CrashTest  -> TEST BUILD with keys that crash the game on purpose (never for release; see w3crashfix.c)
param([switch]$Package, [switch]$CrashTest, [switch]$WindowTest, [string]$Zig)
#   .\build.ps1 -WindowTest -> after the normal tests, also tries the windows that open after a crash, outside the game:
#                              the stand-in game crashes while one of its own windows is frozen. This OPENS a folder
#                              window and the report page in your browser, so it is not part of the normal tests.
#                              Run it whenever the code that handles windows after a crash has changed.
$ErrorActionPreference = 'Stop'
Set-Location $PSScriptRoot
if (-not $Zig) {
    $local = Get-ChildItem tools -Recurse -Filter zig.exe -ErrorAction SilentlyContinue | Select-Object -First 1
    $Zig = if ($local) { $local.FullName } else { 'zig' }
}
New-Item -ItemType Directory -Force build | Out-Null
if ($Package -and $CrashTest) { throw 'A crash-test build must never be packaged for release.' }

# The version stands in four places; they must agree (2.1.0 went out with installer.manifest still saying 2.0.0.0).
$verMod = [regex]::Match((Get-Content src\w3crashfix.c -Raw), '#define W3CF_VERSION "(\d+\.\d+\.\d+)-').Groups[1].Value
$verIns = [regex]::Match((Get-Content src\installer.c -Raw), '#define W3CF_VERSION\s+L"(\d+\.\d+\.\d+)-').Groups[1].Value
$verRc  = [regex]::Match((Get-Content src\installer.rc -Raw), 'FILEVERSION (\d+),(\d+),(\d+),').Groups[1..3].Value -join '.'
$verMan = [regex]::Match((Get-Content src\installer.manifest -Raw), 'name="W3CrashFix\.Installer" version="(\d+\.\d+\.\d+)\.').Groups[1].Value
if (-not $verMod -or @($verMod, $verIns, $verRc, $verMan | Select-Object -Unique).Count -ne 1) {
    throw "Version mismatch, nothing was built: w3crashfix.c $verMod, installer.c $verIns, installer.rc $verRc, installer.manifest $verMan"
}

# Privacy check before a release (Onno, 19 Nov 2026, after the 1.8.0 changelog told readers that an example id had
# been a real player's: "we need to work together to prevent this type of thing from happening again"). Nothing is
# built when a published text contains a player's report id, an upload name or another private term. The terms live
# in promo\private-terms.txt and the ids in promo\private-ids.txt; promo\ is never packaged or committed, so this
# script itself names none of them. The ids are taken from the 'Report id' lines of the crash logs that were
# already scanned and analysed (promo\pcloud-analysed.txt), when the pCloud drive is there; ids found once are kept.
function Test-Privacy {
    $terms = @(Get-Content promo\private-terms.txt, promo\endorsers.txt -ErrorAction SilentlyContinue | Where-Object { $_.Trim() -and -not $_.StartsWith('#') } | ForEach-Object { $_.Trim() })
    $idFile = 'promo\private-ids.txt'
    $ids = [Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
    Get-Content $idFile -ErrorAction SilentlyContinue | Where-Object { $_ -match '^[0-9A-F]{16}$' } | ForEach-Object { [void]$ids.Add($_) }
    $logRoot = (Get-Content promo\pcloud-folder.txt -TotalCount 1 -ErrorAction SilentlyContinue)   # where the uploads land on this PC
    if ($logRoot -and (Test-Path -LiteralPath $logRoot)) {
        foreach ($line in Get-Content promo\pcloud-analysed.txt -ErrorAction SilentlyContinue | Where-Object { $_ -notmatch '^#' }) {
            $rel = ($line -split "`t")[1]; if (-not $rel) { continue }
            $f = Join-Path $logRoot $rel
            if (-not (Test-Path -LiteralPath $f)) { continue }
            foreach ($l in Get-Content -LiteralPath $f -TotalCount 40) {
                foreach ($m in [regex]::Matches($l, '(?i)report id:\s*\S*?([0-9A-F]{16})')) { [void]$ids.Add($m.Groups[1].Value.ToUpper()) }
            }
        }
        [IO.File]::WriteAllLines("$PWD\$idFile", [string[]]($ids | Sort-Object), (New-Object Text.UTF8Encoding $false))
    } else { 'NOTE: privacy check without the pCloud drive: only the report ids already in promo\private-ids.txt are checked' }
    if (-not $terms.Count) { throw 'Privacy check: promo\private-terms.txt is missing or empty' }
    $ver = (Select-String src\w3crashfix.c -Pattern '#define W3CF_VERSION "(.+)"').Matches[0].Groups[1].Value
    $short = $ver.Split('-')[0]
    # Everything that goes into the release files, plus the texts that go on GitHub and Nexus by hand.
    $files = @(Get-ChildItem src, .github -Recurse -File) + @(Get-Item build.ps1, Install-W3CrashFix.ps1, README.md, LICENSE, .gitignore, nexus\description.bbcode) +
             @(Get-ChildItem "promo\release-$short" -File -ErrorAction SilentlyContinue)
    $text = '\.(md|c|h|ps1|rc|manifest|def|py|yml|bbcode|txt|svg|gitignore)$|LICENSE$'
    $found = @()
    foreach ($f in $files | Where-Object { $_.Name -match $text }) {
        $t = [IO.File]::ReadAllText($f.FullName)
        foreach ($w in $terms) {
            $rx = '(?i)(?<![\w])' + [regex]::Escape($w) + '(?![\w])'
            if ($t -match $rx) { $found += "$($f.FullName.Substring($PWD.Path.Length + 1)): private term '$w'" }
        }
        foreach ($id in $ids) {   # the whole id, or its first 8 characters on their own (as in "0123ABCD...")
            if ($t -match "(?i)$id" -or $t -match "(?i)(?<![0-9A-F])$($id.Substring(0, 8))(?![0-9A-F])") { $found += "$($f.FullName.Substring($PWD.Path.Length + 1)): a player's report id ($($id.Substring(0, 4))...)" }
        }
    }
    if ($found.Count) { throw "Privacy check failed, nothing was built:`n  " + ($found -join "`n  ") }
    "Privacy check passed: $($files.Count) files, $($terms.Count) private terms, $($ids.Count) report ids"
}
if ($Package) { Test-Privacy }
$cc = @('cc', '-target', 'x86_64-windows-gnu', '-O2', '-Wall')
[string[]]$modFlags = @('-DW3_LICENSE'); if ($CrashTest) { $modFlags += '-DW3_CRASHTEST' }   # W3_LICENSE: the license dialog at game start
$guiLibs = @('-luser32', '-lkernel32', '-lshell32', '-lole32', '-ladvapi32', '-lgdi32', '-lversion')
function Run { & $Zig @args; if ($LASTEXITCODE) { throw "zig failed: $args" } }
# Fill in the PE checksum field (the linker leaves it 0; Microsoft's toolchain and signed files always set it).
Add-Type -Namespace W3 -Name Img -MemberDefinition @'
[DllImport("imagehlp.dll", CharSet = CharSet.Unicode)]
public static extern uint MapFileAndCheckSumW(string file, out uint headerSum, out uint checkSum);
'@ -ErrorAction SilentlyContinue
function Set-PeChecksum($path) {
    $full = (Resolve-Path $path).Path
    $h = 0; $c = 0
    if ([W3.Img]::MapFileAndCheckSumW($full, [ref]$h, [ref]$c) -ne 0) { throw "checksum failed: $path" }
    $b = [IO.File]::ReadAllBytes($full)
    $off = [BitConverter]::ToInt32($b, 0x3C) + 24 + 64            # PE header + COFF header + CheckSum offset
    [BitConverter]::GetBytes([uint32]$c).CopyTo($b, $off)
    [IO.File]::WriteAllBytes($full, $b)
}
$installerFlags = @('-s', '-municode', '-Wl,--subsystem,windows', '-Wl,--build-id=none')

Run @cc @modFlags -shared -o build\dinput8.dll src\w3crashfix.c src\dinput8.def -luser32 -lkernel32
Run @cc @modFlags -shared -DW3_ASI -o build\W3CrashFix.asi src\w3crashfix.c src\w3crashfix_asi.def -luser32 -lkernel32
# installer embeds both files above (see src\installer.rc), so it is built after them
Run @cc @installerFlags -o build\W3CrashFix-Installer.exe src\installer.c src\installer.rc @guiLibs
Set-PeChecksum build\W3CrashFix-Installer.exe
# A test variant of the mod, only for the license dialog tests below: the stand-in "game" of the tests is not a
# supported build, on which the real mod shows no dialog. This variant shows it anyway. It is never released.
New-Item -ItemType Directory -Force build\license | Out-Null
Run @cc @modFlags -DW3_LICENSE_TEST -shared -DW3_ASI -o build\license\W3CrashFix.asi src\w3crashfix.c src\w3crashfix_asi.def -luser32 -lkernel32
'Built build\dinput8.dll, build\W3CrashFix.asi and build\W3CrashFix-Installer.exe'

# Test: a stand-in "witcher3.exe" that is not the game (src\test_diag.c). The mod must leave it unpatched, describe it
# in 'diag:' lines, and - if this PC has a D3D12 device - name the caller of a descriptor heap.
$t = 'build\test\diag'
New-Item -ItemType Directory -Force $t | Out-Null
Remove-Item "$t\*.log" -ErrorAction SilentlyContinue
Run @cc -municode -o "$t\witcher3.exe" src\test_diag.c -ld3d12 -ldxguid -luser32
Copy-Item build\W3CrashFix.asi $t -Force
$licId = (Select-String src\w3cf_license.h -Pattern '#define W3CF_LICENSE_ID "(.+)"').Matches[0].Groups[1].Value
Remove-Item "$t\W3CrashFix.ini", "$t\W3CrashFix.accepted", "$t\W3CrashFix.id", "$t\W3CrashFix.noreport" -ErrorAction SilentlyContinue   # no acceptance: on this unsupported stand-in the dialog must stay away by itself
# A value from the mod's settings file (W3CrashFix.ini), or '' if the file or the key is missing.
function IniVal($file, $key) {
    if (Test-Path $file) { $m = Select-String -Path $file -Pattern "^$key=(.*)$"; if ($m) { return $m[0].Matches[0].Groups[1].Value.Trim() } }
    ''
}
$out =& "$t\witcher3.exe" "$PWD\$t\W3CrashFix.asi" heap
$code = $LASTEXITCODE
$log = if (Test-Path "$t\W3CrashFix.activity.log") { Get-Content "$t\W3CrashFix.activity.log" -Raw } else { '' }
foreach ($need in 'FIX DISABLED', 'diag: 4 of 5 functions located', 'ensure: same code found 1 time') {
    if (-not $log.Contains($need)) { throw "test_diag: log lacks '$need'" }
}
if ($log.Contains('ACTIVE')) { throw 'test_diag: the mod patched a program that is not the game' }
if ($log.Contains('license: asking')) { throw 'test_diag: the license dialog was shown on an unsupported build' }
if ($log -match 'not found with the same code') { } else { throw 'test_diag: the stand-in has only the first bytes of each function; the mod must say it did not find them with the same code' }

# Finding the code (1.8.0): the stand-in copies the six real functions from the installed game to other places in its
# own code (nothing of the game is in this repository). The mod must find all of them, say they moved, and detour the
# copy of ensure. With a second copy of ensure it must patch nothing. Needs the real 5.0.16 witcher3.exe on this PC;
# without it the test is skipped and says so.
$gameExe = if ($env:W3_GAME_EXE) { $env:W3_GAME_EXE } else { 'C:\Games\The Witcher 3\bin\x64_dx12\witcher3.exe' }
if (Test-Path $gameExe) {
    $r = 'build\test\reloc'
    Remove-Item $r -Recurse -Force -ErrorAction SilentlyContinue
    New-Item -ItemType Directory -Force $r | Out-Null
    Copy-Item "$t\witcher3.exe", build\W3CrashFix.asi $r
    Set-Content "$r\W3CrashFix.ini" "[W3CrashFix]`r`nLicenseAccepted=$licId`r`nOpenReportPage=0" -Encoding ascii
    $ro = (& "$r\witcher3.exe" "$PWD\$r\W3CrashFix.asi" reloc $gameExe) -join "`n"
    $rl = Get-Content "$r\W3CrashFix.activity.log" -Raw
    foreach ($need in 'the code the fix needs is the same, at other places', 'fix:   ensure at w3+0x', 'fix:   crashfn at w3+0x', 'fix: descriptor page recycling fix ACTIVE') {
        if (-not $rl.Contains($need)) { throw "reloc test: log lacks '$need'`n$ro" }
    }
    if ($ro -notmatch 'detour on the copy of ensure: yes') { throw "reloc test: the copy of ensure was not detoured`n$ro" }
    Remove-Item "$r\W3CrashFix.activity.log", "$r\W3CrashFix.activity.previous.log" -ErrorAction SilentlyContinue
    $ro = (& "$r\witcher3.exe" "$PWD\$r\W3CrashFix.asi" relocdup $gameExe) -join "`n"
    $rl = Get-Content "$r\W3CrashFix.activity.log" -Raw
    if (-not $rl.Contains('ensure (w3+0x2809760 in 5.0.16) not found with the same code') -or $rl.Contains('ACTIVE')) { throw "reloc test: with two copies of ensure the mod must patch nothing`n$rl" }
    if ($ro -notmatch 'detour on the copy of ensure: no') { throw "reloc test: a copy of ensure was detoured although there were two`n$ro" }
    Remove-Item $r -Recurse -Force
    'Test passed: finding the code at other places (and refusing when it is there twice)'
} else { "Test SKIPPED: finding the code at other places (no game executable at $gameExe; set W3_GAME_EXE)" }
if ($code -eq 0) {
    if ($log -notmatch 'caller=witcher3\.exe\+0x[0-9A-F]+') { throw 'test_diag: heap caller was not named' }
    if ($log -notmatch 'driver: \d+\.\d+\.\d+\.\d+') { throw 'test_diag: the graphics driver version was not logged (2.0.0)' }
    # after 2.0.0: the other DLL and ASI files in the game folder (the stand-in game may list none or its own test files)
    if ($log -notmatch 'other files: DLL and ASI files in the game folder that are not the game''s own: ') { throw 'test_diag: the other-files line is missing' }
    # 2.0.0: the game's render settings, read from Documents\The Witcher 3\dx12user.settings (or "not found" on a PC
    # without the game); the file's path must never be in the log (it holds the Windows user name).
    if ($log -notmatch 'game settings: (ray tracing (ON|off|\?), path tracing (ON|off|\?) \||not found)') { throw 'test_diag: the game settings line is missing (2.0.0)' }
    if ($log -match 'dx12user\.settings' -and $log -notmatch 'not found \(no Documents') { throw 'test_diag: the log names the settings file path' }
    if ($log -notmatch 'module: witcher3\.exe = ') { throw 'test_diag: module path line missing' }
    # Same program run from a folder laid out like NVIDIA's downloaded DLSS files: the feature folder must show.
    $ngx = "$t\NGX\models\dlssg\versions\1\files"
    New-Item -ItemType Directory -Force $ngx | Out-Null
    Remove-Item "$ngx\*.log" -ErrorAction SilentlyContinue
    Copy-Item "$t\witcher3.exe", "$t\W3CrashFix.asi" $ngx -Force
    & "$ngx\witcher3.exe" "$PWD\$ngx\W3CrashFix.asi" heap | Out-Null
    $log2 = if (Test-Path "$ngx\W3CrashFix.activity.log") { Get-Content "$ngx\W3CrashFix.activity.log" -Raw } else { '' }
    if ($log2 -notmatch 'caller=dlssg\\witcher3\.exe\+0x[0-9A-F]+') { throw 'test_diag: feature folder missing from the caller name' }
    # Crash report: the same program crashes on purpose. OpenReportPage=0 keeps the report page and the folder window
    # from opening here. The setting is given the old way, as an empty W3CrashFix.noreport: the mod must take that
    # over into W3CrashFix.ini and remove the file.
    # The crash log has a folder of its own, with nothing else in it. One left in the old place by an earlier version must go.
    $clf = "$t\W3CrashFix crash report\W3CrashFix.crash.log"
    Remove-Item "$t\W3CrashFix crash report" -Recurse -Force -ErrorAction SilentlyContinue
    Remove-Item "$t\W3CrashFix-report.html", "$t\W3CrashFix.ini", "$t\W3CrashFix-send-log.html" -ErrorAction SilentlyContinue
    Set-Content "$t\W3CrashFix.crash.log" 'an old crash log in the old place' -Encoding ascii
    Remove-Item 'build\Mods', 'build\DLC' -Recurse -Force -ErrorAction SilentlyContinue
    Set-Content "$t\W3CrashFix.noreport" '' -Encoding ascii
    & "$t\witcher3.exe" "$PWD\$t\W3CrashFix.asi" crash | Out-Null
    if ($LASTEXITCODE -eq 0) { throw 'test_diag: the crash test did not crash' }
    $global:LASTEXITCODE = 0                                  # the crash was the point of this test
    $cl = if (Test-Path $clf) { Get-Content $clf -Raw } else { '' }
    $rp = if (Test-Path "$t\W3CrashFix-report.html") { Get-Content "$t\W3CrashFix-report.html" -Raw } else { '' }
    foreach ($need in 'W3CrashFix crash report', 'Crash: exception 0xC0000005 (access violation, writing address 0x0) at witcher3.exe+0x', 'Registers: rax=', 'r15=', 'In-game mods: none', 'Verdict: ', '!!!! CRASH: exception 0xC0000005', 'not opened because OpenReportPage=0 in W3CrashFix.ini', 'mods: no in-game mods found', 'W3CrashFix.noreport was taken over into W3CrashFix.ini') {
        if (-not $cl.Contains($need) -and -not (Get-Content "$t\W3CrashFix.activity.log" -Raw).Contains($need)) { throw "test_diag: crash log lacks '$need'" }
    }
    if ((Test-Path "$t\W3CrashFix.noreport") -or (IniVal "$t\W3CrashFix.ini" 'OpenReportPage') -ne '0') { throw 'test_diag: W3CrashFix.noreport was not taken over into W3CrashFix.ini as OpenReportPage=0' }
    if (Test-Path "$t\W3CrashFix.crash.log") { throw 'test_diag: the crash log of an earlier version in the old place was not removed' }
    # 2.0.0: the GPU state line (answered by a thread of its own, so the report must still come out if D3D12 is slow)
    # and the visible GpuCrashDetails=0 default in W3CrashFix.ini.
    if ($cl -notmatch 'GPU state: [^\r\n]+') { throw 'test_diag: the crash summary has no "GPU state:" line' }
    if (-not (Get-Content "$t\W3CrashFix.activity.log" -Raw).Contains('[CRASH] GPU state: ')) { throw 'test_diag: the activity log lacks the GPU state line' }
    if ((IniVal "$t\W3CrashFix.ini" 'GpuCrashDetails') -ne '1') { throw 'test_diag: W3CrashFix.ini does not show GpuCrashDetails=1 (on by default since 2.3.0)' }
    if ((IniVal "$t\W3CrashFix.ini" 'DefaultsSet') -ne '2') { throw 'test_diag: W3CrashFix.ini does not show DefaultsSet=2' }
    # 1.6.2: the call stack. It starts at the crash address and names at least one caller in the stand-in game.
    $csLine = ($cl -split "`r?`n" | Where-Object { $_ -like 'Call stack: *' } | Select-Object -First 1)
    $crAt = [regex]::Match($cl, 'Crash: exception [^\r\n]* at (witcher3\.exe\+0x[0-9A-F]+)').Groups[1].Value
    if (-not $csLine) { throw 'test_diag: the crash summary has no "Call stack:" line' }
    if (-not $crAt -or -not $csLine.StartsWith("Call stack: $crAt")) { throw "test_diag: the call stack does not start at the crash address ($crAt): $csLine" }
    if (([regex]::Matches($csLine, 'witcher3\.exe\+0x[0-9A-F]+')).Count -lt 2) { throw "test_diag: the call stack names no caller in the stand-in game: $csLine" }
    if (-not (Get-Content "$t\W3CrashFix.activity.log" -Raw).Contains('[CRASH] call stack: ')) { throw 'test_diag: the activity log lacks the call stack line' }
    if (@(Get-ChildItem "$t\W3CrashFix crash report").Count -ne 1) { throw 'test_diag: the crash log''s own folder must hold exactly one file' }
    # 2.0.0: files in the call stack with versions, the events line, and CrashDump=0 visible by default (no dump then).
    if ($cl -notmatch 'Files in the call stack: [^\r\n]*witcher3\.exe') { throw 'test_diag: the crash summary does not list witcher3.exe under "Files in the call stack:"' }
    if ($cl -notmatch 'Before the crash: [^\r\n]+') { throw 'test_diag: the crash summary has no "Before the crash:" line' }
    if ((IniVal "$t\W3CrashFix.ini" 'CrashDump') -ne '0') { throw 'test_diag: W3CrashFix.ini does not show CrashDump=0' }
    # 2.0.0: with CrashDump=1 the same crash also writes W3CrashFix.crash.dmp (a minidump: starts with MDMP) next to
    # the log, the log says so, and the next crash replaces it rather than leaving an old one.
    (Get-Content "$t\W3CrashFix.ini") -replace '^CrashDump=0$', 'CrashDump=1' | Set-Content "$t\W3CrashFix.ini" -Encoding ascii
    # the second crash rewrites the report page and the logs; the checks further down are about the first crash
    $keep = "$t\keep-first-crash"; New-Item -ItemType Directory -Force $keep | Out-Null
    Copy-Item "$t\W3CrashFix-report.html", "$t\W3CrashFix.activity.log", $clf $keep -Force
    & "$t\witcher3.exe" "$PWD\$t\W3CrashFix.asi" crash | Out-Null
    if ($LASTEXITCODE -eq 0) { throw 'test_diag: the crash dump test did not crash' }
    $global:LASTEXITCODE = 0
    $dmp = "$t\W3CrashFix crash report\W3CrashFix.crash.dmp"
    if (-not (Test-Path $dmp)) { throw 'test_diag: CrashDump=1 wrote no W3CrashFix.crash.dmp' }
    $sig = [System.Text.Encoding]::ASCII.GetString([System.IO.File]::ReadAllBytes($dmp), 0, 4)
    if ($sig -ne 'MDMP') { throw "test_diag: W3CrashFix.crash.dmp is not a minidump (starts with '$sig')" }
    if (-not (Get-Content "$t\W3CrashFix.activity.log" -Raw).Contains('crash dump: written, W3CrashFix.crash.dmp')) { throw 'test_diag: the log does not say the dump was written' }
    # With a dump listed, the report page's button says "Send the crash files"
    if (-not (Get-Content "$t\W3CrashFix-report.html" -Raw).Contains('onclick="cp(this)">Send the crash files</a>')) { throw 'test_diag: with a crash dump the report page button does not say "Send the crash files"' }
    "Test passed: crash dump with CrashDump=1 ($([math]::Round((Get-Item $dmp).Length / 1KB)) KB)"
    (Get-Content "$t\W3CrashFix.ini") -replace '^CrashDump=1$', 'CrashDump=0' | Set-Content "$t\W3CrashFix.ini" -Encoding ascii
    Remove-Item $dmp
    Copy-Item "$keep\W3CrashFix-report.html", "$keep\W3CrashFix.activity.log" $t -Force
    Copy-Item "$keep\W3CrashFix.crash.log" $clf -Force
    Remove-Item $keep -Recurse -Force
    # Send page for the activity log (Onno, 19 Nov 2026): written at every start, never opened by the mod. The stand-in
    # game is an unsupported build, so the "especially helpful" notice must be there too. Exactly one script.
    $sp = if (Test-Path "$t\W3CrashFix-send-log.html") { Get-Content "$t\W3CrashFix-send-log.html" -Raw } else { '' }
    if (-not $sp) { throw 'test_diag: W3CrashFix-send-log.html was not written' }
    foreach ($need in 'Send the activity log', 'u.pcloud.com/#/puplink?code=', 'W3CrashFix.activity.log" onclick="cp(this)">Send the activity log</a>', 'also from a game that did not crash', 'Your log is especially helpful', 'W3CrashFix.activity.previous.log', "Copy the file's path") {
        if (-not $sp.Contains($need)) { throw "test_diag: the send page lacks '$need'" }
    }
    if (([regex]::Matches($sp, '<script')).Count -ne 1) { throw 'test_diag: the send page must have exactly one script' }
    if (-not (Get-Content "$t\W3CrashFix.activity.log" -Raw).Contains('send page: W3CrashFix-send-log.html written')) { throw 'test_diag: the activity log lacks the send page line' }
    # The report id: 16 hex digits, the same in the summary and in W3CrashFix.ini.
    # Test builds (-CrashTest) always use the fixed id 1337testlulz and write it to W3CrashFix.ini, note included.
    # Nowhere in the mod's own texts may personalising the id be mentioned (only in W3CrashFix.ini).
    foreach ($txt in @($cl, (Get-Content "$t\W3CrashFix.activity.log" -Raw), $rp)) {
        if ($txt -match '(?i)personali[sz]') { throw 'test_diag: the log or the report page mentions personalising the report id' }
    }
    if ($CrashTest) {
        $rid = '1337testlulz'
        if ((IniVal "$t\W3CrashFix.ini" 'ReportId') -ne $rid) { throw 'test_diag: a test build did not write ReportId=1337testlulz into W3CrashFix.ini' }
        if (-not (Get-Content "$t\W3CrashFix.ini" -Raw).Contains('; ReportId: ')) { throw 'test_diag: a test build did not add the note above the ReportId line' }
    } else {
        $rid = IniVal "$t\W3CrashFix.ini" 'ReportId'
        if ($rid -notmatch '^[0-9A-F]{16}$') { throw "test_diag: W3CrashFix.ini does not hold a report id ('$rid')" }
        # The note about personalising the id: only in W3CrashFix.ini, once, right above the ReportId line.
        $iniText = Get-Content "$t\W3CrashFix.ini" -Raw
        if (([regex]::Matches($iniText, '; ReportId: ')).Count -ne 1 -or $iniText -notmatch '; Allowed: [^\r\n]*\r?\n; Anything else is replaced by a new random id\.\r?\nReportId=') { throw 'test_diag: W3CrashFix.ini lacks the note above the ReportId line (or has it twice)' }
        # 2.0.0: the line about several installations, exactly once, right above "; Allowed:"
        if (([regex]::Matches($iniText, '; More than one installation of the game\?')).Count -ne 1 -or $iniText -notmatch '(?s); More than one installation.{0,220}?tell your installations apart\.\r?\n; Allowed: ') { throw 'test_diag: W3CrashFix.ini lacks the line about several installations above "; Allowed:" (or has it twice)' }
    }
    if (-not $cl.Contains("Report id: $rid")) { throw 'test_diag: the crash summary does not show the report id from W3CrashFix.ini' }
    # 2.1.0: every setting has its explanation, exactly once (also after several game starts), right above its line.
    $iniAll = Get-Content "$t\W3CrashFix.ini" -Raw
    foreach ($s in @(@('GpuCrashDetails', 'Takes effect at the next game start\.'), @('CrashDump', 'Takes effect at the next game start\.'), @('OpenReportPage', 'The crash log is saved either way\.'))) {
        if (([regex]::Matches($iniAll, "; $($s[0]): ")).Count -ne 1 -or $iniAll -notmatch "$($s[1])\r?\n$($s[0])=") { throw "test_diag: W3CrashFix.ini lacks the explanation above $($s[0])= (or has it twice)" }
    }
    # No line of the settings file is longer than 80 characters (it must read well in Notepad without word wrap).
    $long = @(($iniAll -split "`r?`n") | Where-Object { $_.Length -gt 80 })
    if ($long.Count) { throw "test_diag: W3CrashFix.ini has line(s) longer than 80 characters: $($long[0])" }
    if ($iniAll -match '(?m)^LicenseAccepted=' -and ([regex]::Matches($iniAll, '; LicenseAccepted: ')).Count -ne 1) { throw 'test_diag: W3CrashFix.ini lacks the explanation above LicenseAccepted= (or has it twice)' }
    # The report page: one button, to the upload page; the folder link; nothing left of the two old buttons.
    foreach ($need in 'Send the crash log', 'u.pcloud.com/#/puplink?code=', 'A small folder window has opened in the lower right corner that holds only this file', "Copy the file's path", 'W3CrashFix crash report\W3CrashFix.crash.log" onclick="cp(this)"', 'Files are uploaded!', 'Why use my browser?', 'The mod itself never connects to the internet') {
        if (-not $rp.Contains($need)) { throw "test_diag: report page lacks '$need'" }
    }
    if (([regex]::Matches($rp, '<script')).Count -ne 1) { throw 'test_diag: the report page must hold exactly one script (the copy button)' }
    # 1.6.1: the Send button copies the crash log's path as well, and the steps say where to paste it.
    foreach ($need in 'W3CrashFix crash report\W3CrashFix.crash.log" onclick="cp(this)">Send the crash log</a>', 'Click Here to Add Files', 'press Ctrl+V and then Enter', 'id="c"') {
        if (-not $rp.Contains($need)) { throw "test_diag: report page lacks '$need'" }
    }
    if (([regex]::Matches($rp, 'onclick="cp\(this\)"')).Count -ne 2) { throw 'test_diag: both the Send button and the small button must copy the path' }
    # 1.7.1: the date and time of the crash, in the crash log and on the page, and the "older crash" notice (hidden at first).
    $today = Get-Date
    $whenRx = '{0} {1} {2:0000}, \d\d:\d\d' -f $today.Day, $today.ToString('MMM', [Globalization.CultureInfo]::InvariantCulture), $today.Year
    if ($cl -notmatch "(?m)^W3CrashFix crash report\r?\nCrashed at: $whenRx \(this PC's time\)\r?\nMod: ") { throw 'test_diag: the crash log lacks the "Crashed at:" line (date and time) right below its first line' }
    if ($rp -notmatch "<p class=""s"" id=""w"" data-t=""\d{13}"">Crash of $whenRx \(your PC's time\)\.</p>") { throw 'test_diag: the report page lacks the date and time of the crash' }
    $dataT = [int64]([regex]::Match($rp, 'data-t="(\d{13})"').Groups[1].Value)
    $nowMs = [DateTimeOffset]::UtcNow.ToUnixTimeMilliseconds()
    if ([math]::Abs($nowMs - $dataT) -gt 600000) { throw "test_diag: the page's crash time (data-t=$dataT) is not the time of the test crash" }
    foreach ($need in '<p class="o" id="o" hidden><b>This page belongs to an older crash</b>', 'Date.now()-t>3600000', "document.getElementById('o').hidden=false") {
        if (-not $rp.Contains($need)) { throw "test_diag: report page lacks the older-crash notice part '$need'" }
    }
    # The footnote names the page's own size, and it must be right (KB with one decimal, rounded).
    $rpBytes = (Get-Item "$t\W3CrashFix-report.html").Length
    $want = '{0}.{1}' -f [math]::Floor([math]::Floor(($rpBytes * 10 + 512) / 1024) / 10), ([math]::Floor(($rpBytes * 10 + 512) / 1024) % 10)
    if (-not $rp.Contains("but the file itself ($want KB), and the installer's Uninstall removes it.")) { throw "test_diag: the footnote does not give the page's size ($rpBytes bytes = $want KB)" }
    # The page shows only "Fix active" and the verdict (Onno, 17 Nov 2026); the full summary stays in the crash log.
    foreach ($need in 'Fix active: ', 'Verdict: ') { if (-not $rp.Contains($need)) { throw "test_diag: report page lacks '$need'" } }
    foreach ($gone in 'Mod: W3CrashFix', 'Report id: ', 'In-game mods:', 'Game build supported:', 'Registers:', 'Session length:', 'Graphics card:', 'Descriptor heaps:', 'Memory: VRAM', 'summary above') {
        if ($rp.Contains($gone)) { throw "test_diag: report page still shows '$gone' (only Fix active and the verdict belong there)" }
    }
    foreach ($gone in 'Report on GitHub', 'open Nexus Mods', 'issues/new', 'Open the folder', 'file:///') {
        if ($rp.Contains($gone)) { throw "test_diag: report page still contains '$gone'" }
    }
    # In-game mods. The stand-in game sits in build\test\diag, so its "game folder" is build (two levels up, as
    # <game>\bin\x64_dx12 is). The mod only names them: a crash report is made as always, with the same report id,
    # and lists the mods. The game's own add-on folders in DLC (bob, ep1, dlc + digits) are not named.
    $mods = 'build\Mods'; $dlc = 'build\DLC'
    Remove-Item $mods, $dlc -Recurse -Force -ErrorAction SilentlyContinue
    foreach ($m in "$mods\mod0rt_shadow_fixes", "$mods\modSomethingElse", "$dlc\dlcMyAddon", "$dlc\dlc7", "$dlc\bob") { New-Item -ItemType Directory -Force $m | Out-Null }
    # A long mod list (since 1.6.2): the crash summary must name every one of them, however long the list is
    # (a 1.6.1 report was cut off at 600 characters). 150 long names make about 9 KB.
    $many = 1..150 | ForEach-Object { 'modListTest{0:D3}_AVeryLongModFolderNameLikeRealOnes' -f $_ }
    foreach ($m in $many) { New-Item -ItemType Directory -Force "$mods\$m" | Out-Null }
    Remove-Item $clf, "$t\W3CrashFix-report.html"
    & "$t\witcher3.exe" "$PWD\$t\W3CrashFix.asi" crash | Out-Null
    $global:LASTEXITCODE = 0
    $cl = if (Test-Path $clf) { Get-Content $clf -Raw } else { '' }
    if (-not (Test-Path "$t\W3CrashFix-report.html")) { throw 'test_diag: no report page was made although in-game mods must not switch reporting off' }
    foreach ($need in 'mod0rt_shadow_fixes', 'modSomethingElse', 'dlcMyAddon', "Report id: $rid", 'mods: in-game mods found (153): ', 'shortened here; a crash report lists all of them') {
        if (-not $cl.Contains($need)) { throw "test_diag: with in-game mods, the crash log lacks '$need'" }
    }
    $modLine = ($cl -split "`r?`n" | Where-Object { $_ -like 'In-game mods*' } | Select-Object -First 1)
    if ($modLine -notlike 'In-game mods (153): *') { throw "test_diag: the summary's mods line does not start with 'In-game mods (153): '" }
    if ($modLine -match 'dlc7|bob') { throw "test_diag: the game's own add-on folders were named as mods ($modLine)" }
    foreach ($need in @('mod0rt_shadow_fixes', 'modSomethingElse', 'dlcMyAddon') + $many) {
        if (-not $modLine.Contains($need)) { throw "test_diag: the summary's mods line lacks '$need' (the list must be complete)" }
    }
    if ($modLine.Contains('...') -or $modLine.Contains('more (too many')) { throw 'test_diag: the summary cut the mods list short' }
    Remove-Item $mods, $dlc -Recurse -Force
    # A personalised report id (since 1.6.2): kept as the user wrote it. One with characters that are not allowed is
    # replaced by a new random id. Release builds only: test builds always use 1337testlulz.
    if (-not $CrashTest) {
        # Text before or after the id is kept (Onno, 18 Nov 2026). A completely own id is shelved for 1.7.0, and text in
        # the middle breaks the id: both replaced, like too long or other characters.
        $mid = $rid.Substring(0, 8) + 'HAVOC' + $rid.Substring(8)
        foreach ($case in @(@("Havoc_$rid", "Havoc_$rid"), @("$($rid)havoc", "$($rid)havoc"), @('Geralt_of_Rivia-1272', $null), @($mid, $null),
                            @('bad id!', $null), @('Geralt1272', $null), @(('x' * 41), $null),
                            @('Havoc_1337TestLulz_abc', $null))) {   # the test builds' id is reserved, in any case
            $ini = Get-Content "$t\W3CrashFix.ini" -Raw
            Set-Content "$t\W3CrashFix.ini" ($ini -replace '(?m)^ReportId=.*$', "ReportId=$($case[0])") -Encoding ascii -NoNewline
            Remove-Item $clf, "$t\W3CrashFix-report.html" -ErrorAction SilentlyContinue
            & "$t\witcher3.exe" "$PWD\$t\W3CrashFix.asi" crash | Out-Null
            $global:LASTEXITCODE = 0
            $cl = if (Test-Path $clf) { Get-Content $clf -Raw } else { '' }
            $now = IniVal "$t\W3CrashFix.ini" 'ReportId'
            if ($case[1]) {
                if ($now -ne $case[1] -or -not $cl.Contains("Report id: $($case[1])")) { throw "test_diag: the personalised report id '$($case[1])' was not kept (ini: '$now')" }
            } elseif ($now -notmatch '^[0-9A-F]{16}$' -or -not $cl.Contains("Report id: $now")) { throw "test_diag: the invalid report id was not replaced by a new random one (ini: '$now')" }
        }
        # A refused id is reported in W3CrashFix.ini, once, right above ReportId=, with the new id and without a
        # reason (Onno: "notified that their id is invalid ... without revealing the reason"). It stays while the id
        # is the one the mod made, and goes once the user sets a usable id of their own.
        $iniText = Get-Content "$t\W3CrashFix.ini" -Raw; $now = IniVal "$t\W3CrashFix.ini" 'ReportId'
        $notice = "; The ReportId you entered (Havoc_1337TestLulz_abc) can't be used, so a new random id was made: $now"
        if (([regex]::Matches($iniText, '; The ReportId you entered')).Count -ne 1 -or -not $iniText.Contains("$notice`r`nReportId=")) { throw 'test_diag: the refused report id is not reported in W3CrashFix.ini as expected' }
        foreach ($txt in @($iniText, (Get-Content "$t\W3CrashFix.activity.log" -Raw))) { if ($txt -match '(?i)reserved|test build') { throw 'test_diag: the reason for refusing the report id is given away' } }
        & "$t\witcher3.exe" "$PWD\$t\W3CrashFix.asi" heap | Out-Null; $global:LASTEXITCODE = 0
        if (-not (Get-Content "$t\W3CrashFix.ini" -Raw).Contains($notice)) { throw 'test_diag: the notice went away although the id is still the one the mod made' }
        $iniText = Get-Content "$t\W3CrashFix.ini" -Raw
        Set-Content "$t\W3CrashFix.ini" ($iniText -replace '(?m)^ReportId=.*$', "ReportId=Havoc_$now") -Encoding ascii -NoNewline
        & "$t\witcher3.exe" "$PWD\$t\W3CrashFix.asi" heap | Out-Null; $global:LASTEXITCODE = 0
        if ((Get-Content "$t\W3CrashFix.ini" -Raw).Contains('; The ReportId you entered')) { throw 'test_diag: the notice stayed after the user set a usable id' }
    }
    Remove-Item "$t\W3CrashFix.ini"
    # License dialog (the variant in build\license): without an acceptance in W3CrashFix.ini the mod asks. The test program presses the buttons
    # itself, so two dialogs flash by. OK must store the acceptance; OK + "I decline" must rename the mod's file and remove the settings.
    $lic = 'build\test\license'
    if (Test-Path $lic) { Remove-Item $lic -Recurse -Force }
    New-Item -ItemType Directory -Force $lic | Out-Null
    Copy-Item "$t\witcher3.exe", build\license\W3CrashFix.asi $lic
    & "$lic\witcher3.exe" "$PWD\$lic\W3CrashFix.asi" accept | Out-Null
    if ($LASTEXITCODE) { throw "test_diag: the license dialog did not appear or Accept failed (exit $LASTEXITCODE)" }
    $ll = Get-Content "$lic\W3CrashFix.activity.log" -Raw
    foreach ($need in 'license: asking the user to accept the MIT License', 'held until the user has chosen', 'license: the game continues', 'license: accepted (saved in W3CrashFix.ini') {
        if (-not $ll.Contains($need)) { throw "test_diag: license log lacks '$need'" }
    }
    if ((IniVal "$lic\W3CrashFix.ini" 'LicenseAccepted') -ne $licId) { throw 'test_diag: OK did not store the license id in W3CrashFix.ini' }
    # An acceptance given for another license text does not count: the dialog must come back and the value must be replaced.
    Set-Content "$lic\W3CrashFix.ini" "[W3CrashFix]`r`nLicenseAccepted=OLD-0" -Encoding ascii
    & "$lic\witcher3.exe" "$PWD\$lic\W3CrashFix.asi" accept | Out-Null
    if ($LASTEXITCODE) { throw "test_diag: the dialog did not return for an acceptance of another version (exit $LASTEXITCODE)" }
    if ((IniVal "$lic\W3CrashFix.ini" 'LicenseAccepted') -ne $licId) { throw 'test_diag: LicenseAccepted in W3CrashFix.ini was not updated to this license id' }
    if ((Get-Content "$lic\W3CrashFix.activity.log" -Raw) -notmatch 'license: an acceptance of an earlier license text was found') { throw 'test_diag: the dialog did not say that the license has changed' }
    # Coming from 1.5.0: the acceptance is in W3CrashFix.accepted. It must be taken over (no dialog) and the old file removed.
    Remove-Item "$lic\W3CrashFix.ini"
    Set-Content "$lic\W3CrashFix.accepted" $licId -Encoding ascii
    & "$lic\witcher3.exe" "$PWD\$lic\W3CrashFix.asi" heap | Out-Null
    $global:LASTEXITCODE = 0
    $ll = Get-Content "$lic\W3CrashFix.activity.log" -Raw
    if ($ll.Contains('license: asking')) { throw 'test_diag: the license was asked again although 1.5.0 had stored the acceptance in W3CrashFix.accepted' }
    if ((Test-Path "$lic\W3CrashFix.accepted") -or (IniVal "$lic\W3CrashFix.ini" 'LicenseAccepted') -ne $licId) { throw 'test_diag: W3CrashFix.accepted was not taken over into W3CrashFix.ini' }
    if ((IniVal "$lic\W3CrashFix.ini" 'OpenReportPage') -ne '1') { throw 'test_diag: W3CrashFix.ini does not show OpenReportPage=1' }
    # The Enter key: ignored in the first 3 seconds, then the same as the OK button (for players without a mouse).
    Remove-Item "$lic\W3CrashFix.ini"
    $enterOut = & "$lic\witcher3.exe" "$PWD\$lic\W3CrashFix.asi" enter
    if ($LASTEXITCODE) { throw "test_diag: the Enter key test failed (exit $LASTEXITCODE): $enterOut" }
    if ((IniVal "$lic\W3CrashFix.ini" 'LicenseAccepted') -ne $licId) { throw 'test_diag: Enter after 3 seconds did not accept the license' }
    # Decline: the mod's file is renamed and the settings file goes (here it holds a report id from an earlier acceptance).
    Set-Content "$lic\W3CrashFix.ini" "[W3CrashFix]`r`nReportId=0123456789ABCDEF" -Encoding ascii
    & "$lic\witcher3.exe" "$PWD\$lic\W3CrashFix.asi" decline | Out-Null
    if ($LASTEXITCODE) { throw "test_diag: Decline failed (exit $LASTEXITCODE)" }
    $ll = Get-Content "$lic\W3CrashFix.activity.log" -Raw
    if ((Test-Path "$lic\W3CrashFix.asi") -or -not (Test-Path "$lic\W3CrashFix.removed")) { throw 'test_diag: Decline did not rename the mod''s file' }
    if (Test-Path "$lic\W3CrashFix.ini") { throw 'test_diag: Decline did not remove W3CrashFix.ini' }
    if (-not $ll.Contains('license: declined - the mod''s file was renamed')) { throw 'test_diag: license log lacks the decline line' }
    if ($ll.Contains('hooks installed') -or $ll.Contains('crash handler')) { throw 'test_diag: the mod started working although the license was declined' }
    'Test passed: diagnostic, caller names, crash report, report id, in-game mods, settings file and license dialog'
} elseif ($code -eq 3) { 'Test passed: diagnostic (no D3D12 device here, caller names not tested)' }
else { throw "test_diag exited with $code`: $out" }

# Uninstall leaves no traces (Onno, 18 Nov 2026: "no traces should be left behind"): in a stand-in game folder with
# every file the mod and the installer can leave in bin\x64_dx12, "--uninstall" must leave only the game's own file.
$u = 'build\test\uninstall'; $ub = "$u\bin\x64_dx12"
Remove-Item $u -Recurse -Force -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Force "$ub\W3CrashFix crash report" | Out-Null
Copy-Item "$t\witcher3.exe" $ub
Copy-Item build\W3CrashFix.asi "$ub\W3CrashFix.asi"; Copy-Item build\W3CrashFix.asi "$ub\W3CrashFix.removed"
foreach ($n in 'W3CrashFix.ini', 'W3CrashFix.accepted', 'W3CrashFix.id', 'W3CrashFix.noreport', 'W3CrashFix-report.html', 'W3CrashFix.crash.log',
               'W3CrashFix crash report\W3CrashFix.crash.log', 'W3CrashFix crash report\W3CrashFix.crash.dmp', 'W3CrashFix.activity.log', 'W3CrashFix.activity.previous.log', 'W3CrashFix.log', 'W3CrashFix.previous.log', 'W3CrashFix-send-log.html') {
    Set-Content "$ub\$n" 'x' -Encoding ascii
}
$p = Start-Process build\W3CrashFix-Installer.exe -ArgumentList '--uninstall', "`"$PWD\$u`"" -Wait -PassThru -WindowStyle Hidden
if ($p.ExitCode) { throw "uninstall test: the installer's --uninstall failed (exit $($p.ExitCode))" }
$left = @(Get-ChildItem $ub -Force | Where-Object { $_.Name -ne 'witcher3.exe' } | ForEach-Object Name)
if ($left.Count) { throw "uninstall test: Uninstall left traces behind: $($left -join ', ')" }
Remove-Item $u -Recurse -Force
'Test passed: Uninstall leaves no traces'

# The windows after a crash, outside the game (see -WindowTest at the top). The stand-in game crashes while one of its
# own windows is frozen; the mod must open the folder window and the report page and then let the program end. If the
# program is still there after 30 seconds, the mod is waiting for something it must never wait for.
if ($WindowTest) {
    # Onno, 20 Nov 2026: "if you open windows and tabs during building, could you close them when you're done?" The test
    # opens a folder window on build\test\window\... and the report page in the default browser; both are closed again
    # at the end. The folder window is found by its folder; the browser tab as the active report-page tab right after
    # the test (see the clean-up below).
    $browsers = 'chrome', 'msedge', 'firefox', 'brave', 'opera'
    function Get-ReportTabWindows { @(Get-Process $browsers -ErrorAction SilentlyContinue | Where-Object { $_.MainWindowTitle -like 'W3CrashFix crash report*' }) }
    $wt = 'build\test\window'
    if (Test-Path $wt) { Remove-Item $wt -Recurse -Force }
    New-Item -ItemType Directory -Force $wt | Out-Null
    Copy-Item "$t\witcher3.exe", build\W3CrashFix.asi $wt
    Set-Content "$wt\W3CrashFix.ini" "[W3CrashFix]`r`nOpenReportPage=1" -Encoding ascii
    $proc = Start-Process "$PWD\$wt\witcher3.exe" -ArgumentList "`"$PWD\$wt\W3CrashFix.asi`"", 'crashwin' -PassThru -WindowStyle Hidden
    $ended = $proc.WaitForExit(30000)
    if ($ended -and $proc.ExitCode -eq 9) { throw 'window test: the frozen stand-in window was not visible, so the test proves nothing' }
    if (-not $ended) { Stop-Process -Id $proc.Id -Force; throw 'window test: the stand-in game was still there 30 seconds after its crash - the mod hangs in its crash handling' }
    $wl = Get-Content "$wt\W3CrashFix.activity.log" -Raw
    foreach ($need in '!!!! CRASH: exception 0xC0000005', 'crash report: opened the folder', 'crash report: saved and opened W3CrashFix-report.html', 'always on top: ') {
        if (-not $wl.Contains($need)) { throw "window test: the log lacks '$need'" }
    }
    if ($wl.Contains('took longer than 12 seconds')) { throw 'window test: opening the windows ran into the 12-second limit' }
    'Window test passed: with a frozen window of its own, the stand-in game ended by itself after its crash. Log lines:'
    ($wl -split "`r?`n" | Where-Object { $_ -match 'crash report:' }) | ForEach-Object { '  ' + $_ }
    # Clean up what the test opened.
    $full = (Resolve-Path $wt).Path; $closed = @()
    try {
        $sh = New-Object -ComObject Shell.Application
        foreach ($w in @($sh.Windows())) {
            $p = $null; try { $p = $w.Document.Folder.Self.Path } catch {}
            if ($p -and $p.StartsWith($full, [StringComparison]::OrdinalIgnoreCase)) { $w.Quit(); $closed += 'the folder window' }
        }
    } catch {}
    # The test's page opens as a new tab, and a new tab is the active one, so the browser window's title shows it.
    # Only that one active tab is closed (Ctrl+W once), even if an older report page is open in another tab
    # (Onno, 20 Nov 2026: "you didn't close the chrome tab that you opened during building").
    $ws = New-Object -ComObject WScript.Shell
    $b = Get-ReportTabWindows | Select-Object -First 1
    if ($b -and $ws.AppActivate($b.Id)) { Start-Sleep -Milliseconds 300; $ws.SendKeys('^w'); $closed += "the report page tab ($($b.ProcessName))" }
    if ($closed) { 'Closed after the window test: ' + ($closed -join ', ') }
}

# A release build must not contain the crash-test keys.
if (-not $CrashTest -and (Select-String -Path build\W3CrashFix.asi, build\dinput8.dll -Pattern 'crash keys are enabled' -Encoding ascii -Quiet)) {
    throw 'The built mod contains crash-test code, but -CrashTest was not given.'
}
# The mod that gets released has the license dialog, and is not the test variant of it.
if (Select-String -Path build\W3CrashFix.asi, build\dinput8.dll -Pattern 'license dialog test variant' -Encoding ascii -Quiet) {
    throw 'The built mod is the test variant of the license dialog; that must never be released.'
}
foreach ($f in 'build\W3CrashFix.asi', 'build\dinput8.dll') {
    if (-not (Select-String -Path $f -Pattern 'license: asking the user' -Encoding ascii -Quiet)) { throw "The license dialog is missing from $f." }
}
if ($CrashTest) { 'NOTE: this is a CRASH-TEST BUILD (Ctrl+Alt+Shift+F7 to F12 crash the game). Do not release it.' }

if ($Package) {
    $ver = (Select-String src\w3crashfix.c -Pattern '#define W3CF_VERSION "(.+)"').Matches[0].Groups[1].Value
    try { if (Test-Path dist) { Remove-Item dist -Recurse -Force -ErrorAction Stop } }
    catch { throw "Can't clear dist\ - is a zip open in 7-Zip or Explorer? ($($_.Exception.Message))" }
    $gitAuthor = 'Onno Struijk (Havoc104)'      # as written in the repository
    $utf8 = New-Object Text.UTF8Encoding $false

    function Zip($stage, $zip) { Compress-Archive -Path "$stage\*" -DestinationPath $zip; Remove-Item $stage -Recurse -Force }
    # README/LICENSE with the platform's author name
    function Docs($dir, $author) {
        New-Item -ItemType Directory -Force $dir | Out-Null
        foreach ($d in 'README.md', 'LICENSE') {
            [IO.File]::WriteAllText("$PWD\$dir\$d", [IO.File]::ReadAllText("$PWD\$d").Replace($gitAuthor, $author), $utf8)
        }
    }
    # installer whose file properties show the platform's author name
    function Installer($author, $out) {
        if ($author -eq $gitAuthor) { Copy-Item build\W3CrashFix-Installer.exe $out; return }
        $rc = [IO.File]::ReadAllText("$PWD\src\installer.rc").Replace($gitAuthor, $author)
        $rc = $rc.Replace('"installer.manifest"', '"../../src/installer.manifest"').Replace('"installer.ico"', '"../../src/installer.ico"').Replace('"../build/', '"../../build/')
        New-Item -ItemType Directory -Force build\variant | Out-Null
        [IO.File]::WriteAllText("$PWD\build\variant\installer.rc", $rc, $utf8)
        Run @cc @installerFlags -o $out src\installer.c build\variant\installer.rc @guiLibs
        Set-PeChecksum $out
    }
    function Pack($platform, $author, [switch]$InstallerAsZip) {
        $out = "dist\$platform"; New-Item -ItemType Directory -Force $out | Out-Null
        foreach ($kind in 'asi', 'standalone') {
            $stage = "$out\stage"
            New-Item -ItemType Directory -Force "$stage\bin\x64_dx12" | Out-Null
            Copy-Item $(if ($kind -eq 'asi') { 'build\W3CrashFix.asi' } else { 'build\dinput8.dll' }) "$stage\bin\x64_dx12\"
            Docs "$stage\W3CrashFix-docs" $author
            Zip $stage "$out\W3CrashFix-$ver-$kind.zip"
        }
        if ($InstallerAsZip) {
            $stage = "$out\stage"
            Docs $stage $author
            Installer $author "$stage\W3CrashFix-Installer.exe"
            Zip $stage "$out\W3CrashFix-$ver-installer.zip"
        } else {
            Installer $author "$out\W3CrashFix-$ver-Installer.exe"
        }
    }

    Pack github $gitAuthor
    $src = "dist\github\stage\W3CrashFix-$ver"   # source archive (what GitHub also generates from a tag)
    New-Item -ItemType Directory -Force $src | Out-Null
    Copy-Item src, .github, build.ps1, Install-W3CrashFix.ps1, README.md, LICENSE, .gitignore $src -Recurse
    Compress-Archive -Path $src -DestinationPath "dist\github\W3CrashFix-$ver-source.zip"
    Remove-Item dist\github\stage -Recurse -Force

    Pack nexus 'Onno Struijk (104Havoc)'

    Get-ChildItem dist -Recurse -File | Select-Object @{n = 'File'; e = { $_.FullName.Substring($PWD.Path.Length + 1) } }, Length
}
