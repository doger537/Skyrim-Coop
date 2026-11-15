param([string]$Launcher = '')
$ErrorActionPreference = 'Stop'
if ([string]::IsNullOrWhiteSpace($Launcher)) { $Launcher = Join-Path $PSScriptRoot 'Play-AE.ps1' }
$tools = Split-Path -Parent $Launcher
$fixture = Join-Path ([IO.Path]::GetTempPath()) ('truecoop-ae-launch-' + [guid]::NewGuid().ToString('N'))
$game = Join-Path $fixture 'game with spaces'
$data = Join-Path $game 'Data'
$runtime = Join-Path $data 'SkyrimTogetherReborn'
$backup = Join-Path $game 'TrueCoop-CommonSE-Backup'
$marker = Join-Path $game 'ae-fixture-launch.txt'
New-Item -ItemType Directory -Path $runtime -Force | Out-Null
Add-Type -TypeDefinition 'using System.Reflection; [assembly:AssemblyFileVersion("1.7.104.0")] public class AELaunchFixture { public static void Main() { System.IO.File.WriteAllText(System.IO.Path.Combine(System.Environment.CurrentDirectory,"ae-fixture-launch.txt"),System.Environment.CurrentDirectory+"\n"+(System.Environment.GetCommandLineArgs().Length-1)); } }' -OutputAssembly (Join-Path $game 'SkyrimSE.exe') -OutputType ConsoleApplication
Copy-Item -LiteralPath (Join-Path $game 'SkyrimSE.exe') -Destination (Join-Path $runtime 'SkyrimTogether.exe')
Copy-Item -LiteralPath (Join-Path $game 'SkyrimSE.exe') -Destination (Join-Path $runtime 'SkyrimTogetherServer.exe')
foreach ($name in @('Skyrim.esm','Update.esm','Dawnguard.esm','HearthFires.esm','Dragonborn.esm')) { [IO.File]::WriteAllText((Join-Path $data $name),'fixture master') }
[IO.File]::WriteAllText((Join-Path $data 'ccFixture.esl'),'fixture Creation')
$creation = Join-Path $data 'ccFixture.esl'
$creationHash = (Get-FileHash -LiteralPath $creation).Hash
$guardState = [pscustomobject]@{Running=$false}
# In-process test invocations override detection only within this test session.
# No production file is altered, and no real game or user configuration is used.
Set-Item Function:Get-Process -Value ({ if ($guardState.Running) { [pscustomobject]@{ ProcessName='SkyrimTogether'; Id=-1 } } }.GetNewClosure())
function Assert-Rejected([string]$Expected) {
    $reason = ''
    try { & $Launcher -GamePath $game -CheckOnly | Out-Null } catch { $reason = $_.Exception.Message }
    if (!$reason -or $reason -notlike ('*'+$Expected+'*')) { throw "Expected rejection containing '$Expected'; got '$reason'" }
}
function Write-Journal([string]$Name = 'ccFixture.esl', [string]$Root = $game) {
    New-Item -ItemType Directory -Path $backup -Force | Out-Null
    [pscustomobject]@{ Version=1; GamePath=$Root; Files=@([pscustomobject]@{Name=$Name; SHA256=$creationHash}) } | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath (Join-Path $backup 'manifest.json') -Encoding UTF8
}
& $Launcher -GamePath $game -CheckOnly | Out-Null
if ((Get-FileHash -LiteralPath $creation).Hash -ne $creationHash -or (Test-Path -LiteralPath $marker) -or (Test-Path -LiteralPath $backup)) { throw 'CheckOnly changed files or launched a process' }
Write-Output 'PASS: CheckOnly preserves one Creation and launches nothing; exactly 74 is not required'
Move-Item -LiteralPath $creation -Destination (Join-Path $fixture 'held-Creation.esl')
Assert-Rejected 'No active Creation plugins'
Move-Item -LiteralPath (Join-Path $fixture 'held-Creation.esl') -Destination $creation
Write-Output 'PASS: zero-Creation profile refused'
Move-Item -LiteralPath (Join-Path $runtime 'SkyrimTogether.exe') -Destination (Join-Path $fixture 'held-launcher.exe')
Assert-Rejected 'launcher is missing'
Move-Item -LiteralPath (Join-Path $fixture 'held-launcher.exe') -Destination (Join-Path $runtime 'SkyrimTogether.exe')
Write-Output 'PASS: missing native launcher refused'
Add-Type -TypeDefinition 'using System.Reflection; [assembly:AssemblyFileVersion("1.6.1170.0")] public class WrongAEVersionFixture { public static void Main() {} }' -OutputAssembly (Join-Path $fixture 'wrong-version.exe') -OutputType ConsoleApplication
Move-Item -LiteralPath (Join-Path $game 'SkyrimSE.exe') -Destination (Join-Path $fixture 'held-game.exe')
Copy-Item -LiteralPath (Join-Path $fixture 'wrong-version.exe') -Destination (Join-Path $game 'SkyrimSE.exe')
Assert-Rejected 'targets SkyrimSE.exe 1.7.104.0'
# These are only EXEs compiled above inside the isolated test fixture.
Remove-Item -LiteralPath (Join-Path $game 'SkyrimSE.exe')
Move-Item -LiteralPath (Join-Path $fixture 'held-game.exe') -Destination (Join-Path $game 'SkyrimSE.exe')
Write-Output 'PASS: unsupported runtime refused'
Write-Journal
Copy-Item -LiteralPath $creation -Destination (Join-Path $backup 'ccFixture.esl')
Assert-Rejected 'Restore-AE-Creations.cmd'
if ((Get-FileHash -LiteralPath $creation).Hash -ne $creationHash -or !(Test-Path -LiteralPath (Join-Path $backup 'ccFixture.esl'))) { throw 'Pending backup rejection moved files' }
Write-Output 'PASS: active CommonSE backup refused without auto-restore'
New-Item -ItemType Directory -Path (Join-Path $backup 'History\fixture') -Force | Out-Null
Move-Item -LiteralPath (Join-Path $backup 'ccFixture.esl') -Destination (Join-Path $backup 'History\fixture\ccFixture.esl')
& $Launcher -GamePath $game -CheckOnly | Out-Null
Write-Output 'PASS: restored journal with History duplicates does not block AE'
Move-Item -LiteralPath $creation -Destination (Join-Path $fixture 'held-Creation.esl')
Assert-Rejected 'missing from both'
Move-Item -LiteralPath (Join-Path $fixture 'held-Creation.esl') -Destination $creation
Write-Output 'PASS: journal cannot silently ignore a file missing from Data and canonical backup'
Write-Journal -Name '..\outside.esl'
Assert-Rejected 'Unsafe or duplicate'
if ((Get-FileHash -LiteralPath $creation).Hash -ne $creationHash) { throw 'Unsafe journal altered a file' }
Write-Output 'PASS: unsafe journal filename refused'
Write-Journal -Root (Join-Path $fixture 'another game')
Assert-Rejected 'another game directory'
Write-Output 'PASS: foreign-game journal refused'
Write-Journal
Move-Item -LiteralPath (Join-Path $backup 'manifest.json') -Destination (Join-Path $fixture 'held-manifest.json')
Copy-Item -LiteralPath $creation -Destination (Join-Path $backup 'ccFixture.esl')
Assert-Rejected 'Restore-AE-Creations.cmd'
Move-Item -LiteralPath (Join-Path $backup 'ccFixture.esl') -Destination (Join-Path $fixture 'untracked-copy.esl')
Move-Item -LiteralPath (Join-Path $fixture 'held-manifest.json') -Destination (Join-Path $backup 'manifest.json')
Write-Output 'PASS: untracked canonical backup blocks launch even without journal'
$guardState.Running = $true
$reason = ''
try { & $Launcher -GamePath $game | Out-Null } catch { $reason = $_.Exception.Message }
$guardState.Running = $false
if ($reason -notlike '*Close Skyrim*' -or (Test-Path -LiteralPath $marker)) { throw 'Running game guard did not prevent launch' }
Write-Output 'PASS: running-game guard prevents a second launch'
foreach ($name in @('Play-AE.ps1','Play-AE.cmd','Start-AE-Server.cmd','Restore-AE-Creations.cmd')) { Copy-Item -LiteralPath (Join-Path $tools $name) -Destination $runtime }
$recovery = Join-Path $runtime 'TrueCoop-Recovery'
New-Item -ItemType Directory -Path $recovery -Force | Out-Null
Copy-Item -LiteralPath (Join-Path $tools 'SharedContent.ps1') -Destination $recovery
# Child CMDs use fixture-only copies with detection stubbed, so a real game running
# on the development PC cannot interfere with tests of unrelated fake files.
$guard = 'Get-Process SkyrimSE,SkyrimTogether,SkyrimSELauncher -ErrorAction SilentlyContinue'
foreach ($copy in @((Join-Path $runtime 'Play-AE.ps1'),(Join-Path $recovery 'SharedContent.ps1'))) {
    $text = Get-Content -LiteralPath $copy -Raw
    if (!$text.Contains($guard)) { throw 'Expected process guard absent from fixture copy' }
    [IO.File]::WriteAllText($copy,$text.Replace($guard,'$false'))
}
function Run-Cmd([string]$Name) {
    $info = New-Object Diagnostics.ProcessStartInfo
    $info.FileName = $env:ComSpec
    $info.Arguments = '/d /c ""'+(Join-Path $runtime $Name)+'""'
    $info.WorkingDirectory = $env:SystemRoot
    $info.UseShellExecute = $false
    $info.CreateNoWindow = $true
    $info.RedirectStandardInput = $true
    $info.RedirectStandardOutput = $true
    $info.RedirectStandardError = $true
    $process = [Diagnostics.Process]::Start($info)
    $process.StandardInput.Close()
    $output = $process.StandardOutput.ReadToEnd()
    $errorText = $process.StandardError.ReadToEnd()
    $process.WaitForExit()
    $result = [pscustomobject]@{Exit=$process.ExitCode;Output=$output;Error=$errorText}
    $process.Dispose()
    return $result
}
$windowsPS = Join-Path $env:SystemRoot 'System32\WindowsPowerShell\v1.0\powershell.exe'
Push-Location $env:SystemRoot
try {
    & $windowsPS -NoProfile -ExecutionPolicy Bypass -File (Join-Path $runtime 'Play-AE.ps1') -CheckOnly | Out-Null
    if ($LASTEXITCODE -ne 0) { throw '-File default path discovery failed' }
} finally { Pop-Location }
Write-Output 'PASS: Windows PowerShell -File resolves default game path after binding from another directory'
$result = Run-Cmd 'Play-AE.cmd'
if ($result.Exit -ne 0 -or $result.Error) { throw ('Play CMD failed: '+$result.Output+$result.Error) }
$deadline = [DateTime]::UtcNow.AddSeconds(5)
while (!(Test-Path -LiteralPath $marker) -and [DateTime]::UtcNow -lt $deadline) { Start-Sleep -Milliseconds 50 }
if (!(Test-Path -LiteralPath $marker) -or (Get-Content -LiteralPath $marker -First 1) -ne $game -or (Get-FileHash -LiteralPath $creation).Hash -ne $creationHash) { throw 'Play CMD failed to launch fixture from game root or modified Creations' }
Write-Output 'PASS: actual Play-AE CMD launches fake EXE from game root and preserves Creations'
Move-Item -LiteralPath $creation -Destination (Join-Path $backup 'ccFixture.esl')
$result = Run-Cmd 'Play-AE.cmd'
if ($result.Exit -eq 0 -or $result.Error -notlike '*Restore-AE-Creations.cmd*') { throw 'Play CMD did not preserve pending-backup error exit code' }
Write-Output 'PASS: actual Play-AE CMD propagates rejection exit status'
$result = Run-Cmd 'Restore-AE-Creations.cmd'
if ($result.Exit -ne 0 -or $result.Error -or (Get-FileHash -LiteralPath $creation).Hash -ne $creationHash) { throw ('Restore CMD failed: '+$result.Output+$result.Error) }
Write-Output 'PASS: Restore-AE CMD invokes isolated recovery helper and restores fixture bytes'
New-Item -ItemType Directory -Path (Join-Path $runtime 'config') -Force | Out-Null
$config = Join-Path $runtime 'config\STServer.ini'
[IO.File]::WriteAllText($config,"[GameServer]`nuPort=23456`nsServerName=fixture`n")
$configHash = (Get-FileHash -LiteralPath $config).Hash
$result = Run-Cmd 'Start-AE-Server.cmd'
$serverMarker = Join-Path $runtime 'ae-fixture-launch.txt'
if ($result.Exit -ne 0 -or $result.Error -or !(Test-Path -LiteralPath $serverMarker) -or (Get-Content -LiteralPath $serverMarker -First 1) -ne $runtime -or (Get-Content -LiteralPath $serverMarker -Last 1) -ne '0' -or (Get-FileHash -LiteralPath $config).Hash -ne $configHash) { throw 'Server CMD changed arguments/configuration or used wrong working directory' }
Write-Output 'PASS: server CMD keeps native working directory, passes no overrides and preserves configuration'
Write-Output ('Fixture: '+$fixture)
