param([string]$Helper = '')
$ErrorActionPreference='Stop'
if ([string]::IsNullOrWhiteSpace($Helper)) { $Helper=Join-Path $PSScriptRoot 'SharedContent.ps1' }
$base=Join-Path ([IO.Path]::GetTempPath()) ('ae-fixture-'+[guid]::NewGuid().ToString('N'))
$data=Join-Path $base 'Data'
New-Item -ItemType Directory -Path $data -Force | Out-Null
function Write-Plugin([string]$Name,[string[]]$Masters=@()) {
 $payload=New-Object IO.MemoryStream
 $w=New-Object IO.BinaryWriter($payload)
 foreach($master in $Masters) { $b=[Text.Encoding]::ASCII.GetBytes($master+[char]0);$w.Write([Text.Encoding]::ASCII.GetBytes('MAST'));$w.Write([uint16]$b.Length);$w.Write($b) }
 $w.Flush()
 $f=[IO.File]::Create((Join-Path $data $Name));$h=New-Object IO.BinaryWriter($f)
 $h.Write([Text.Encoding]::ASCII.GetBytes('TES4'));$h.Write([uint32]$payload.Length);$h.Write((New-Object byte[] 16));$h.Write($payload.ToArray());$h.Dispose();$payload.Dispose()
}
foreach($n in @('Skyrim.esm','Update.esm','Dawnguard.esm','HearthFires.esm','Dragonborn.esm')){Write-Plugin $n}
Add-Type -TypeDefinition 'using System.Reflection; [assembly:AssemblyFileVersion("1.7.104.0")] public class Entry { public static void Main() { System.IO.File.WriteAllText(System.IO.Path.Combine(System.Environment.CurrentDirectory,"fake-launch.txt"),"started"); } }' -OutputAssembly (Join-Path $base 'SkyrimSE.exe') -OutputType ConsoleApplication
# Only fixture operations bypass process detection. Production helper always checks running games.
function Get-Process {}
Write-Plugin 'ccTest.esl'
[IO.File]::WriteAllText((Join-Path $data 'ccTest.bsa'),'fixture')
$sha=(Get-FileHash (Join-Path $data 'ccTest.bsa')).Hash
& $Helper -GamePath $base -Action Plan | Out-Null
if(!(Test-Path (Join-Path $data 'ccTest.esl'))){throw 'Plan mutated files'}
Write-Output 'PASS: plan is read-only'
$blocked=$false
try { & $Helper -GamePath $base -Action Enable -Launch | Out-Null } catch { $blocked=$true }
if(!$blocked -or !(Test-Path (Join-Path $data 'ccTest.esl'))){throw 'Missing launcher did not block before moving files'}
Write-Output 'PASS: missing launcher blocks before moving files'
& $Helper -GamePath $base -Action Enable | Out-Null
if((Test-Path (Join-Path $data 'ccTest.esl')) -or !(Test-Path (Join-Path $base 'TrueCoop-CommonSE-Backup/ccTest.bsa'))){throw 'Enable failed'}
Write-Output 'PASS: plugin and archive preserved outside Data'
& $Helper -GamePath $base -Action Enable | Out-Null
Write-Output 'PASS: repeated common-mode enable'
& $Helper -GamePath $base -Action Restore | Out-Null
if((Get-FileHash (Join-Path $data 'ccTest.bsa')).Hash -ne $sha){throw 'Hash mismatch'}
Write-Output 'PASS: restore is byte-identical'
$unsupported='ccTest - '+[char]0x0442+'.bsa'
[IO.File]::WriteAllText((Join-Path $data $unsupported),'fixture unsupported archive')
$blocked=$false
try { & $Helper -GamePath $base -Action Enable | Out-Null } catch { $blocked=$true }
if(!$blocked -or !(Test-Path (Join-Path $data 'ccTest.esl'))){throw 'Unsupported filename moved files'}
Remove-Item -LiteralPath (Join-Path $data $unsupported)
Write-Output 'PASS: unsupported filename is rejected before moving files'
Write-Plugin 'NeedsCC.esp' @('ccTest.esl')
$blocked=$false
try { & $Helper -GamePath $base -Action Enable | Out-Null } catch { $blocked=$true }
if(!$blocked -or !(Test-Path (Join-Path $data 'ccTest.esl'))){throw 'Dependency not blocked'}
Write-Output 'PASS: dependent plugin blocks all moves'
# Remove only the exact fixture file created above, inside the fixture Data directory.
Remove-Item -LiteralPath (Join-Path $data 'NeedsCC.esp')
& $Helper -GamePath $base -Action Enable | Out-Null
[IO.File]::WriteAllText((Join-Path $data 'ccTest.bsa'),'Steam update fixture')
$blocked=$false
try { & $Helper -GamePath $base -Action Restore | Out-Null } catch { $blocked=$true }
if(!$blocked -or !(Test-Path (Join-Path $base 'TrueCoop-CommonSE-Backup/ccTest.esl'))){throw 'Conflict not blocked before partial restore'}
Write-Output 'PASS: restore conflict preserves backup and prevents partial restore'
$before=(Get-FileHash (Join-Path $base 'TrueCoop-CommonSE-Backup/manifest.json')).Hash
$blocked=$false
try { & $Helper -GamePath $base -Action Enable | Out-Null } catch { $blocked=$true }
if(!$blocked -or (Get-FileHash (Join-Path $base 'TrueCoop-CommonSE-Backup/manifest.json')).Hash -ne $before){throw 'Changed files did not block enable before journal replacement'}
Write-Output 'PASS: changed duplicate blocks resume without altering journal'
# Replace only the deliberately altered fixture, then exercise identical duplicate recovery.
Copy-Item -LiteralPath (Join-Path $base 'TrueCoop-CommonSE-Backup/ccTest.bsa') -Destination (Join-Path $data 'ccTest.bsa') -Force
& $Helper -GamePath $base -Action Restore | Out-Null
& $Helper -GamePath $base -Action Restore | Out-Null
if((Get-FileHash (Join-Path $data 'ccTest.bsa')).Hash -ne $sha){throw 'Identical duplicate restore failed'}
Write-Output 'PASS: restore accepts identical duplicates and repeated restore'
& $Helper -GamePath $base -Action Enable | Out-Null
foreach($n in @('ccTest.esl','ccTest.bsa')){Copy-Item -LiteralPath (Join-Path $base ('TrueCoop-CommonSE-Backup/'+$n)) -Destination (Join-Path $data $n)}
Write-Plugin 'ccNew.esl'
& $Helper -GamePath $base -Action Enable | Out-Null
$state=Get-Content (Join-Path $base 'TrueCoop-CommonSE-Backup/manifest.json') -Raw | ConvertFrom-Json
if(@($state.Files).Count -ne 3 -or (Test-Path (Join-Path $data 'ccTest.esl')) -or !(Get-ChildItem (Join-Path $base 'TrueCoop-CommonSE-Backup/History') -Recurse -Filter 'ccTest.bsa')){throw 'Duplicate resume/new file merge failed'}
Write-Output 'PASS: reappeared files resume safely and new Creations merge into journal'
& $Helper -GamePath $base -Action Restore | Out-Null
Move-Item -LiteralPath (Join-Path $data 'ccTest.esl') -Destination (Join-Path $base 'TrueCoop-CommonSE-Backup/ccTest.esl')
& $Helper -GamePath $base -Action Enable | Out-Null
if(Test-Path (Join-Path $data 'ccTest.bsa')){throw 'Partial operation did not resume'}
& $Helper -GamePath $base -Action Restore | Out-Null
Write-Output 'PASS: interrupted move resumes and restores every file'
[IO.File]::WriteAllText((Join-Path $data 'ccNew.bsa'),'new fixture archive')
for($i=0;$i -lt 72;$i++) {
    $n='ccBulk'+$i.ToString('000')
    Write-Plugin ($n+'.esl')
    [IO.File]::WriteAllText((Join-Path $data ($n+'.bsa')),'fixture '+$i)
}
$bulk=@{}
foreach($f in Get-ChildItem -LiteralPath $data -File | Where-Object Name -like 'cc*'){$bulk[$f.Name]=(Get-FileHash $f.FullName).Hash}
if($bulk.Count -ne 148){throw 'Bulk fixture count wrong'}
& $Helper -GamePath $base -Action Enable | Out-Null
foreach($n in @($bulk.Keys | Select-Object -First 7)){Copy-Item -LiteralPath (Join-Path $base ('TrueCoop-CommonSE-Backup/'+$n)) -Destination (Join-Path $data $n)}
& $Helper -GamePath $base -Action Enable | Out-Null
if(@(Get-ChildItem -LiteralPath $data -File | Where-Object Name -like 'cc*').Count){throw 'Bulk resume left Creation files'}
& $Helper -GamePath $base -Action Restore | Out-Null
foreach($n in $bulk.Keys){if((Get-FileHash (Join-Path $data $n)).Hash -ne $bulk[$n]){throw 'Bulk restore hash mismatch'}}
Write-Output 'PASS: 74 Creation plugins / 148 files survive resume and byte-identical restore'
$entry = Join-Path $base 'entry point with spaces'
$entryData = Join-Path $entry 'Data'
$runtime = Join-Path $entryData 'SkyrimTogetherReborn'
New-Item -ItemType Directory -Path $runtime -Force | Out-Null
Copy-Item -LiteralPath (Join-Path $base 'SkyrimSE.exe') -Destination $entry
Copy-Item -LiteralPath (Join-Path $base 'SkyrimSE.exe') -Destination (Join-Path $runtime 'SkyrimTogether.exe')
foreach($name in @('Skyrim.esm','Update.esm','Dawnguard.esm','HearthFires.esm','Dragonborn.esm')) {
    Copy-Item -LiteralPath (Join-Path $data $name) -Destination $entryData
}
Copy-Item -LiteralPath (Join-Path $data 'ccTest.esl') -Destination $entryData
Copy-Item -LiteralPath (Join-Path $data 'ccTest.bsa') -Destination $entryData
Copy-Item -LiteralPath $Helper -Destination $runtime
# Only the fixture copy bypasses process detection: a real game may be open on the host.
$fixtureHelper=Join-Path $runtime 'SharedContent.ps1'
$fixtureCode=Get-Content -LiteralPath $fixtureHelper -Raw
$guard='Get-Process SkyrimSE,SkyrimTogether,SkyrimSELauncher -ErrorAction SilentlyContinue'
if(!$fixtureCode.Contains($guard)){throw 'Fixture process guard not found'}
[IO.File]::WriteAllText($fixtureHelper,$fixtureCode.Replace($guard,'$false'))
foreach($name in @('Play-CommonSE.cmd','Restore-Creations.cmd')) {
    Copy-Item -LiteralPath (Join-Path (Split-Path -Parent $Helper) $name) -Destination $runtime
}
$windowsPS=Join-Path $env:SystemRoot 'System32/WindowsPowerShell/v1.0/powershell.exe'
Push-Location $env:SystemRoot
try {
    & $windowsPS -NoProfile -ExecutionPolicy Bypass -File (Join-Path $runtime 'SharedContent.ps1') -Action Plan | Out-Null
    if($LASTEXITCODE -ne 0 -or !(Test-Path (Join-Path $entryData 'ccTest.esl'))){throw '-File default path regression'}
    Write-Output 'PASS: Windows PowerShell -File discovers game without -GamePath from another working directory'
    foreach($name in @('Play-CommonSE.cmd','Restore-Creations.cmd')) {
        $info=New-Object Diagnostics.ProcessStartInfo
        $info.FileName=$env:ComSpec
        $info.Arguments='/d /c ""'+(Join-Path $runtime $name)+'""'
        $info.WorkingDirectory=$env:SystemRoot
        $info.UseShellExecute=$false
        $info.CreateNoWindow=$true
        $info.RedirectStandardInput=$true
        $info.RedirectStandardOutput=$true
        $info.RedirectStandardError=$true
        $process=[Diagnostics.Process]::Start($info)
        $process.StandardInput.Close()
        $output=$process.StandardOutput.ReadToEnd()
        $errorOutput=$process.StandardError.ReadToEnd()
        $process.WaitForExit()
        if($process.ExitCode -ne 0 -or $errorOutput){throw "Batch failure: $name $output $errorOutput"}
        if($name -eq 'Play-CommonSE.cmd') {
            $deadline=[DateTime]::UtcNow.AddSeconds(5)
            while(!(Test-Path (Join-Path $entry 'fake-launch.txt')) -and [DateTime]::UtcNow -lt $deadline){Start-Sleep -Milliseconds 50}
            if(!(Test-Path (Join-Path $entry 'fake-launch.txt')) -or (Test-Path (Join-Path $entryData 'ccTest.esl'))){throw 'Batch launch did not prepare fixture and invoke fake launcher'}
            Write-Output 'PASS: actual Play CMD prepares fixture and launches fake executable with spaces in path'
        } else {
            if((Get-FileHash (Join-Path $entryData 'ccTest.bsa')).Hash -ne $sha){throw 'Batch restore hash mismatch'}
            Write-Output 'PASS: actual Restore CMD returns original bytes'
        }
        $process.Dispose()
    }
} finally { Pop-Location }
Write-Output ('Fixture: '+$base)

