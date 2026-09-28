# ESP32 Secure OTA - September 14, 2026

Hardware-tested ESP32 firmware update project using Wi-Fi, a local version
server, and ESP-IDF OTA partitions.

## Security model

This project uses ESP-IDF security features rather than custom cryptography in
the application:

- Secure Boot uses an ECDSA-256 signing key pair. The release computer keeps the
	private key and the ESP32 verifies application images with the public key.
- Flash Encryption uses a device-protected AES key to encrypt firmware stored
	in flash. The ESP32 decrypts it through its hardware while running.
- The existing OTA manager downloads the ESP-IDF application image and writes
	it to the inactive OTA partition. ESP-IDF validates the image before it is
	selected for boot.

Secure Boot and Flash Encryption are intentionally not enabled in the checked-in
development configuration yet. They can permanently change eFuses, so test the
complete signed OTA and recovery process on a separate board first. See
[SECURITY.md](SECURITY.md) for the key-handling and activation procedure.

## Per-device encrypted OTA development flow

Production OTA uses the HTTPS endpoints on `ota.divysoni.me`; the device
validates the server certificate with ESP-IDF's certificate bundle. The local
`python server.py` service is for LAN development only.

The initial USB-installed factory image seeds a unique 32-byte AES key into the
device's `ota_keys` NVS namespace. Generate the private provisioning material
outside the repository:

```text
python provision_device_key.py
```

Use the generated `factory_key.conf` only while building the initial USB image;
do not commit it. Later OTA builds load the key from NVS and should leave
`CONFIG_OTA_FACTORY_AES_KEY_HEX` empty.

Because an existing `sdkconfig` value takes precedence over defaults, apply the
factory fragment by temporarily moving the existing configuration before the
first build:

```powershell
Move-Item sdkconfig sdkconfig.before-factory
idf.py -D SDKCONFIG_DEFAULTS="sdkconfig.defaults;factory_key.conf" build
idf.py -p COM<n> flash monitor
Move-Item sdkconfig.before-factory sdkconfig
```

Do not run `erase-flash` after this build unless you intend to provision the
device again. Keep `device_key.txt` for every later encrypted release.

Create an encrypted, signed release for that device with:

```text
python sign_release.py --firmware build/firmware.bin --version 6.2.0 \
	--private-key signing-key.pem --aes-key device_key.txt \
	--encrypted-output release.bin --output release.json
```

The manifest contains a signature for every chunk, binding its version, index,
offset, lengths, plaintext SHA-256, nonce, and GCM tag. The ESP32 verifies the
chunk signature, downloads and authenticates that AES-256-GCM chunk in bounded
buffers, checks its plaintext hash, and only then writes it to the inactive OTA
partition. It selects the new boot partition only after all chunks and the full
image size and SHA-256 pass. The AES key is not logged or embedded in later OTA
images.