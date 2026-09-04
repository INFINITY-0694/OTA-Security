from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path


SERVER_PORT = 8000
VERSION_FILE = Path(__file__).with_name("server_version.txt")
FIRMWARE_FILE = Path(__file__).with_name("build") / "secure_ota_v1.bin"


class OtaRequestHandler(BaseHTTPRequestHandler):
    def do_GET(self):
        if self.path == "/version":
            self.send_version()
            return
        if self.path == "/firmware":
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
    print(f"OTA version server listening on port {SERVER_PORT}")
    server.serve_forever()


if __name__ == "__main__":
    main()
