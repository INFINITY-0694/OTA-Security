# OTA Security Design

## Key roles

The project uses separate keys for separate jobs:

| Key | Location | Purpose |
|---|---|---|
| ECDSA-256 private signing key | Protected build or release computer | Signs the ESP-IDF application image |
| ECDSA-256 public verification data | ESP32 Secure Boot configuration | Verifies the application image |
| AES flash-encryption key | ESP32 eFuse/key storage | Encrypts firmware stored in that device's flash |

The ECDSA private key must never be stored in the ESP32, committed to Git, or
copied to the OTA server. The public verification data is not a secret. The
flash-encryption key is generated and protected by ESP-IDF; it must not be
hard-coded in `main/`.

## Generate the development signing key

Run this from an ESP-IDF PowerShell environment, outside the repository if
possible:

```powershell
.\generate_signing_key.ps1
```

Treat `secure_boot_signing_key.pem` as a private secret. The `.gitignore` rule
prevents common signing-key files from being committed, but always check
`git status` before publishing. Do not generate or flash a production key on
the current board until the development-board procedure below is complete.

The helper refuses to overwrite an existing key. You can choose another local
path with `-OutputPath`, but keep the key outside the repository when possible.

The ESP32 target in this project is classic `esp32`. Its Secure Boot V1
implementation supports ECDSA-256, not RSA-3072. The helper uses the supported
ECDSA-256 scheme. RSA-3072 would require hardware and a Secure Boot version
that support it; changing the key type in this project would therefore be
incorrect.

## What happens during a signed build

```text
application image
        |
        v
SHA-256 digest + ECDSA-256 private key
        |
        v
signed ESP-IDF image
```

During boot and OTA validation, ESP-IDF uses the trusted public verification
data to check the signature. The public key does not reveal the private key or
return a hash. Verification returns success or failure for the image.

## What happens with Flash Encryption

```text
firmware in ESP32 flash -- AES hardware --> encrypted flash contents
encrypted flash contents -- AES hardware --> instructions while running
```

The application does not manually decrypt the image. This avoids putting a
recoverable firmware decryption key in application code. Flash Encryption
protects firmware stored on the device; it is separate from HTTPS transport
protection and ECDSA image signing.

## Safe activation order

1. Use a dedicated development ESP32, not the only working board.
2. Back up the current source and record the board's target and partition table.
3. Generate the ECDSA-256 signing key with ESP-IDF's Secure Boot tooling. Keep the
   private key outside the repository.
4. Enable Secure Boot in `idf.py menuconfig` and build a signed image.
5. Flash and boot the signed image, then test a signed OTA update.
6. Test an interrupted update and confirm the previous image remains usable.
7. Enable Flash Encryption in development mode and repeat the OTA tests.
8. Only after recovery is proven should production eFuse settings be enabled.

Do not manually add security values to `sdkconfig`. ESP-IDF must generate and
validate the related configuration because several eFuse operations are
irreversible.

## Current implementation boundary

`main/ota_manager.c` intentionally does not implement ECDSA, SHA-256, or AES.
It passes the downloaded ESP-IDF image to `esp_ota_write()` and
`esp_ota_end()`. Secure Boot and Flash Encryption operate below this code in
the bootloader, OTA image format, and ESP32 hardware.

The current development configuration leaves Secure Boot and Flash Encryption
disabled until a board is selected for security testing.

## OTA rollback behavior

After an OTA reboot, the application checks whether ESP-IDF marked the new
image as `PENDING_VERIFY`. It confirms the image only after NVS and Wi-Fi
startup succeed. If the new image fails before confirmation, ESP-IDF can roll
back to the previous OTA slot when rollback is enabled in the security test
configuration.

## HTTPS server mode

`server.py` supports HTTPS when both environment variables are set. Keep the
certificate and server private key outside Git:

```powershell
$env:OTA_TLS_CERT_FILE = "C:\path\to\server.crt"
$env:OTA_TLS_KEY_FILE = "C:\path\to\server.key"
python server.py
```

The ESP32 must also be configured with the CA certificate that issued
`server.crt`; changing the server to HTTPS alone is not sufficient for secure
certificate validation. Until that CA is embedded in the ESP-IDF image, keep
the current HTTP URLs and do not claim that transport security is enabled.