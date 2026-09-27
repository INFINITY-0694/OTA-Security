# Ota Logs

Run the recorder from an ESP-IDF PowerShell terminal in the project root:

```powershell
& ".\Ota Logs\capture-monitor.ps1" -Port COM6
```

The recorder saves the complete monitor output to `Log_File.txt`, then creates three log-only views:

- `ota_check.txt`: release checks, verified manifest version, newer-version decision, and check failures.
- `ota_update.txt`: new-version notice, downloads, chunk verification and writes, full-image validation, and install result.
- `ota_other.txt`: boot, Wi-Fi, watchdog, reset, and related system events.

These four files are overwritten by the next capture. Rename or copy them first if you need to retain an older run. Keep the monitor open through the update and reboot, then stop it with `Ctrl+]` so the three filtered files are generated.

The current `Log_File.txt` is the complete monitor transcript you saved. `ota_update.txt` is the focused chunk-by-chunk view; the recorder rebuilds it from `Log_File.txt` after the next capture.

Review MAC addresses and local IP addresses before sharing logs outside your project group. Never share signing keys or device AES keys.