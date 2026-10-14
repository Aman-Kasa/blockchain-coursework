#!/usr/bin/env python3
"""Attacker simulation for the tamper-detection demo.

The brief's motivating fraud: "a due date can be silently altered after the
fact". This script extends the due date of the first BORROWED record in
data/chain.dat by 30 days and then does everything an attacker WITHOUT the
member's private key can do to hide it:

  * recomputes the record's ID and its Merkle leaf hash,
  * recomputes the block's Merkle root and reward transaction_id,
  * re-mines the block so its hash meets the difficulty again,
  * relinks and re-mines every later block.

It cannot produce a new ECDSA signature for the altered record, so it keeps
the old one. validate_chain then reports the record's signature as invalid,
showing that hashing, Merkle roots and proof of work alone do not make the
chain tamper-proof; the signatures do.

Usage: python3 tests/forge_chain.py [data/chain.dat] [days]
"""
import hashlib
import sys

FEE = 1


def sha(b):
    if isinstance(b, str):
        b = b.encode()
    return hashlib.sha256(b).hexdigest()


def merkle(leaves):
    if not leaves:
        return "0" * 64
    level = list(leaves)
    while len(level) > 1:
        nxt = []
        for i in range(0, len(level), 2):
            left = level[i]
            right = level[i + 1] if i + 1 < len(level) else level[i]
            nxt.append(sha(bytes.fromhex(left) + bytes.fromhex(right)))
        level = nxt
    return level[0]


def record_parts(line):
    f = line[2:].split("|")          # strip "R|"
    return f                         # [id, v1, ts, action, book, copy, title, member, name, due, fine, reward, cond, reason, counter, amount, signer, nonce, pubkey, sig]


def content(f):
    return "|".join(f[1:19])


def leaf(f):
    return sha(f"{f[0]}|{content(f)}|{f[19]}")


def reward_txid(f):
    if f[3] != "RETURNED" or int(f[11]) <= 0:
        return ""
    return sha(f"REWARD|SYSTEM|{f[7]}|{f[5]}|{f[0]}|{f[11]}|{FEE}")


def header_hash(h):
    return sha("|".join([h[1], h[2], h[3], h[4], h[5], h[7], h[8], h[9], h[10], h[11]]))


def main():
    path = sys.argv[1] if len(sys.argv) > 1 else "data/chain.dat"
    days = int(sys.argv[2]) if len(sys.argv) > 2 else 30
    lines = [l.rstrip("\n") for l in open(path) if l.strip()]
    blocks = []                      # [header_fields, [record_fields...]]
    for l in lines:
        if l.startswith("#"):
            continue
        if l.startswith("B|"):
            blocks.append([l.split("|"), []])
        elif l.startswith("R|"):
            blocks[-1][1].append(record_parts(l))

    target = None
    for bi, (h, recs) in enumerate(blocks):
        for ri, f in enumerate(recs):
            if f[3] == "BORROWED":
                target = (bi, ri)
                break
        if target:
            break
    if not target:
        sys.exit("no BORROWED record to forge - borrow a book and mine first")

    bi, ri = target
    f = blocks[bi][1][ri]
    old_id = f[0]
    f[9] = str(int(f[9]) + days * 86400)
    f[0] = sha(content(f))[:16]
    print(f"Forging block {bi}: record {old_id} due date pushed back {days} days (new record id {f[0]})")
    print("  signature left unchanged (a new one needs the member's private key)")

    for k in range(bi, len(blocks)):
        h, recs = blocks[k]
        if k > 0:
            h[4] = blocks[k - 1][0][6]                       # previous_hash
        h[5] = merkle([leaf(r) for r in recs])               # merkle_root
        txids = "".join(t for t in (reward_txid(r) for r in recs) if t)
        h[9] = sha(txids) if txids else ""                   # transaction_id
        diff = int(h[10])
        nonce = 0
        while True:
            h[11] = str(nonce)
            hh = header_hash(h)
            if hh.startswith("0" * diff):
                h[6] = hh
                break
            nonce += 1
        print(f"  block {h[1]}: Merkle root, links and proof of work redone ({nonce + 1} attempts)")

    with open(path, "w") as out:
        out.write("# library chain v1 (forged)\n")
        for h, recs in blocks:
            out.write("|".join(h) + "\n")
            for r in recs:
                out.write("R|" + "|".join(r) + "\n")


if __name__ == "__main__":
    main()
