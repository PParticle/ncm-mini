[CmdletBinding()]
param(
    [ValidateSet('Debug', 'Release')]
    [string]$Configuration = 'Debug',
    [switch]$FrameworkDependent
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$output = Join-Path $root 'artifacts\publish'
$bandSource = Join-Path $root 'src\NCMMini.Band'
$hostProject = Join-Path $root 'src\NCMMini.Host\NCMMini.Host.csproj'

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
try {
    Remove-Item $output -Recurse -Force -ErrorAction Stop
} catch {
    Write-Warning 'The previous development DeskBand is still loaded. Restarting Explorer to release it.'
    Get-Process explorer -ErrorAction SilentlyContinue | Stop-Process -Force
    Start-Sleep -Seconds 1
    Remove-Item $output -Recurse -Force -ErrorAction SilentlyContinue
    Start-Process explorer.exe
    Start-Sleep -Seconds 2
}
New-Item $output -ItemType Directory -Force | Out-Null

$compiler = Get-Command g++.exe -ErrorAction SilentlyContinue
if (-not $compiler) {
    throw 'g++.exe was not found. Install mingw-winlibs with Scoop and reopen PowerShell.'
}

$commonFlags = @(
    '-O2',
    '-std=c++17',
    '-DUNICODE',
    '-D_UNICODE',
    '-municode',
    '-static',
    '-static-libgcc',
    '-static-libstdc++',
    '-lole32',
    '-luuid',
    '-ladvapi32',
    '-luser32',
    '-lgdi32'
)

& $compiler.Source '-shared' (Join-Path $bandSource 'NCMMiniBand.cpp') '-o' (Join-Path $output 'NCMMiniBand.dll') @commonFlags
if ($LASTEXITCODE -ne 0) { throw "DeskBand compilation failed with exit code $LASTEXITCODE." }

& $compiler.Source (Join-Path $bandSource 'NCMMiniBandCtl.cpp') '-o' (Join-Path $output 'NCMMiniBandCtl.exe') @commonFlags
if ($LASTEXITCODE -ne 0) { throw "DeskBand controller compilation failed with exit code $LASTEXITCODE." }

$publishArguments = @(
    'publish',
    $hostProject,
    '--configuration', $Configuration,
    '--runtime', 'win-x64',
    '--output', $output,
    '-p:PublishSingleFile=true',
    '-p:DebugType=embedded'
)
if ($FrameworkDependent) {
    $publishArguments += '--self-contained'
    $publishArguments += 'false'
} else {
    $publishArguments += '--self-contained'
    $publishArguments += 'true'
    $publishArguments += '-p:IncludeNativeLibrariesForSelfExtract=true'
}

& dotnet @publishArguments
if ($LASTEXITCODE -ne 0) { throw "Host publication failed with exit code $LASTEXITCODE." }

Copy-Item (Join-Path $PSScriptRoot 'install.ps1') $output
Copy-Item (Join-Path $PSScriptRoot 'uninstall.ps1') $output
Write-Host "NCM Mini build output: $output"
