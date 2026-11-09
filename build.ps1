# Builds W3CrashFix with Zig (https://ziglang.org, any recent version; only `zig cc` is used).
#   .\build.ps1            -> build\dinput8.dll, build\W3CrashFix.asi, unit test (runs it)
#   .\build.ps1 -Package   -> also creates dist\ zips for a release
param([switch]$Package, [string]$Zig)
$ErrorActionPreference = 'Stop'
Set-Location $PSScriptRoot
if (-not $Zig) {
    $local = Get-ChildItem tools -Recurse -Filter zig.exe -ErrorAction SilentlyContinue | Select-Object -First 1
    $Zig = if ($local) { $local.FullName } else { 'zig' }
}
New-Item -ItemType Directory -Force build | Out-Null
$cc = @('cc', '-target', 'x86_64-windows-gnu', '-O2', '-Wall')
function Run { & $Zig @args; if ($LASTEXITCODE) { throw "zig failed: $args" } }

Run @cc -shared -o build\dinput8.dll src\w3crashfix.c src\dinput8.def -luser32 -lkernel32
Run @cc -shared -DW3_ASI -o build\W3CrashFix.asi src\w3crashfix.c src\w3crashfix_asi.def -luser32 -lkernel32
Run @cc -o build\test_stub.exe src\test_stub.c
& .\build\test_stub.exe; if ($LASTEXITCODE) { throw 'unit test failed' }
'Built build\dinput8.dll and build\W3CrashFix.asi'

if ($Package) {
    $ver = (Select-String src\w3crashfix.c -Pattern '#define W3CF_VERSION "(.+)"').Matches[0].Groups[1].Value
    Remove-Item dist -Recurse -Force -ErrorAction SilentlyContinue
    $docs = 'README.md', 'LICENSE'
    foreach ($kind in 'standalone', 'asi') {
        $stage = "dist\stage-$kind"
        New-Item -ItemType Directory -Force "$stage\bin\x64_dx12" | Out-Null
        $file = if ($kind -eq 'asi') { 'build\W3CrashFix.asi' } else { 'build\dinput8.dll' }
        Copy-Item $file "$stage\bin\x64_dx12\"
        New-Item -ItemType Directory -Force "$stage\W3CrashFix-docs" | Out-Null
        Copy-Item $docs "$stage\W3CrashFix-docs\"
        Compress-Archive -Path "$stage\*" -DestinationPath "dist\W3CrashFix-$ver-$kind.zip"
        Remove-Item $stage -Recurse -Force
    }
    # source archive (what GitHub would also generate from a tag)
    $src = "dist\stage-src\W3CrashFix-$ver"
    New-Item -ItemType Directory -Force $src | Out-Null
    Copy-Item src, build.ps1, Install-W3CrashFix.ps1, README.md, LICENSE, .gitignore $src -Recurse
    Compress-Archive -Path $src -DestinationPath "dist\W3CrashFix-$ver-source.zip"
    Remove-Item dist\stage-src -Recurse -Force
    Get-ChildItem dist | Select-Object Name, Length
}
