[CmdletBinding()]
param([int]$Port = 38647)
$ErrorActionPreference = 'Stop'
if ($Port -lt 1024 -or $Port -gt 65535) { throw 'Invalid test port' }
$repo = (Resolve-Path (Join-Path $PSScriptRoot '../..')).Path
$workspace = (Resolve-Path (Join-Path $repo '../..')).Path
$built = Join-Path $repo 'build/windows/x64/releasedbg'
$fixture = Join-Path $workspace ('work/paired-checkpoint-test-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path (Join-Path $fixture 'config') -Force | Out-Null
foreach ($name in @('SkyrimTogetherServer.exe','STServer.dll')) { Copy-Item -LiteralPath (Join-Path $built $name) -Destination $fixture }
@"
[general]
bConsole=false
[LiveServices]
bAnnounceServer=false
[Gameplay]
bAutoPartyJoin=true
[Persistence]
sWorldPath=Data/TrueCoop/world.sqlite3
[GameServer]
uPort=$Port
sServerName=Isolated paired-checkpoint protocol test
"@ | Set-Content -LiteralPath (Join-Path $fixture 'config/STServer.ini') -Encoding ascii
$script:testServer = $null
$serverExe = Join-Path $fixture 'SkyrimTogetherServer.exe'
function Stop-TestServer {
    if ($script:testServer -and !$script:testServer.HasExited) {
        $current = Get-Process -Id $script:testServer.Id -ErrorAction Stop
        if ($current.Path -ne $serverExe) { throw 'Test server identity changed; refusing to stop another process' }
        Stop-Process -InputObject $current
        $current.WaitForExit()
    }
}
function Start-TestServer([string]$Label) {
    $script:testServer = Start-Process -FilePath $serverExe -WorkingDirectory $fixture -WindowStyle Hidden -RedirectStandardOutput (Join-Path $fixture "$Label.stdout.log") -RedirectStandardError (Join-Path $fixture "$Label.stderr.log") -PassThru
    $deadline = [DateTime]::UtcNow.AddSeconds(10)
    do {
        if ($script:testServer.HasExited) { throw "Test server exited: $fixture" }
        if (Get-NetUDPEndpoint -OwningProcess $script:testServer.Id -ErrorAction SilentlyContinue | Where-Object LocalPort -eq $Port) { return }
        Start-Sleep -Milliseconds 100
    } while ([DateTime]::UtcNow -lt $deadline)
    throw "Test server did not bind its own test port: $fixture"
}
function Run-Probe([string]$Label, [string[]]$Arguments) {
    & (Join-Path $built 'TrueCoopProtocolProbe.exe') "127.0.0.1:$Port" @Arguments *> (Join-Path $fixture "$Label.probe.log")
    Get-Content -LiteralPath (Join-Path $fixture "$Label.probe.log")
    if ($LASTEXITCODE -ne 0) { throw "Protocol probe failed: $fixture" }
}
try {
    $state = Join-Path $fixture 'synthetic-recovery.bin'
    Start-TestServer 'initial'
    Run-Probe 'checkpoint' @('--verify-checkpoints', $state, 'pending')
    Stop-TestServer
    Start-TestServer 'pending-restart'
    Run-Probe 'pending-restart' @('--verify-checkpoint-recovery', $state, 'pending')
    Stop-TestServer
    Start-TestServer 'completed-restart'
    Run-Probe 'completed-restart' @('--verify-checkpoint-recovery', $state, 'complete')
    Write-Output "PASS: isolated protocol/storage recovery; no Skyrim client was executed. Evidence: $fixture"
} finally { Stop-TestServer }
