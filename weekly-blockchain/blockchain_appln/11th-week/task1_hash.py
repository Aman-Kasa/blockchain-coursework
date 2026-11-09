"""Task 1: SHA-256 fingerprint of an original file."""
import sys
from hashing import sha256_file

path = sys.argv[1] if len(sys.argv) > 1 else "original_work.py"
print(f"File:    {path}")
print(f"SHA-256: {sha256_file(path)}")
