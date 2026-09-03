[CmdletBinding()]
param(
    [ValidateSet('Debug', 'Release')]
    [string]$Configuration = 'Debug'
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$output = Join-Path $root 'artifacts\publish'
$staging = Join-Path $root 'artifacts\publish.new'
$bandSource = Join-Path $root 'src\NCMMini.Band'
$hostSource = Join-Path $root 'src\NCMMini.HostCpp'

Get-Process NCMMini -ErrorAction SilentlyContinue | Stop-Process -Force
foreach ($controller in @(
    (Join-Path $output 'NCMMiniBandCtl.exe'),
    (Join-Path $env:LOCALAPPDATA 'NCM Mini\NCMMiniBandCtl.exe')
)) {
    if (Test-Path $controller) {
        & $controller hide 2>$null | Out-Null
    }
}
Start-Sleep -Milliseconds 300
Remove-Item $staging -Recurse -Force -ErrorAction SilentlyContinue
New-Item $staging -ItemType Directory -Force | Out-Null

$compiler = Get-Command g++.exe -ErrorAction SilentlyContinue
if (-not $compiler) {
    throw 'g++.exe was not found. Install mingw-winlibs with Scoop and reopen PowerShell.'
}

$commonFlags = @(
    '-O2',
    '-std=c++17',
    '-DUNICODE',
    '-D_UNICODE',
    '-static',
    '-static-libgcc',
    '-static-libstdc++'
)
$bandLibraries = @(
    '-lole32',
    '-luuid',
    '-ladvapi32',
    '-luser32',
    '-lgdi32'
)

& $compiler.Source '-shared' (Join-Path $bandSource 'NCMMiniBand.cpp') '-o' (Join-Path $staging 'NCMMiniBand.dll') @commonFlags @bandLibraries
if ($LASTEXITCODE -ne 0) { throw "DeskBand compilation failed with exit code $LASTEXITCODE." }

& $compiler.Source '-municode' (Join-Path $bandSource 'NCMMiniBandCtl.cpp') '-o' (Join-Path $staging 'NCMMiniBandCtl.exe') @commonFlags @bandLibraries
if ($LASTEXITCODE -ne 0) { throw "DeskBand controller compilation failed with exit code $LASTEXITCODE." }

$hostSources = @(
    (Join-Path $hostSource 'main.cpp'),
    (Join-Path $hostSource 'Host.cpp'),
    (Join-Path $hostSource 'Json.cpp'),
    (Join-Path $hostSource 'Media.cpp'),
    (Join-Path $hostSource 'PipeServer.cpp')
)
$hostLibraries = @('-lole32', '-luuid', '-luser32', '-lshell32', '-lwinhttp', '-lwindowscodecs')
& $compiler.Source '-municode' '-mwindows' @hostSources '-o' (Join-Path $staging 'NCMMini.exe') @commonFlags @hostLibraries
if ($LASTEXITCODE -ne 0) { throw "Native host compilation failed with exit code $LASTEXITCODE." }

if ($Configuration -eq 'Release') {
    & strip.exe (Join-Path $staging 'NCMMini.exe') (Join-Path $staging 'NCMMiniBand.dll') (Join-Path $staging 'NCMMiniBandCtl.exe')
}

Copy-Item (Join-Path $PSScriptRoot 'install.ps1') $staging
Copy-Item (Join-Path $PSScriptRoot 'uninstall.ps1') $staging
$restartExplorer = $false
if (Test-Path $output) {
    try {
        Remove-Item $output -Recurse -Force -ErrorAction Stop
    } catch {
        Write-Warning 'The previous development DeskBand is still loaded. Restarting Explorer to release it.'
        Get-Process explorer -ErrorAction SilentlyContinue | Stop-Process -Force
        Get-Process explorer -ErrorAction SilentlyContinue | Wait-Process -Timeout 5 -ErrorAction SilentlyContinue
        Remove-Item $output -Recurse -Force -ErrorAction Stop
        $restartExplorer = $true
    }
}
Move-Item $staging $output
if ($restartExplorer) {
    Start-Process explorer.exe
    Start-Sleep -Seconds 2
}
Write-Host "NCM Mini native build output: $output"
