"""Task 2: store the file hash on the blockchain as proof of ownership."""
import sys
from datetime import datetime, timezone
from blockchain import Blockchain
from hashing import sha256_file

path = sys.argv[1] if len(sys.argv) > 1 else "original_work.py"
owner = sys.argv[2] if len(sys.argv) > 2 else "Aman Abraha Kasa"

chain = Blockchain()
file_hash = sha256_file(path)

existing = chain.find_file_hash(file_hash)
if existing:
    print(f"Already registered in block {existing.index}; first claim stands.")
    block = existing
else:
    block = chain.add({"type": "ip_registration", "file_hash": file_hash,
                       "owner": owner, "filename": path})

ts = datetime.fromtimestamp(block.timestamp, timezone.utc).isoformat()
print(f"File hash:        {file_hash}")
print(f"Owner:            {block.data['owner']}")
print(f"Block number:     {block.index}")
print(f"Transaction hash: {block.hash}")
print(f"Previous hash:    {block.previous_hash}")
print(f"Timestamp (UTC):  {ts}")
