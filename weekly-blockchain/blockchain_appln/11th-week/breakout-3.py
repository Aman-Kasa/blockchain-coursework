"""
One-command class demo:  python3 demo.py
Press Enter to move to each step.
"""
import os, shutil, time
from datetime import datetime, timezone
from blockchain import Blockchain
from hashing import sha256_file, bits_changed
from task5_license import IPLicenseContract

G, R, Y, B, X = "\033[92m", "\033[91m", "\033[93m", "\033[1m", "\033[0m"

def step(n, title):
    input(f"\n{Y}--- press Enter for step {n} ---{X}")
    print(f"\n{B}STEP {n}: {title}{X}")

for f in ["chain.json", "license_chain.json", "modified_work.py"]:
    if os.path.exists(f): os.remove(f)

print(f"{B}Protecting my code with blockchain{X}")
print("My work:  original_work.py  (a Fibonacci program)")

step(1, "Fingerprint the file (SHA-256)")
h = sha256_file("original_work.py")
print(f"Hash: {G}{h}{X}")
print("Same file -> same hash, every time. Hash is public, code stays private.")

step(2, "Record the fingerprint on the blockchain")
chain = Blockchain()
blk = chain.add({"file_hash": h, "owner": "Aman Abraha Kasa"})
ts = datetime.fromtimestamp(blk.timestamp, timezone.utc).strftime("%Y-%m-%d %H:%M:%S UTC")
print(f"Block #{blk.index}   time: {ts}")
print(f"Block hash: {blk.hash}")
print(f"{G}Proof: I had this exact file at this time.{X}")

step(3, "Someone changes ONE character")
shutil.copy("original_work.py", "modified_work.py")
src = open("modified_work.py").read().replace("a, b = 0, 1", "a, b = 1, 1", 1)
open("modified_work.py", "w").write(src)
h2 = sha256_file("modified_work.py")
print("Change:   a, b = 0, 1   ->   a, b = 1, 1")
print(f"Original: {h}")
print(f"Modified: {R}{h2}{X}")
print(f"{bits_changed(h, h2)} of 256 bits changed -> totally different hash")

step(4, "Verify both files against the blockchain")
for f in ["original_work.py", "modified_work.py"]:
    found = chain.find_file_hash(sha256_file(f))
    if found:
        print(f"{f:18} {G}ORIGINAL (matches block #{found.index}){X}")
    else:
        print(f"{f:18} {R}TAMPERED (no match on chain){X}")

step(5, "Can someone fake the record on the chain?")
chain.chain[1].data["owner"] = "Hacker"
print('Hacker changes owner in block #1 to "Hacker"...')
print(f"Chain check: {R}{chain.is_valid()[1]}{X}")

step(6, "Smart contract: licensing with time limits")
c = IPLicenseContract(Blockchain("license_chain.json"))
now = [time.time()]; c.now = lambda: now[0]
c._emit = lambda *a, **k: None          # keep demo output clean
c.register_work("Aman", h)
c.grant_access("Aman", h, "StudioX", 30 * 86400)
show = lambda who, ok: print(f"{who:22} {G+'ACCESS GRANTED' if ok else R+'DENIED'}{X}")
show("StudioX (day 1)", c.request_access("StudioX", h))
show("Bob (no license)", c.request_access("Bob", h))
now[0] += 31 * 86400
show("StudioX (day 31)", c.request_access("StudioX", h))
print("Every attempt is logged on-chain -> evidence of misuse.")

print(f"\n{B}Summary:{X} hash = fingerprint, blockchain = timestamp proof,")
print("any change is detected, smart contract controls and logs access.")
