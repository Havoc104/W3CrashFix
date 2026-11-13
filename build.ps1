# Builds W3CrashFix with Zig (https://ziglang.org, any recent version; only `zig cc` is used).
#   .\build.ps1            -> build\dinput8.dll, build\W3CrashFix.asi, build\W3CrashFix-Installer.exe
#   .\build.ps1 -Package   -> also creates the release files:
#        dist\github\  asi + standalone zips, Installer.exe, source zip   (author shown as on GitHub)
#        dist\nexus\   asi + standalone + installer zips                 (author shown as on Nexus Mods;
#                                                                          Nexus only accepts archives)
#   .\build.ps1 -CrashTest  -> TEST BUILD with keys that crash the game on purpose (never for release; see w3crashfix.c)
param([switch]$Package, [switch]$CrashTest, [string]$Zig)
$ErrorActionPreference = 'Stop'
Set-Location $PSScriptRoot
if (-not $Zig) {
    $local = Get-ChildItem tools -Recurse -Filter zig.exe -ErrorAction SilentlyContinue | Select-Object -First 1
    $Zig = if ($local) { $local.FullName } else { 'zig' }
}
New-Item -ItemType Directory -Force build | Out-Null
if ($Package -and $CrashTest) { throw 'A crash-test build must never be packaged for release.' }
$cc = @('cc', '-target', 'x86_64-windows-gnu', '-O2', '-Wall')
[string[]]$modFlags = @(); if ($CrashTest) { $modFlags = @('-DW3_CRASHTEST') }
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
'Built build\dinput8.dll, build\W3CrashFix.asi and build\W3CrashFix-Installer.exe'

# Test: a stand-in "witcher3.exe" that is not the game (src\test_diag.c). The mod must leave it unpatched, describe it
# in 'diag:' lines, and - if this PC has a D3D12 device - name the caller of a descriptor heap.
$t = 'build\test\diag'
New-Item -ItemType Directory -Force $t | Out-Null
Remove-Item "$t\*.log" -ErrorAction SilentlyContinue
Run @cc -municode -o "$t\witcher3.exe" src\test_diag.c -ld3d12 -ldxguid
Copy-Item build\W3CrashFix.asi $t -Force
$out = & "$t\witcher3.exe" "$PWD\$t\W3CrashFix.asi" heap
$code = $LASTEXITCODE
$log = if (Test-Path "$t\W3CrashFix.log") { Get-Content "$t\W3CrashFix.log" -Raw } else { '' }
foreach ($need in 'FIX DISABLED', 'diag: 4 of 5 functions located', 'ensure: same code found 1 time') {
    if (-not $log.Contains($need)) { throw "test_diag: log lacks '$need'" }
}
if ($log.Contains('ACTIVE')) { throw 'test_diag: the mod patched a program that is not the game' }
if ($code -eq 0) {
    if ($log -notmatch 'caller=witcher3\.exe\+0x[0-9A-F]+') { throw 'test_diag: heap caller was not named' }
    if ($log -notmatch 'module: witcher3\.exe = ') { throw 'test_diag: module path line missing' }
    # Same program run from a folder laid out like NVIDIA's downloaded DLSS files: the feature folder must show.
    $ngx = "$t\NGX\models\dlssg\versions\1\files"
    New-Item -ItemType Directory -Force $ngx | Out-Null
    Remove-Item "$ngx\*.log" -ErrorAction SilentlyContinue
    Copy-Item "$t\witcher3.exe", "$t\W3CrashFix.asi" $ngx -Force
    & "$ngx\witcher3.exe" "$PWD\$ngx\W3CrashFix.asi" heap | Out-Null
    $log2 = if (Test-Path "$ngx\W3CrashFix.log") { Get-Content "$ngx\W3CrashFix.log" -Raw } else { '' }
    if ($log2 -notmatch 'caller=dlssg\\witcher3\.exe\+0x[0-9A-F]+') { throw 'test_diag: feature folder missing from the caller name' }
    # Crash report: the same program crashes on purpose. W3CrashFix.noreport keeps the report page from opening here.
    Remove-Item "$t\W3CrashFix.crash.log", "$t\W3CrashFix-report.html" -ErrorAction SilentlyContinue
    Set-Content "$t\W3CrashFix.noreport" '' -Encoding ascii
    & "$t\witcher3.exe" "$PWD\$t\W3CrashFix.asi" crash | Out-Null
    if ($LASTEXITCODE -eq 0) { throw 'test_diag: the crash test did not crash' }
    $global:LASTEXITCODE = 0                                  # the crash was the point of this test
    $cl = if (Test-Path "$t\W3CrashFix.crash.log") { Get-Content "$t\W3CrashFix.crash.log" -Raw } else { '' }
    $rp = if (Test-Path "$t\W3CrashFix-report.html") { Get-Content "$t\W3CrashFix-report.html" -Raw } else { '' }
    foreach ($need in 'W3CrashFix crash report', 'Crash: exception 0xC0000005 (access violation, writing address 0x0) at witcher3.exe+0x', 'Verdict: ', '!!!! CRASH: exception 0xC0000005', 'not opened because W3CrashFix.noreport exists') {
        if (-not $cl.Contains($need) -and -not (Get-Content "$t\W3CrashFix.log" -Raw).Contains($need)) { throw "test_diag: crash log lacks '$need'" }
    }
    foreach ($need in 'Report on GitHub', 'Copy summary and open Nexus Mods', 'issues/new?title=Crash%20report', 'W3CrashFix.crash.log') {
        if (-not $rp.Contains($need)) { throw "test_diag: report page lacks '$need'" }
    }
    Remove-Item "$t\W3CrashFix.noreport"
    'Test passed: diagnostic, caller names and crash report'
} elseif ($code -eq 3) { 'Test passed: diagnostic (no D3D12 device here, caller names not tested)' }
else { throw "test_diag exited with $code`: $out" }

# A release build must not contain the crash-test keys.
if (-not $CrashTest -and (Select-String -Path build\W3CrashFix.asi, build\dinput8.dll -Pattern 'crash keys are enabled' -Encoding ascii -Quiet)) {
    throw 'The built mod contains crash-test code, but -CrashTest was not given.'
}
if ($CrashTest) { 'NOTE: this is a CRASH-TEST BUILD (Ctrl+Alt+Shift+F7 / F8 crash the game). Do not release it.' }

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
        $rc = $rc.Replace('"installer.manifest"', '"../../src/installer.manifest"').Replace('"../build/', '"../../build/')
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
