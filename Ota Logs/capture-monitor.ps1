param(
    [string]$Port = "COM6"
)

$ErrorActionPreference = "Stop"
$rawPath = Join-Path $PSScriptRoot "Log_File.txt"
$utf8NoBom = New-Object System.Text.UTF8Encoding($false)
$categories = @{
    "ota_check.txt" = '(?i)OTA:.*(Checking signed|Verified release|New version available|No newer release|Manifest|signature verification)'
    "ota_update.txt" = '(?i)(OTA:.*(New version available|Downloading|Chunk|Full firmware|Encrypted firmware installed|installation failed|verification failed)|esp_image:)'
    "ota_other.txt" = '(?i)(WIFI:|boot:|app_init:|New firmware passed startup checks|task_wdt|esp-tls|HTTP_CLIENT|rst:)'
}

Write-Host "Full monitor log: $rawPath"
Write-Host "Starting ESP-IDF monitor on $Port. Stop with Ctrl+]."

& idf.py -p $Port monitor 2>&1 | Tee-Object -FilePath $rawPath

if (-not (Test-Path -LiteralPath $rawPath) -or
    (Get-Item -LiteralPath $rawPath).Length -eq 0) {
    throw "The serial capture is empty. Check that idf.py monitor ran and produced output."
}

$lines = Get-Content -LiteralPath $rawPath
foreach ($entry in $categories.GetEnumerator()) {
    $outputPath = Join-Path $PSScriptRoot $entry.Key
    $matchingLines = @($lines | Where-Object { $_ -match $entry.Value })
    if ($matchingLines.Count -gt 0) {
        [System.IO.File]::WriteAllLines($outputPath, [string[]]$matchingLines, $utf8NoBom)
    } else {
        [System.IO.File]::WriteAllText($outputPath, "No matching events in this capture.`r`n", $utf8NoBom)
    }
}

Write-Host "Created Log_File.txt, ota_check.txt, ota_update.txt, and ota_other.txt in $PSScriptRoot"