# Project file details

This project is a hardware-tested ESP32 OTA project. It connects to Wi-Fi,
checks the server version, downloads a valid ESP-IDF firmware image, writes it
to the inactive OTA partition, and reboots into the new image.

## Files in this folder

| File or folder | Why it is needed |
|---|---|
| `CMakeLists.txt` | Starts the ESP-IDF project and sets the application version. |
| `main/` | Contains the ESP32 application source files. |
| `partitions.csv` | Creates NVS, OTA data, and the two OTA application partitions needed for future OTA updates. |
| `sdkconfig.defaults` | Sets the ESP32 flash settings and selects `partitions.csv`. |
| `main/Kconfig.projbuild` | Adds the Wi-Fi credentials and version-server URL settings to ESP-IDF menuconfig. |
| `server.py` | Runs the local server that provides `GET /version` for the ESP32. |
| `server_version.txt` | Stores the version returned by the local server. It must match the firmware binary served from `build/`. |
| `details.md` | Explains the required files in this folder. |

## Configure and flash a physical ESP32

1. Connect the ESP32 over USB and identify its serial port.
2. Open ESP-IDF menuconfig:

```powershell
idf.py menuconfig
```

Set `Secure OTA project settings` to your Wi-Fi SSID, Wi-Fi password, and the
LAN URL of the computer running `server.py`, for example
`http://192.168.1.100:8000/version`. Do not use `localhost`; the ESP32 must
reach the computer over the network.

Build, flash, and monitor the board with:

```powershell
idf.py build
idf.py -p COM<n> flash monitor
```

Replace `COM<n>` with the port assigned to the board.

## Run the local version server

From this folder, run:

```powershell
python server.py
```

Run this on the same computer whose LAN address is configured above. Allow
Python through the firewall on private networks if the ESP32 cannot connect.
The ESP32 requests `/version` every 30 seconds. Rebuild the firmware before
changing `server_version.txt` to a newer version.

Useful documentation:

- [ESP-IDF Get Started](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/get-started/index.html)
- [ESP-IDF Build and Flash](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/get-started/start-project.html#build-the-project)
- [ESP-IDF Wi-Fi Station](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-guides/wifi.html)
