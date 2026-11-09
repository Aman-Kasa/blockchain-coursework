"""Task 4: given only a file and the chain, decide if it is the original."""
import sys
from datetime import datetime, timezone
from blockchain import Blockchain
from hashing import sha256_file


def verify(path, chain):
    ok, msg = chain.is_valid()
    print(f"[1] Chain integrity check: {msg}")
    if not ok:
        print("    Chain itself was tampered with; records cannot be trusted.")
        return False
    h = sha256_file(path)
    print(f"[2] Recomputed SHA-256 of '{path}': {h}")
    block = chain.find_file_hash(h)
    if block:
        ts = datetime.fromtimestamp(block.timestamp, timezone.utc).isoformat()
        print(f"[3] MATCH in block {block.index} (tx {block.hash[:16]}...)")
        print(f"    Registered by {block.data['owner']} at {ts}")
        print("    VERDICT: file is ORIGINAL and unaltered.")
        return True
    print("[3] NO MATCH on chain.")
    print("    VERDICT: file has been ALTERED (or was never registered).")
    return False


if __name__ == "__main__":
    chain = Blockchain()
    for p in sys.argv[1:] or ["original_work.py", "modified_work.py"]:
        print(f"\n=== Verifying {p} ===")
        verify(p, chain)

    # Bonus: show that rewriting history on the chain is detected
    print("\n=== Attacker edits the owner name inside block 1 ===")
    chain.chain[1].data["owner"] = "Mallory"
    print(f"Chain check: {chain.is_valid()[1]}")
