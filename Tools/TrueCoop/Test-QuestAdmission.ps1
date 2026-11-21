[CmdletBinding()]
param([ValidateRange(1024,65535)][int]$Port = 38648)

$ErrorActionPreference = 'Stop'
$repo = (Resolve-Path (Join-Path $PSScriptRoot '../..')).Path
$built = Join-Path $repo 'build/windows/x64/releasedbg'
$fixture = Join-Path ([IO.Path]::GetTempPath()) ('truecoop-quest-admission-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path (Join-Path $fixture 'config') -Force | Out-Null
$server = Join-Path $fixture 'SkyrimTogetherServer.exe'
foreach ($name in @('SkyrimTogetherServer.exe','STServer.dll')) {
    Copy-Item -LiteralPath (Join-Path $built $name) -Destination $fixture
}
@"
[general]
bConsole=false
[LiveServices]
bAnnounceServer=false
[Persistence]
sWorldPath=Data/TrueCoop/world.sqlite3
[GameServer]
uPort=$Port
sServerName=Isolated quest admission test
"@ | Set-Content -LiteralPath (Join-Path $fixture 'config/STServer.ini') -Encoding ascii

function Test-ServerRun([string]$Label) {
    $process = Start-Process -FilePath $server -WorkingDirectory $fixture -WindowStyle Hidden -PassThru -RedirectStandardOutput (Join-Path $fixture "$Label.out.log") -RedirectStandardError (Join-Path $fixture "$Label.err.log")
    try {
        $deadline = [DateTime]::UtcNow.AddSeconds(10)
        do {
            if ($process.HasExited) { throw "Test server exited. Logs in $fixture" }
            $bound = Get-NetUDPEndpoint -OwningProcess $process.Id -LocalPort $Port -ErrorAction SilentlyContinue
            if (!$bound) { Start-Sleep -Milliseconds 100 }
        } while (!$bound -and [DateTime]::UtcNow -lt $deadline)
        if (!$bound) { throw "Test server did not bind its port. Logs in $fixture" }

        $result = & (Join-Path $built 'TrueCoopProtocolProbe.exe') "127.0.0.1:$Port" --verify-quest-policy
        $result | Set-Content -LiteralPath (Join-Path $fixture "$Label.probe.log")
        $passed = $result | Where-Object { $_ -match '^PASS: .* Revision \d+$' }
        if ($LASTEXITCODE -ne 0 -or !$passed) { throw "Quest admission probe failed. Logs in $fixture" }
        Write-Host $passed
        return ($passed -split ' ')[-1]
    } finally {
        if (!$process.HasExited) {
            $current = Get-Process -Id $process.Id
            if ($current.Path -ne $server) { throw 'Test server process changed identity' }
            Stop-Process -InputObject $current
            $process.WaitForExit()
        }
    }
}

$initial = Test-ServerRun 'new-campaign'
$restarted = Test-ServerRun 'restarted-campaign'
if ($initial -ne $restarted) { throw "Quest revision changed on restart. Logs in $fixture" }
Write-Output "PASS: the persisted campaign admitted both simulated clients again after restart at revision $restarted. Logs in $fixture"
