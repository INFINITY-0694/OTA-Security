# OTA Control Room

Vercel-ready dashboard and API for the ESP32 OTA project.

## Vercel setup

Set the dashboard root directory to `dashboard` and connect a Vercel Blob store. Add this environment variable:

```text
OTA_ADMIN_TOKEN=<long random value>
```

The Blob integration supplies `BLOB_READ_WRITE_TOKEN` automatically.

The deployed endpoints are:

```text
GET  /version
GET  /manifest
GET  /update
POST /api/release
```

Create `release.json` with `sign_release.py` on the protected release computer.
Upload the plaintext firmware for hash and size checks, the generated encrypted
release binary, and its signed chunk manifest. The private signing key is never
uploaded.

Configure the ESP32 with the deployed HTTPS URLs only after the domain and certificate are active.
