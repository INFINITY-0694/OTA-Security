#!/usr/bin/env python3
"""Create a signed OTA release manifest without exposing the private key."""

import argparse
import base64
import hashlib
import json
from pathlib import Path
import secrets
from cryptography.hazmat.primitives import hashes, serialization
from cryptography.hazmat.primitives.asymmetric import ec, padding, rsa
from cryptography.hazmat.primitives.ciphers.aead import AESGCM


CHUNK_SIZE = 32 * 1024


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--firmware", type=Path, required=True)
    parser.add_argument("--version", required=True)
    parser.add_argument("--private-key", type=Path, required=True)
    parser.add_argument("--public-key", type=Path, default=Path("main/ota_public_key.pem"),
                        help="Public key embedded in the ESP32 firmware")
    parser.add_argument("--aes-key", type=Path, required=True,
                        help="File containing the 64-hex-character per-device AES key")
    parser.add_argument("--encrypted-output", type=Path, default=Path("release.bin"))
    parser.add_argument("--output", type=Path, default=Path("release.json"))
    args = parser.parse_args()

    if not args.firmware.is_file():
        parser.error(f"firmware file does not exist: {args.firmware}")
    if not args.private_key.is_file():
        parser.error(f"private signing key does not exist: {args.private_key}")
    if not args.public_key.is_file():
        parser.error(f"embedded public key does not exist: {args.public_key}")
    if not args.aes_key.is_file():
        parser.error(f"AES key file does not exist: {args.aes_key}")

    plaintext = args.firmware.read_bytes()
    try:
        key = bytes.fromhex(args.aes_key.read_text(encoding="ascii").strip())
    except (OSError, UnicodeDecodeError, ValueError) as error:
        parser.error(f"invalid AES key file: {error}")
    if len(key) != 32:
        parser.error("AES key must contain exactly 32 bytes encoded as 64 hex characters")
    if not args.version.replace(".", "").isdigit() or args.version.count(".") != 2:
        parser.error("version must use MAJOR.MINOR.PATCH format")
    try:
        private_key = serialization.load_pem_private_key(
            args.private_key.read_bytes(), password=None)
        public_key = serialization.load_pem_public_key(args.public_key.read_bytes())
    except (OSError, ValueError, TypeError) as error:
        parser.error(f"invalid signing key: {error}")
    if private_key.public_key().public_numbers() != public_key.public_numbers():
        parser.error("private signing key does not match the embedded public key")
    plaintext_size = len(plaintext)
    sha256_hex = hashlib.sha256(plaintext).hexdigest()
    encrypted_chunks = []
    chunks = []
    for index, offset in enumerate(range(0, plaintext_size, CHUNK_SIZE)):
        chunk = plaintext[offset:offset + CHUNK_SIZE]
        nonce = secrets.token_bytes(12)
        encrypted_with_tag = AESGCM(key).encrypt(nonce, chunk, None)
        ciphertext = encrypted_with_tag[:-16]
        tag = encrypted_with_tag[-16:]
        nonce_b64 = base64.b64encode(nonce).decode("ascii")
        tag_b64 = base64.b64encode(tag).decode("ascii")
        chunk_sha256 = hashlib.sha256(chunk).hexdigest()
        encrypted_size = len(encrypted_with_tag)
        payload = (f"{args.version}\n{index}\n{offset}\n{len(chunk)}\n"
                   f"{encrypted_size}\n{chunk_sha256}\n{nonce_b64}\n{tag_b64}\n").encode("ascii")
        if isinstance(private_key, ec.EllipticCurvePrivateKey):
            signed = private_key.sign(payload, ec.ECDSA(hashes.SHA256()))
        elif isinstance(private_key, rsa.RSAPrivateKey):
            signed = private_key.sign(payload, padding.PKCS1v15(), hashes.SHA256())
        else:
            parser.error("signing key must be an EC or RSA private key")
        encrypted_chunks.append(encrypted_with_tag)
        chunks.append({
            "index": index,
            "offset": offset,
            "size": len(chunk),
            "encrypted_size": encrypted_size,
            "sha256": chunk_sha256,
            "nonce": nonce_b64,
            "tag": tag_b64,
            "signature": base64.b64encode(signed).decode("ascii"),
        })

    args.encrypted_output.write_bytes(b"".join(encrypted_chunks))
    manifest = {"version": args.version, "size": plaintext_size,
                "encrypted_size": sum(item["encrypted_size"] for item in chunks),
                "sha256": sha256_hex, "chunk_size": CHUNK_SIZE, "chunks": chunks}
    args.output.write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    print(f"Wrote {args.output} and {args.encrypted_output} ({plaintext_size} bytes plaintext)")


if __name__ == "__main__":
    main()
