param([string]$Helper = (Join-Path $PSScriptRoot 'SharedContent.ps1'))
$ErrorActionPreference='Stop'
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
Add-Type -TypeDefinition 'using System.Reflection; [assembly:AssemblyFileVersion("1.7.104.0")] public class Entry { public static void Main() {} }' -OutputAssembly (Join-Path $base 'SkyrimSE.exe') -OutputType ConsoleApplication
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
Write-Output ('Fixture: '+$base)

