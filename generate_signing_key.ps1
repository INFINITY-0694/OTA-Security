param(
    [string]$OutputPath = "secure_boot_signing_key.pem"
)

$espsecurePath = Join-Path $env:IDF_PATH "components\esptool_py\esptool\espsecure.py"

if (-not $env:IDF_PATH -or -not (Test-Path $espsecurePath)) {
    throw "IDF_PATH is not set to a valid ESP-IDF installation."
}

if (Test-Path $OutputPath) {
    throw "Refusing to overwrite existing signing key: $OutputPath"
}

python $espsecurePath generate-signing-key --version 1 --scheme ecdsa256 $OutputPath
if ($LASTEXITCODE -ne 0) {
    throw "ESP-IDF signing-key generation failed."
}

Write-Host "Created ECDSA-256 Secure Boot V1 signing key: $OutputPath"
Write-Host "Keep this private key outside Git and never copy it to the ESP32."