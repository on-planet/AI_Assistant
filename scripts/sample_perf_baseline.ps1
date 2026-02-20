param(
    [string]$LogPath = "./logs/perf.log",
    [string]$OutCsv = "./logs/perf_baseline.csv"
)

$ErrorActionPreference = "Stop"

if (-not (Test-Path $LogPath)) {
    Write-Error "Log file not found: $LogPath"
}

$lines = Get-Content -Path $LogPath

$rows = New-Object System.Collections.Generic.List[object]

foreach ($line in $lines) {
    # 支持形如：FPS=60.0 FrameTime=16.6 PartCount=24
    if ($line -match 'FPS\s*[=:]\s*([0-9]+(?:\.[0-9]+)?)' -and
        $line -match 'Frame(Time|Ms)\s*[=:]\s*([0-9]+(?:\.[0-9]+)?)' -and
        $line -match 'PartCount\s*[=:]\s*([0-9]+)') {

        $fps = [double]($Matches[1])

        $frameMatch = [regex]::Match($line, 'Frame(Time|Ms)\s*[=:]\s*([0-9]+(?:\.[0-9]+)?)')
        $frameMs = [double]($frameMatch.Groups[2].Value)

        $partMatch = [regex]::Match($line, 'PartCount\s*[=:]\s*([0-9]+)')
        $partCount = [int]($partMatch.Groups[1].Value)

        $rows.Add([PSCustomObject]@{
            FPS = $fps
            FrameTimeMs = $frameMs
            PartCount = $partCount
        }) | Out-Null
    }
}

if ($rows.Count -eq 0) {
    Write-Error "No perf samples found in log: $LogPath"
}

$avgFps = ($rows | Measure-Object FPS -Average).Average
$minFps = ($rows | Measure-Object FPS -Minimum).Minimum
$maxFps = ($rows | Measure-Object FPS -Maximum).Maximum

$avgFrame = ($rows | Measure-Object FrameTimeMs -Average).Average
$minFrame = ($rows | Measure-Object FrameTimeMs -Minimum).Minimum
$maxFrame = ($rows | Measure-Object FrameTimeMs -Maximum).Maximum

$avgPart = ($rows | Measure-Object PartCount -Average).Average
$minPart = ($rows | Measure-Object PartCount -Minimum).Minimum
$maxPart = ($rows | Measure-Object PartCount -Maximum).Maximum

$summary = [PSCustomObject]@{
    Samples = $rows.Count
    AvgFPS = [Math]::Round($avgFps, 3)
    MinFPS = [Math]::Round($minFps, 3)
    MaxFPS = [Math]::Round($maxFps, 3)
    AvgFrameTimeMs = [Math]::Round($avgFrame, 3)
    MinFrameTimeMs = [Math]::Round($minFrame, 3)
    MaxFrameTimeMs = [Math]::Round($maxFrame, 3)
    AvgPartCount = [Math]::Round($avgPart, 3)
    MinPartCount = $minPart
    MaxPartCount = $maxPart
}

$dir = Split-Path -Parent $OutCsv
if (-not [string]::IsNullOrWhiteSpace($dir) -and -not (Test-Path $dir)) {
    New-Item -ItemType Directory -Path $dir | Out-Null
}

$rows | Export-Csv -Path $OutCsv -NoTypeInformation -Encoding UTF8

Write-Host "=== Perf Baseline Summary ==="
$summary | Format-List
Write-Host "Samples CSV: $OutCsv"
