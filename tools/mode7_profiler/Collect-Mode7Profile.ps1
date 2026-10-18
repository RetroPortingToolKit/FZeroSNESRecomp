[CmdletBinding()]
param([switch]$NoOpen)

$ErrorActionPreference = 'Stop'
$game = Join-Path $PSScriptRoot 'F-Zero Forever.exe'
$logs = Join-Path $PSScriptRoot 'diagnostics'
$reports = Join-Path $PSScriptRoot 'profile-results'
$stamp = Get-Date -Format 'yyyyMMdd-HHmmss'
$report = Join-Path $reports ("mode7-profile-$stamp-" + [guid]::NewGuid().ToString('N').Substring(0, 8))
$archive = "$report.zip"
if (-not (Test-Path -LiteralPath $game -PathType Leaf)) { throw 'F-Zero Forever.exe is missing. Extract the entire profiler ZIP first.' }

Write-Host 'F-Zero Mode 7 profiler'
Write-Host 'Use your usual graphics settings. Play a course that runs poorly for at least 30 seconds, then quit the game.'
Write-Host 'This records timing and hardware information locally. Nothing is uploaded.'
Write-Host 'Leave this window open; it will create a report ZIP when the game exits.'

$previous = @{}
if (Test-Path -LiteralPath $logs) {
    Get-ChildItem -LiteralPath $logs -Filter 'performance-*.jsonl' -File | ForEach-Object { $previous[$_.Name] = $true }
}
New-Item -ItemType Directory -Path $report -Force | Out-Null
# Only selected hardware fields: no username, computer name, serials or paths.
$hardware = [ordered]@{ schema = 1 }
try {
    $hardware.cpu = @(Get-CimInstance Win32_Processor | Select-Object Name, NumberOfCores, NumberOfLogicalProcessors, MaxClockSpeed)
    $hardware.graphics = @(Get-CimInstance Win32_VideoController | Select-Object Name, DriverVersion, DriverDate)
    $hardware.memory_bytes = (Get-CimInstance Win32_ComputerSystem).TotalPhysicalMemory
} catch {
    $hardware.collection_note = 'Windows hardware query unavailable; the game report still includes CPU and adapter information.'
}
$hardware | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath (Join-Path $report 'hardware.json') -Encoding UTF8

$process = Start-Process -FilePath $game -ArgumentList @('--profile-mode7', '--launcher') -WorkingDirectory $PSScriptRoot -PassThru
# Keep the process handle while it is alive. Windows PowerShell can otherwise
# return an unavailable ExitCode after Start-Process -Wait has reaped the child.
$null = $process.Handle
$process.WaitForExit()
$process.Refresh()
$newLogs = @()
if (Test-Path -LiteralPath $logs) {
    $newLogs = @(Get-ChildItem -LiteralPath $logs -Filter 'performance-*.jsonl' -File | Where-Object { -not $previous.ContainsKey($_.Name) })
}
if ($newLogs.Count -eq 0) {
    throw 'No timing report was created. Start a game from the launcher and quit after playing. Check that this extracted folder is writable.'
}
foreach ($log in $newLogs) { Copy-Item -LiteralPath $log.FullName -Destination $report }
& (Join-Path $PSScriptRoot 'Summarize-Mode7Profile.ps1') -InputDirectory $report -OutputFile (Join-Path $report 'summary.txt')
if ($null -ne $process.ExitCode -and $process.ExitCode -ne 0) {
    Add-Content -LiteralPath (Join-Path $report 'summary.txt') -Value "`r`nGame exited with code $($process.ExitCode); the recording may be incomplete."
}
$files = @(Get-ChildItem -LiteralPath $report -File | ForEach-Object FullName)
Compress-Archive -LiteralPath $files -DestinationPath $archive -CompressionLevel Optimal
Write-Host "Report ready: $archive"
Write-Host 'Send this report ZIP back with a short description of where you noticed the slowdown.'
if (-not $NoOpen) { Start-Process -FilePath 'explorer.exe' -ArgumentList ('/select,"' + $archive + '"') }
