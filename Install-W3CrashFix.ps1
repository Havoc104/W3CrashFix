# Installs / removes W3CrashFix in the DX12 bin folder of The Witcher 3: Wild Hunt Remastered. Pick one form:
#   .\Install-W3CrashFix.ps1              -> dinput8.dll   (standalone, no other mods needed)
#   .\Install-W3CrashFix.ps1 -Asi         -> W3CrashFix.asi (needs Ultimate ASI Loader; other ASI loaders are untested and not supported)
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
    # also what the license dialog can leave behind: the mod's file renamed after a decline, and the acceptance note
    foreach ($f in $dll, $asiFile, (Join-Path $bin 'W3CrashFix.removed')) { if (Test-Ours $f) { Remove-Item $f; "Removed $(Split-Path $f -Leaf)"; $removed++ } }
    foreach ($leaf in 'W3CrashFix.ini', 'W3CrashFix.accepted', 'W3CrashFix.id', 'W3CrashFix.noreport') {   # the mod's settings, in this version's and in earlier forms
        $acc = Join-Path $bin $leaf
        if (Test-Path $acc) { Remove-Item $acc; "Removed $leaf"; $removed++ }
    }
    # what a crash report left: the report page and the crash log (in its own folder, or next to the mod in earlier versions)
    foreach ($leaf in 'W3CrashFix-report.html', 'W3CrashFix.crash.log', 'W3CrashFix crash report\W3CrashFix.crash.log') {
        $cr = Join-Path $bin $leaf
        if (Test-Path -LiteralPath $cr) { Remove-Item -LiteralPath $cr; "Removed $leaf"; $removed++ }
    }
    $crDir = Join-Path $bin 'W3CrashFix crash report'
    if ((Test-Path -LiteralPath $crDir) -and -not (Get-ChildItem -LiteralPath $crDir -Force)) { Remove-Item -LiteralPath $crDir; 'Removed W3CrashFix crash report (folder)' }
    if (-not $removed) { 'Not installed.' }
    return
}

if ($Asi) {
    Copy-Item (Find-Source 'W3CrashFix.asi') $asiFile -Force
    "Installed W3CrashFix.asi in $bin"
    if (Test-Ours $dll) { Remove-Item $dll; 'Removed our dinput8.dll so Ultimate ASI Loader can use that name.' }
    elseif (-not (Test-Path $dll)) { 'NOTE: no dinput8.dll found. The ASI version needs Ultimate ASI Loader (x64) in this folder.' }
} else {
    if ((Test-Path $dll) -and -not (Test-Ours $dll)) {
        throw "A different dinput8.dll is already in $bin. If it is Ultimate ASI Loader, use -Asi instead."
    }
    Copy-Item (Find-Source 'dinput8.dll') $dll -Force
    "Installed dinput8.dll in $bin"
}
"After starting the game, $bin\W3CrashFix.activity.log should start with a line that says 'loaded'."
