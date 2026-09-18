# Main source files

These are the files used by the current ESP32 OTA application.

| File | Why it is needed |
|---|---|
| `main.c` | Starts the application, initializes NVS, starts Wi-Fi, and starts the OTA manager after Wi-Fi connects. |
| `wifi_manager.c` | Connects the physical ESP32 to the configured Wi-Fi network, retries failed connections, and prints network information. |
| `wifi_manager.h` | Declares the Wi-Fi initialization function for `main.c`. |
| `ota_manager.c` | Requests `/version` every 30 seconds, compares `major.minor.patch` versions, and downloads newer firmware from `/update`. |
| `ota_manager.h` | Declares the OTA manager initialization function for `main.c`. |
| `CMakeLists.txt` | Registers the source files and ESP-IDF components required to build this folder. |
| `details.md` | Documents the source files in this folder. |

The OTA manager downloads a valid ESP-IDF image, writes it to the inactive OTA
partition, validates it, and restarts the device.
