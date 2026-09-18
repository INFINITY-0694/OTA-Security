# ESP32 Secure OTA Project - Product Requirements Document

## 1. Document purpose

This document is the handoff specification for the ESP32 Secure OTA project. It describes the current implementation, the intended production security architecture, file ownership, constraints, and the work remaining before cryptographic OTA can be claimed as complete.

The project is an ESP-IDF application for a classic ESP32 target. It updates firmware over Wi-Fi from a local development server today and has a Vercel dashboard/API path for future hosted releases.

## 2. Product goal

Allow an ESP32 to safely discover, download, validate, install, and boot a newer firmware image without a USB cable for every release.

The production goal is:

- Firmware authenticity: only authorized firmware can boot.
- Firmware confidentiality at rest: firmware stored in ESP32 flash is protected.
- Transport confidentiality and server authentication: firmware is transferred over validated HTTPS.
- Recovery: a failed new image does not permanently remove the last working image.
- Controlled release management: only an authenticated administrator can publish a firmware release.

## 3. Current verified status

### Implemented and hardware-tested

- Wi-Fi station connection with retry handling.
- Version polling every 30 seconds.
- Semantic `major.minor.patch` comparison.
- Local server endpoints `GET /version` and `GET /update`.
- HTTP response and content-length checks.
- Complete-download check.
- Writing to the inactive OTA partition.
- ESP-IDF image validation through `esp_ota_end()`.
- Boot-partition selection and reboot.
- Rollback confirmation after NVS and Wi-Fi startup.
- OTA upgrade demonstrated from firmware `5.0.0` to `6.0.0`.
- Two OTA application slots in the partition table.
- Vercel dashboard repository and custom domain deployment path.

### Not active yet

- HTTPS and CA certificate validation in the ESP32 firmware.
- ESP-IDF Secure Boot V1.
- ECDSA-256 signed application images.
- ESP32 hardware Flash Encryption.
- Cryptographic release verification tests.
- Production key provisioning.

The current development configuration uses HTTP and leaves Secure Boot and Flash Encryption disabled. Do not describe the current system as cryptographically secure OTA yet.

## 4. Users and actors

| Actor | Responsibility |
|---|---|
| Developer/release operator | Builds firmware, reviews it, and publishes a version. |
| Dashboard administrator | Authenticates to upload and publish a binary. |
| ESP32 device | Polls the version endpoint and installs a newer image. |
| OTA service | Stores release metadata and serves the version and binary. |
| ESP-IDF bootloader/hardware | Performs image/boot verification and, when enabled, encryption operations. |

## 5. Current device workflow

1. `main/main.c` initializes NVS.
2. `main/wifi_manager.c` connects to the configured Wi-Fi network.
3. `main/main.c` checks whether the running image is `PENDING_VERIFY` and confirms it after startup succeeds.
4. `main/ota_manager.c` starts the OTA task.
5. Every 30 seconds, the device requests `CONFIG_OTA_VERSION_URL`.
6. The device accepts an update only when the server semantic version is newer.
7. The device requests `CONFIG_OTA_FIRMWARE_URL`.
8. It requires HTTP 200 and a positive content length.
9. It writes the response to `esp_ota_get_next_update_partition(NULL)`.
10. It aborts on a short read, write error, or incomplete response.
11. `esp_ota_end()` validates the image.
12. `esp_ota_set_boot_partition()` selects the new slot.
13. The device reboots and confirms the image on successful startup.

## 6. Current source map

| Path | Responsibility |
|---|---|
| `main/main.c` | Application boot, NVS, Wi-Fi startup, pending-image confirmation. |
| `main/wifi_manager.c` | Wi-Fi station setup, retries, connection logs, IP reporting. |
| `main/ota_manager.c` | Version polling, version comparison, firmware download, OTA write, validation, reboot. |
| `main/Kconfig.projbuild` | Wi-Fi credentials and version/firmware URL settings. |
| `partitions.csv` | NVS, OTA data, `ota_0`, and `ota_1` layout. |
| `sdkconfig.defaults` | Custom partition table, 4 MB flash, rollback enablement. |
| `CMakeLists.txt` | ESP-IDF project name and firmware version. |
| `server.py` | Local development version and firmware HTTP server; optional TLS mode. |
| `server_version.txt` | Local server's advertised version. |
| `dashboard/index.html` | Hosted dashboard UI. |
| `dashboard/app.js` | Dashboard version display and release upload request. |
| `dashboard/api/release.js` | Authenticated Vercel release upload. |
| `dashboard/api/version.js` | Public version response from release metadata. |
| `dashboard/api/update.js` | Public firmware binary response. |
| `dashboard/README.md` | Vercel Blob and environment setup. |
| `SECURITY.md` | Security design, key roles, and safe activation order. |
| `generate_signing_key.ps1` | ECDSA-256 Secure Boot V1 development key helper. |

## 7. Release requirements

A release must contain:

- A valid semantic version, for example `6.1.0`.
- A firmware binary built for the same target and partition layout.
- A recorded binary size.
- A SHA-256 digest for audit and integrity metadata.
- A publication state.
- A release timestamp.
- A signature once Secure Boot signing is activated.

The server version must match the image's embedded application version. Publishing a text version that is newer than the binary causes repeated downloads of an image that may still report the old version.

## 8. Cryptography requirements and discussion starting point

### 8.1 Firmware authenticity

Use the ESP32/ESP-IDF native path:

```text
application image -> SHA-256 digest -> ECDSA-256 signature
ESP32 Secure Boot V1 -> verifies trusted signature during boot/OTA image validation
```

The private signing key stays on the protected release computer. The ESP32 stores trusted public verification data through the Secure Boot configuration. The private key must never be stored in the firmware, dashboard, OTA server, or Git repository.

Important platform constraint: classic ESP32 Secure Boot V1 is designed for ECDSA-256. RSA is not the correct Secure Boot choice for this target. A custom RSA signature layer would be separate from ESP-IDF Secure Boot and would require its own verification, packaging, key storage, and failure policy.

### 8.2 Firmware encryption at rest

Use ESP32 hardware Flash Encryption. The device manages the protected encryption key and decrypts flash internally while executing.

```text
firmware written to flash -> hardware-backed AES-XTS Flash Encryption -> encrypted storage
```

This is not application-level AES-GCM. Do not place a reusable AES key in `main/`. Flash Encryption protects firmware stored on the device; it does not replace HTTPS and does not prove who published the image.

### 8.3 Transport security

Use HTTPS from the ESP32 to `https://ota.divysoni.me/version` and `/update`, with CA certificate validation configured in the ESP-IDF HTTP client. A browser showing a padlock does not prove that the ESP32 validates the certificate.

### 8.4 What must be tested

- Signed image boots on a dedicated development board.
- Modified image is rejected.
- Image signed by an untrusted key is rejected.
- HTTPS certificate validation succeeds for the real domain.
- Wrong, expired, or untrusted server certificate is rejected.
- Flash Encryption device boots and performs OTA.
- Interrupted update preserves the previous image.
- Failed startup triggers rollback when rollback is enabled.
- No private signing or encryption key appears in Git, logs, dashboard storage, or firmware strings.

## 9. Safety constraints

- Use a separate development ESP32 before burning security eFuses.
- Never manually invent Secure Boot or Flash Encryption values in `sdkconfig`.
- Keep the private signing key outside the repository.
- Do not claim RSA/AES-GCM is active unless a separately designed custom protocol is implemented and reviewed.
- Do not enable production eFuse settings until signed OTA and recovery are repeatable.
- Keep the existing local HTTP server as a development fallback only; production device URLs should use HTTPS.

## 10. Acceptance criteria

The current OTA feature is accepted when a device can upgrade from `5.0.0` to `6.0.0`, reboot, report `6.0.0`, confirm the pending image, and continue polling without errors.

The production security feature is accepted only when the cryptographic tests in section 8.4 pass on a dedicated development board and the implementation uses the ESP32-native security mechanisms appropriate for the classic ESP32 target.

## 11. Open decisions for the next AI agent

1. Confirm the exact ESP32 chip and ESP-IDF release to preserve Secure Boot V1 compatibility.
2. Decide whether the release service will use Vercel Blob public URLs or authenticated/short-lived delivery URLs.
3. Define the CA certificate provisioning method for the ESP32 HTTPS client.
4. Define the signed-image build pipeline and where signing occurs.
5. Decide whether a custom application-level RSA or AES-GCM protocol is genuinely required. It should not be added merely as a substitute for Secure Boot or Flash Encryption.
6. Select a sacrificial development board for irreversible security testing.
