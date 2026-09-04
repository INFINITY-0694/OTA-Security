# Main source file details

These files implement the current V2 ESP32 application.

| File | Why it is needed |
|---|---|
| `main.c` | Starts the application, initializes NVS, starts Wi-Fi, and starts the OTA manager after Wi-Fi connects. |
| `wifi_manager.c` | Connects the physical ESP32 to the configured Wi-Fi network, retries failed connections, and prints network information. |
| `wifi_manager.h` | Declares the Wi-Fi initialization function for `main.c`. |
| `ota_manager.c` | Requests `/version` every 30 seconds from the configured LAN server, compares `major.minor.patch` versions, and prints the result. |
| `ota_manager.h` | Declares the OTA manager initialization function for `main.c`. |
| `crypto_manager.c` | Decrypts AES-256-GCM firmware data and calculates SHA-256 hashes using Mbed TLS. |
| `crypto_manager.h` | Declares the decryption and hash functions. |
| `signature_verifier.c` | Verifies a package signature with the embedded trusted public key. |
| `signature_verifier.h` | Declares the signature verification function. |
| `manifest.c` | Reads and validates the fixed binary OTA package metadata. |
| `manifest.h` | Defines the OTA package metadata returned by `manifest.c`. |
| `trusted_public_key.pem` | Placeholder for the trusted ECDSA P-256 public key. It must never contain a private key. |
| `CMakeLists.txt` | Registers the source files and ESP-IDF components required to build this folder. |
| `details.md` | Explains the required files in this folder. |

The OTA manager downloads a valid ESP-IDF image and writes it to the inactive
OTA partition. The crypto modules
are reserved for the next signed and encrypted package format.
is successful.
