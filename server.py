from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import os
from pathlib import Path
import ssl


SERVER_PORT = 8000
VERSION_FILE = Path(__file__).with_name("server_version.txt")
FIRMWARE_FILE = Path(__file__).with_name("build") / "esp32_secure_ota_2026_09_14.bin"
TLS_CERT_FILE = os.getenv("OTA_TLS_CERT_FILE")
TLS_KEY_FILE = os.getenv("OTA_TLS_KEY_FILE")


class OtaRequestHandler(BaseHTTPRequestHandler):
    def do_GET(self):
        if self.path == "/version":
            self.send_version()
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
            firmware = FIRMWARE_FILE.read_bytes()
        except OSError:
            self.send_error(404, "Firmware binary unavailable; build the project first")
            return

        self.send_response(200)
        self.send_header("Content-Type", "application/octet-stream")
        self.send_header("Content-Length", str(len(firmware)))
        self.end_headers()
        self.wfile.write(firmware)


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
