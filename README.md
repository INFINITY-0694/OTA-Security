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