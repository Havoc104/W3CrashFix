# Installs / removes W3CrashFix in the DX12 bin folder of The Witcher 3: Wild Hunt Remastered. Pick one form:
#   .\Install-W3CrashFix.ps1              -> dinput8.dll   (standalone, no other mods needed)
#   .\Install-W3CrashFix.ps1 -Asi         -> W3CrashFix.asi (needs an ASI loader, e.g. Ultimate ASI Loader)
#   .\Install-W3CrashFix.ps1 -Uninstall   -> removes whichever of ours is installed
# -GamePath <folder> if the game is not in the default Steam library.
# Files are taken from build\ (developer build) or from the script's own folder (release zip).
param([switch]$Asi, [switch]$Uninstall,
      [string]$GamePath = 'C:\Program Files (x86)\Steam\steamapps\common\The Witcher 3')
$ErrorActionPreference = 'Stop'
$bin = Join-Path $GamePath 'bin\x64_dx12'
if (-not (Test-Path (Join-Path $bin 'witcher3.exe'))) { throw "witcher3.exe not found in $bin - pass -GamePath <game folder>." }
$dll = Join-Path $bin 'dinput8.dll'
$asiFile = Join-Path $bin 'W3CrashFix.asi'
if (Get-Process witcher3 -ErrorAction SilentlyContinue) { throw 'Close The Witcher 3: Wild Hunt Remastered first.' }

# ours = W3CrashFix, or its pre-release name W3HeapProbe
function Test-Ours($path) { (Test-Path $path) -and (Select-String -Path $path -Pattern 'W3CrashFix|W3HeapProbe' -Quiet) }
function Find-Source($name) {
    foreach ($d in (Join-Path $PSScriptRoot 'build'), $PSScriptRoot, (Join-Path $PSScriptRoot 'bin\x64_dx12')) {
        $f = Join-Path $d $name; if (Test-Path $f) { return $f }
    }
    throw "$name not found next to this script (run build.ps1 first?)"
}

if ($Uninstall) {
    $removed = 0
    foreach ($f in $dll, $asiFile) { if (Test-Ours $f) { Remove-Item $f; "Removed $(Split-Path $f -Leaf)"; $removed++ } }
    if (-not $removed) { 'Not installed.' }
    return
}

if ($Asi) {
    Copy-Item (Find-Source 'W3CrashFix.asi') $asiFile -Force
    "Installed W3CrashFix.asi in $bin"
    if (Test-Ours $dll) { Remove-Item $dll; 'Removed our dinput8.dll so an ASI loader can use that name.' }
    elseif (-not (Test-Path $dll)) { 'NOTE: no dinput8.dll ASI loader found. Make sure an ASI loader (e.g. Ultimate ASI Loader) is installed in this folder.' }
} else {
    if ((Test-Path $dll) -and -not (Test-Ours $dll)) {
        throw "A different dinput8.dll (probably an ASI loader) is already in $bin - use -Asi instead."
    }
    Copy-Item (Find-Source 'dinput8.dll') $dll -Force
    "Installed dinput8.dll in $bin"
}
"After starting the game, $bin\W3CrashFix.log should contain two lines with 'ACTIVE'."
