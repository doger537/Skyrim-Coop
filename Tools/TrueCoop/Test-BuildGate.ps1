# Checks that a missing build tool stops before configuration or compilation.
$ErrorActionPreference = 'Stop'
$repo = (Resolve-Path (Join-Path $PSScriptRoot '../..')).Path
$reportPath = Join-Path $repo 'build/truecoop/preflight.json'
$oldReport = $null
if (Test-Path -LiteralPath $reportPath) { $oldReport = [IO.File]::ReadAllBytes($reportPath) }
try {
    $missingTool = Join-Path $repo 'build/truecoop/intentionally-absent-xmake.exe'
    if (Test-Path -LiteralPath $missingTool) { throw 'Test fixture must be absent' }
    $shell = (Get-Process -Id $PID).Path
    $output = & $shell -NoProfile -File (Join-Path $PSScriptRoot 'Build.ps1') -CheckOnly -SkipUI -XmakePath $missingTool 2>&1
    if ($LASTEXITCODE -eq 0) { throw 'Gate accepted missing xmake' }
    $report = Get-Content -LiteralPath $reportPath -Raw | ConvertFrom-Json
    if ($report.Ready) { throw 'Report incorrectly claims readiness' }
    if (!($report.Missing -match 'xmake')) { throw 'Report omits missing tool' }
    if (($output | Out-String) -notmatch 'Missing prerequisites') { throw 'Failure did not explain prerequisites' }
    Write-Output 'PASS: missing xmake prevents build and produces an actionable report'
} finally {
    if ($null -ne $oldReport) { [IO.File]::WriteAllBytes($reportPath, $oldReport) }
}
