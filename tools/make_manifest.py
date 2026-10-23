#!/usr/bin/env python3
"""
Generate manifest.json for OTA with firmware signing.

Usage:
    make_manifest.py firmware.bin --version 1.2.0 --build 43 \\
        --url http://server/firmware.bin \\
        --hw-rev 1 --min-bootloader 1.0.0 \\
        --secure-version 5 --project myproject --board esp32 \\
        --private-key ota_private.pem
"""
import argparse
import hashlib
import json
import os
import sys
import base64
from cryptography.hazmat.primitives import hashes
from cryptography.hazmat.primitives.asymmetric import padding, utils
from cryptography.hazmat.backends import default_backend
from cryptography.hazmat.primitives.serialization import load_pem_private_key

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("firmware_bin")
    parser.add_argument("--version", required=True)
    parser.add_argument("--build", type=int, required=True)
    parser.add_argument("--url", required=True)
    parser.add_argument("--hw-rev", default="*")
    parser.add_argument("--min-bootloader", default="1.0.0")
    parser.add_argument("--mandatory", action="store_true")
    parser.add_argument("--secure-version", type=int, default=0)
    parser.add_argument("--project", default="")
    parser.add_argument("--board", default="")
    parser.add_argument("-o", "--output", default="manifest.json")
    parser.add_argument("--private-key", required=True, help="Path to private key PEM file")
    args = parser.parse_args()

    if not os.path.isfile(args.firmware_bin):
        print(f"error: {args.firmware_bin} not found", file=sys.stderr)
        sys.exit(1)

    size = os.path.getsize(args.firmware_bin)
    sha256 = hashlib.sha256()
    with open(args.firmware_bin, "rb") as f:
        for chunk in iter(lambda: f.read(65536), b""):
            sha256.update(chunk)
    firmware_hash = sha256.digest()  # raw bytes

    # --- Sign the hash with private key ---
    with open(args.private_key, "rb") as f:
        private_key = load_pem_private_key(f.read(), password=None, backend=default_backend())

    # IMPORTANT: firmware_hash is already a raw SHA-256 digest (32 bytes).
    # We must tell cryptography that the input is pre-hashed, otherwise
    # .sign() will hash it AGAIN internally, producing a signature over
    # SHA256(SHA256(firmware)) instead of SHA256(firmware). The device
    # (mbedtls_pk_verify) verifies against the single hash, so a mismatch
    # here causes "Signature verification FAILED (error -17280)" on target.
    signature = private_key.sign(
        firmware_hash,
        padding.PKCS1v15(),
        utils.Prehashed(hashes.SHA256())
    )
    signature_b64 = base64.b64encode(signature).decode('ascii')

    manifest = {
        "version": args.version,
        "build": args.build,
        "url": args.url,
        "size": size,
        "sha256": sha256.hexdigest(),
        "hw_rev": args.hw_rev,
        "min_bootloader": args.min_bootloader,
        "mandatory": args.mandatory,
        "secure_version": args.secure_version,
        "project": args.project,
        "board": args.board,
        "signature": signature_b64,
    }

    with open(args.output, "w") as f:
        json.dump(manifest, f, indent=2)
        f.write("\n")

    print(f"Wrote {args.output}:")
    print(json.dumps(manifest, indent=2))

if __name__ == "__main__":
    main()