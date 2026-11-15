[CmdletBinding()]
param(
    [string]$XmakePath = 'xmake',
    [ValidateSet('debug', 'releasedbg', 'release')][string]$Mode = 'releasedbg',
    [ValidateSet('latest', '1.6.1170')][string]$SkyrimVersion = 'latest',
    [switch]$CheckOnly,
    [switch]$SkipUI
)
$ErrorActionPreference = 'Stop'
$repo = (Resolve-Path (Join-Path $PSScriptRoot '../..')).Path
$logDir = Join-Path $repo 'build/truecoop'
$vsEnvironment = @{}
foreach ($name in @('VSInstallDir', 'VCInstallDir', 'VisualStudioVersion')) {
    $vsEnvironment[$name] = [Environment]::GetEnvironmentVariable($name, 'Process')
}
New-Item -ItemType Directory -Force -Path $logDir | Out-Null

function Invoke-Checked {
    param([string]$Program, [string[]]$Arguments, [string]$Log)
    & $Program @Arguments 2>&1 | Tee-Object -FilePath (Join-Path $logDir $Log)
    if ($LASTEXITCODE -ne 0) { throw "$Program failed ($LASTEXITCODE); see $logDir/$Log" }
}

Push-Location $repo
try {
    $missing = [System.Collections.Generic.List[string]]::new()
    $git = Get-Command git -ErrorAction SilentlyContinue
    $xmake = Get-Command $XmakePath -ErrorAction SilentlyContinue
    if (!$git) { $missing.Add('Git') }
    if (!$xmake) { $missing.Add('xmake 3.1.0 (pass -XmakePath)') }
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
    $vsPath = $null
    if (Test-Path -LiteralPath $vswhere) {
        $vsPath = & $vswhere -utf8 -latest -version '[17.0,18.0)' -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    }
    if (!$vsPath) { $missing.Add('Visual Studio 2022 C++ Build Tools (MSVC x64/x86)') }
    $sdk = Join-Path ${env:ProgramFiles(x86)} 'Windows Kits/10/Include'
    $sdkReady = @(Get-ChildItem -LiteralPath $sdk -Directory -ErrorAction SilentlyContinue | Where-Object {
        (Test-Path (Join-Path $_.FullName 'um/Windows.h')) -and (Test-Path (Join-Path $_.FullName 'ucrt/stdio.h'))
    }).Count -gt 0
    if (!$sdkReady) { $missing.Add('Windows 10/11 SDK headers') }
    $commit = $null
    $submodules = @()
    if ($git) {
        $commit = & $git.Source rev-parse HEAD
        if ($LASTEXITCODE -ne 0) { throw 'Cannot identify source revision' }
        $submodules = @(& $git.Source submodule status --recursive)
        if ($LASTEXITCODE -ne 0) { throw 'Cannot inspect submodules' }
        if ($submodules | Where-Object { $_ -match '^[-+U]' }) { $missing.Add('Pinned submodules: run git submodule update --init --recursive') }
    }
    if (!$SkipUI) {
        if (!(Get-Command node -ErrorAction SilentlyContinue)) { $missing.Add('Node.js (upstream CI: 20.x)') }
        if (!(Get-Command npm.cmd -ErrorAction SilentlyContinue)) { $missing.Add('npm.cmd') }
    }
    [ordered]@{
        Revision = $commit; Mode = $Mode; SkyrimVersion = $SkyrimVersion
        VisualStudio = $vsPath; WindowsSDK = $sdkReady
        Xmake = $(if ($xmake) { $xmake.Source } else { $null })
        Submodules = $submodules; Missing = @($missing.ToArray())
        Ready = ($missing.Count -eq 0)
    } | ConvertTo-Json -Depth 5 | Set-Content (Join-Path $logDir 'preflight.json') -Encoding UTF8
    if ($missing.Count) { throw ('Missing prerequisites: ' + ($missing -join '; ')) }
    if (!$CheckOnly) {
        # Bypass xmake's non-UTF-8 vswhere discovery for installation paths with Cyrillic.
        # VS batch scripts require the trailing separator on these variables.
        $env:VSInstallDir = $vsPath.TrimEnd('\') + '\'
        $env:VCInstallDir = (Join-Path $vsPath 'VC').TrimEnd('\') + '\'
        $env:VisualStudioVersion = '17.0'
        # Refresh configuration; the shared build identity uses git describe --tags --always.
        Invoke-Checked $xmake.Source @('config', '--clean', '--plat=windows', '--arch=x64', "--mode=$Mode", "--skyrim_version=$SkyrimVersion", '--yes') 'configure.log'
        $expectedVersion = & $git.Source describe --tags --always
        if ($LASTEXITCODE -ne 0) { throw 'Cannot determine build version' }
        $buildInfo = Get-Content -LiteralPath (Join-Path $repo 'build/BuildInfo.h') -Raw
        if (!$buildInfo.Contains('#define BUILD_COMMIT "' + $expectedVersion.Trim() + '"')) {
            throw 'Stale BUILD_COMMIT: refusing to build incompatible protocol under an old version'
        }
        Invoke-Checked $xmake.Source @('build', '--yes') 'native-build.log'
        Invoke-Checked $xmake.Source @('build', '--yes', 'TPTests') 'tests-build.log'
        Invoke-Checked $xmake.Source @('run', 'TPTests') 'tests.log'
        if (!$SkipUI) {
            Invoke-Checked 'npm.cmd' @('exec', '--yes', '--package=pnpm@9.15.9', '--', 'pnpm', '--prefix', 'Code/skyrim_ui', 'install', '--frozen-lockfile') 'ui-install.log'
            Invoke-Checked 'npm.cmd' @('exec', '--yes', '--package=pnpm@9.15.9', '--', 'pnpm', '--prefix', 'Code/skyrim_ui', 'deploy:production') 'ui-build.log'
        }
    }
} finally {
    foreach ($name in $vsEnvironment.Keys) {
        [Environment]::SetEnvironmentVariable($name, $vsEnvironment[$name], 'Process')
    }
    Pop-Location
}
