# Build the unencrypted CIA (build/cia/gmud3ds-nocrypt.cia) from PowerShell.
#
# tools/cia/build_cia.sh needs the devkitPro MSYS2 environment (DEVKITARM,
# make, 3dsxtool, smdhtool). The `bash` on PATH under Windows is usually WSL,
# which has neither that toolchain nor the /g/... drive layout, so this wrapper
# launches C:\msys64\usr\bin\bash.exe with a login shell instead.
#
# Usage:
#   .\tools\cia\build_cia.ps1
#   $env:MSYS2_ROOT = 'D:\msys64'; .\tools\cia\build_cia.ps1

$ErrorActionPreference = 'Stop'

function Find-Msys2Bash {
    $roots = @()
    if ($env:MSYS2_ROOT) { $roots += $env:MSYS2_ROOT }
    $roots += @('C:\msys64', 'C:\devkitPro\msys2', 'C:\msys32')

    foreach ($root in $roots) {
        $candidate = Join-Path $root 'usr\bin\bash.exe'
        if (Test-Path -LiteralPath $candidate) {
            return (Resolve-Path -LiteralPath $candidate).Path
        }
    }
    throw "MSYS2 bash.exe not found. Set MSYS2_ROOT to your MSYS2 install (e.g. C:\msys64)."
}

function Find-Python {
    # Prefer the active conda env, then a real python.exe on PATH.
    # Skip the Microsoft Store stub: it only opens the Store, it cannot run scripts.
    if ($env:CONDA_PREFIX) {
        $candidate = Join-Path $env:CONDA_PREFIX 'python.exe'
        if (Test-Path -LiteralPath $candidate) { return $candidate }
    }
    foreach ($name in @('python.exe', 'python3.exe')) {
        $cmd = Get-Command $name -ErrorAction SilentlyContinue
        if ($cmd -and $cmd.Source -notlike '*\WindowsApps\*') {
            return $cmd.Source
        }
    }
    return $null
}

function Convert-ToMsysPath([string] $winPath) {
    # C:\Users\me\python.exe -> /c/Users/me/python.exe
    return '/' + $winPath.Substring(0, 1).ToLowerInvariant() +
        ($winPath.Substring(2) -replace '\\', '/')
}

$bash = Find-Msys2Bash
$projectRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..\..')).Path

# G:\hero -> /g/hero (MSYS2 drive mount)
$drive = $projectRoot.Substring(0, 1).ToLowerInvariant()
$unixRoot = '/' + $drive + ($projectRoot.Substring(2) -replace '\\', '/')

$python = Find-Python
if ($python) {
    # MSYS2's login shell has no python on PATH; hand the interpreter over.
    $pythonUnix = Convert-ToMsysPath $python
    $env:PYTHON = $pythonUnix
} else {
    $pythonUnix = '(not found)'
}

Write-Host "MSYS2   : $bash"
Write-Host "Project : $unixRoot"
Write-Host "Python  : $pythonUnix"
Write-Host ''

& $bash -lc "cd '$unixRoot' && bash tools/cia/build_cia.sh"
exit $LASTEXITCODE
