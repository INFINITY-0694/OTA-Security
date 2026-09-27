from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import os
from pathlib import Path
import ssl
import json


SERVER_PORT = 8000
VERSION_FILE = Path(__file__).with_name("server_version.txt")
MANIFEST_FILE = Path(__file__).with_name("release.json")
FIRMWARE_FILE = Path(__file__).with_name("release.bin")
TLS_CERT_FILE = os.getenv("OTA_TLS_CERT_FILE")
TLS_KEY_FILE = os.getenv("OTA_TLS_KEY_FILE")


def load_release():
    try:
        manifest = json.loads(MANIFEST_FILE.read_text(encoding="utf-8"))
        for field in ("version", "size", "encrypted_size", "sha256", "chunk_size", "chunks"):
            if field not in manifest:
                raise ValueError(f"manifest field missing: {field}")

        chunk_size = int(manifest["chunk_size"])
        if chunk_size != 32 * 1024 or not isinstance(manifest["chunks"], list) or not manifest["chunks"]:
            raise ValueError("invalid chunk configuration")
        total_size = 0
        total_encrypted_size = 0
        for index, chunk in enumerate(manifest["chunks"]):
            if (not isinstance(chunk, dict) or chunk.get("index") != index or
                chunk.get("offset") != total_size or
                    not isinstance(chunk.get("size"), int) or chunk["size"] <= 0 or
                    chunk["size"] > chunk_size or
                    (index + 1 < len(manifest["chunks"]) and chunk["size"] != chunk_size) or
                    chunk.get("encrypted_size") != chunk["size"] + 16 or
                    not isinstance(chunk.get("sha256"), str) or len(chunk["sha256"]) != 64 or
                not isinstance(chunk.get("nonce"), str) or len(chunk["nonce"]) != 16 or
                not isinstance(chunk.get("tag"), str) or len(chunk["tag"]) != 24 or
                not isinstance(chunk.get("signature"), str)):
                raise ValueError(f"invalid chunk metadata at index {index}")
            total_size += chunk["size"]
            total_encrypted_size += chunk["encrypted_size"]

        if total_size != int(manifest["size"]) or total_encrypted_size != int(manifest["encrypted_size"]):
            raise ValueError("chunk sizes do not match manifest totals")
        if total_encrypted_size != FIRMWARE_FILE.stat().st_size:
            raise ValueError("encrypted artifact size does not match manifest")
        return manifest
    except (OSError, ValueError, KeyError, TypeError, json.JSONDecodeError) as error:
        raise RuntimeError(f"release is incomplete or inconsistent: {error}") from error


class OtaRequestHandler(BaseHTTPRequestHandler):
    def do_GET(self):
        if self.path == "/version":
            self.send_version()
            return
        if self.path == "/manifest":
            self.send_manifest()
            return
        if self.path == "/update":
            self.send_firmware()
            return
        self.send_error(404, "Not found")

    def send_version(self):
        try:
            version = VERSION_FILE.read_text(encoding="utf-8").strip()
        except OSError:
            self.send_error(500, "Version file unavailable")
            return

        self.send_response(200)
        self.send_header("Content-Type", "text/plain; charset=utf-8")
        self.send_header("Content-Length", str(len(version)))
        self.end_headers()
        self.wfile.write(version.encode("utf-8"))

    def send_firmware(self):
        try:
            load_release()
            firmware = FIRMWARE_FILE.read_bytes()
        except RuntimeError as error:
            self.send_error(503, str(error))
            return
        except OSError:
            self.send_error(404, "Firmware binary unavailable; build and sign a release first")
            return

        self.send_response(200)
        self.send_header("Content-Type", "application/octet-stream")
        self.send_header("Content-Length", str(len(firmware)))
        self.end_headers()
        self.wfile.write(firmware)

    def send_manifest(self):
        try:
            manifest = json.dumps(load_release(), indent=2).encode("utf-8")
        except RuntimeError as error:
            self.send_error(503, str(error))
            return
        except OSError:
            self.send_error(404, "Signed release manifest unavailable")
            return

        self.send_response(200)
        self.send_header("Content-Type", "application/json")
        self.send_header("Cache-Control", "no-store")
        self.send_header("Content-Length", str(len(manifest)))
        self.end_headers()
        self.wfile.write(manifest)


def main():
    server = ThreadingHTTPServer(("0.0.0.0", SERVER_PORT), OtaRequestHandler)

    if bool(TLS_CERT_FILE) != bool(TLS_KEY_FILE):
        raise RuntimeError("Set both OTA_TLS_CERT_FILE and OTA_TLS_KEY_FILE for HTTPS")

    if TLS_CERT_FILE and TLS_KEY_FILE:
        tls_context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
        tls_context.load_cert_chain(TLS_CERT_FILE, TLS_KEY_FILE)
        server.socket = tls_context.wrap_socket(server.socket, server_side=True)
        print(f"OTA HTTPS server listening on port {SERVER_PORT}")
    else:
        print(f"OTA HTTP server listening on port {SERVER_PORT}")

    server.serve_forever()


if __name__ == "__main__":
    main()
