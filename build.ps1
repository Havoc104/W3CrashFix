# Builds W3CrashFix with Zig (https://ziglang.org, any recent version; only `zig cc` is used).
#   .\build.ps1            -> build\dinput8.dll, build\W3CrashFix.asi, build\W3CrashFix-Installer.exe, unit test (runs it)
#   .\build.ps1 -Package   -> also creates the release files:
#        dist\github\  asi + standalone zips, Installer.exe, source zip   (author shown as on GitHub)
#        dist\nexus\   asi + standalone + installer zips                 (author shown as on Nexus Mods;
#                                                                          Nexus only accepts archives)
param([switch]$Package, [string]$Zig)
$ErrorActionPreference = 'Stop'
Set-Location $PSScriptRoot
if (-not $Zig) {
    $local = Get-ChildItem tools -Recurse -Filter zig.exe -ErrorAction SilentlyContinue | Select-Object -First 1
    $Zig = if ($local) { $local.FullName } else { 'zig' }
}
New-Item -ItemType Directory -Force build | Out-Null
$cc = @('cc', '-target', 'x86_64-windows-gnu', '-O2', '-Wall')
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

Run @cc -shared -o build\dinput8.dll src\w3crashfix.c src\dinput8.def -luser32 -lkernel32
Run @cc -shared -DW3_ASI -o build\W3CrashFix.asi src\w3crashfix.c src\w3crashfix_asi.def -luser32 -lkernel32
# installer embeds both files above (see src\installer.rc), so it is built after them
Run @cc @installerFlags -o build\W3CrashFix-Installer.exe src\installer.c src\installer.rc @guiLibs
Set-PeChecksum build\W3CrashFix-Installer.exe
Run @cc -o build\test_stub.exe src\test_stub.c
& .\build\test_stub.exe; if ($LASTEXITCODE) { throw 'unit test failed' }
'Built build\dinput8.dll, build\W3CrashFix.asi and build\W3CrashFix-Installer.exe'

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
