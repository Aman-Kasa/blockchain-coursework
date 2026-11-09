"""SHA-256 file fingerprinting."""
import hashlib


def sha256_file(path, chunk_size=65536):
    """Hash a file in chunks so large files (images, videos) don't fill memory."""
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(chunk_size), b""):
            h.update(chunk)
    return h.hexdigest()


def bits_changed(hex_a, hex_b):
    """How many of the 256 output bits differ between two hashes."""
    return bin(int(hex_a, 16) ^ int(hex_b, 16)).count("1")
