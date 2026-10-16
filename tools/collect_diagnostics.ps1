param([string]$Root = $PSScriptRoot, [switch]$NoOpen)
$ErrorActionPreference = 'Stop'
$Root = (Resolve-Path -LiteralPath $Root).Path
$diagnostics = Join-Path $Root 'diagnostics'
$items = @()
if (Test-Path -LiteralPath $diagnostics) {
    $items += @(Get-ChildItem -LiteralPath $diagnostics -Directory -Filter 'replay-*' |
        Sort-Object LastWriteTime -Descending | Select-Object -First 3 |
        ForEach-Object { $_.FullName })
    $items += @(Get-ChildItem -LiteralPath $diagnostics -File -Filter 'performance-*.jsonl' |
        Sort-Object LastWriteTime -Descending | Select-Object -First 1 |
        ForEach-Object { $_.FullName })
}
$lastRun = Join-Path $Root 'last_run_report.json'
if (Test-Path -LiteralPath $lastRun) { $items += $lastRun }
$netLog = Join-Path $Root 'saves/netplay/net_diag.jsonl'
if (Test-Path -LiteralPath $netLog) { $items += $netLog }
if (!$items.Count) { throw 'No diagnostics found. Play with Crash diagnostics and replay enabled, quit, then try again.' }
$reports = Join-Path $Root 'reports'
[void][System.IO.Directory]::CreateDirectory($reports)
$stamp = Get-Date -Format 'yyyyMMdd-HHmmss-fff'
$archive = Join-Path $reports ('FZero-diagnostics-' + $stamp + '.zip')
Compress-Archive -LiteralPath $items -DestinationPath $archive -CompressionLevel Optimal
Write-Output ('Created ' + $archive)
Write-Output 'Send this ZIP with a description of the problem. It contains game memory, saves and local diagnostic paths. Nothing is uploaded automatically.'
if (!$NoOpen) {
    Start-Process -FilePath (Join-Path $env:SystemRoot 'explorer.exe') -ArgumentList @('/select,', ('"' + $archive + '"')) -WindowStyle Normal
}
