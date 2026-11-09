"""
A minimal local blockchain (mock of Ethereum-style timestamping).

Each block stores: index, timestamp, data (our IP record), previous block's
hash, a proof-of-work nonce, and its own hash. Changing any old block changes
its hash and breaks every link after it, which is what makes records
tamper-evident.
"""
import hashlib
import json
import os
import time

DIFFICULTY = 4  # block hash must start with this many zeros
CHAIN_FILE = "chain.json"


class Block:
    def __init__(self, index, timestamp, data, previous_hash, nonce=0, hash=None):
        self.index = index
        self.timestamp = timestamp
        self.data = data
        self.previous_hash = previous_hash
        self.nonce = nonce
        self.hash = hash or self.compute_hash()

    def compute_hash(self):
        payload = json.dumps(
            {
                "index": self.index,
                "timestamp": self.timestamp,
                "data": self.data,
                "previous_hash": self.previous_hash,
                "nonce": self.nonce,
            },
            sort_keys=True,
        )
        return hashlib.sha256(payload.encode()).hexdigest()

    def mine(self):
        while not self.hash.startswith("0" * DIFFICULTY):
            self.nonce += 1
            self.hash = self.compute_hash()


class Blockchain:
    def __init__(self, path=CHAIN_FILE):
        self.path = path
        if os.path.exists(path):
            with open(path) as f:
                self.chain = [Block(**b) for b in json.load(f)]
        else:
            genesis = Block(0, time.time(), {"type": "genesis"}, "0" * 64)
            genesis.mine()
            self.chain = [genesis]
            self.save()

    def save(self):
        with open(self.path, "w") as f:
            json.dump([b.__dict__ for b in self.chain], f, indent=2)

    def add(self, data):
        """Append a record and return the mined block (our 'transaction receipt')."""
        block = Block(len(self.chain), time.time(), data, self.chain[-1].hash)
        block.mine()
        self.chain.append(block)
        self.save()
        return block

    def find_file_hash(self, file_hash):
        for b in self.chain:
            if b.data.get("file_hash") == file_hash:
                return b
        return None

    def is_valid(self):
        """Re-check every block's hash, proof-of-work, and link to its parent."""
        for prev, cur in zip(self.chain, self.chain[1:]):
            if cur.hash != cur.compute_hash():
                return False, f"block {cur.index} contents were altered"
            if not cur.hash.startswith("0" * DIFFICULTY):
                return False, f"block {cur.index} has invalid proof-of-work"
            if cur.previous_hash != prev.hash:
                return False, f"block {cur.index} link to block {prev.index} is broken"
        return True, "chain is valid"
