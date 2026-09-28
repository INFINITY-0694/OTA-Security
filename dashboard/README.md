# OTA Control Room

Vercel-ready dashboard and API for the ESP32 OTA project.

## Vercel setup

Set the dashboard root directory to `dashboard` and connect a Vercel Blob store. Add this environment variable:

```text
OTA_ADMIN_TOKEN=<long random value>
```

The Blob integration supplies `BLOB_READ_WRITE_TOKEN` automatically.

Add `ota.divysoni.me` to the Vercel project's Domains settings and configure the
DNS record Vercel provides. Wait until Vercel reports the domain and HTTPS
certificate as active before building firmware for production.

The firmware-facing routes are rewritten to these Vercel functions:

```text
POST /api/auth
GET  /version
GET  /manifest
GET  /update
POST /api/release
```

Create `release.json` with `sign_release.py` on the protected release computer.
Upload the plaintext firmware for hash and size checks, the generated encrypted
release binary, and its signed chunk manifest. The private signing key is never
uploaded.

Sign in through the dashboard with the `OTA_ADMIN_TOKEN` value. The token is
validated by `/api/auth`, kept only in page memory, and sent as a bearer token
for release uploads. Signing out or reloading clears the browser session.
Configure `OTA_ADMIN_TOKEN` as a Vercel Production environment variable. Blob
storage is required to publish or serve releases; the dashboard reports a clear
configuration error until the Vercel Blob integration is connected.

The ESP32 uses these HTTPS URLs:

```text
https://ota.divysoni.me/version
https://ota.divysoni.me/manifest
https://ota.divysoni.me/update
```

Before flashing production firmware, confirm the domain responds successfully:

```powershell
Invoke-WebRequest -UseBasicParsing https://ota.divysoni.me/version
Invoke-WebRequest -UseBasicParsing https://ota.divysoni.me/manifest
```

The manifest and update routes require a release to have been uploaded. Keep the
local `python server.py` HTTP service for LAN development only; it is not the
production OTA endpoint.
