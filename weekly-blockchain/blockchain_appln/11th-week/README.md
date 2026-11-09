# Week 11 – Blockchain for IP Protection

Python 3, no external packages. Run in order:

```bash
python3 task1_hash.py         # SHA-256 fingerprint
python3 task2_timestamp.py    # register hash on local blockchain (chain.json)
python3 task3_tamper.py       # modify 1 character, compare hashes
python3 task4_verify.py       # verify original vs modified against the chain
python3 task5_license.py      # smart-contract licensing simulation
```

| File | Purpose |
|---|---|
| `original_work.py` | The original digital work being protected |
| `hashing.py` | SHA-256 file hashing (chunked), bit-difference helper |
| `blockchain.py` | Mock blockchain: blocks linked by hash, proof-of-work, timestamps, validation |
| `contracts/IPLicense.sol` | Solidity smart contract for Task 5 (deployable in Remix / Sepolia) |
| `task5_license.py` | Python simulation of the same contract logic |
| `output.txt` | Output from my run |

> Your timestamps and transaction hashes will differ from mine when you re-run. The file hash stays the same as long as `original_work.py` is unchanged byte-for-byte.

---

## 1. File Hashing for IP Fingerprint

**Hash of `original_work.py`:**
`83187274d3c8227cbef28d39d8b53e9446e1b77889b1b493653b37ba90df388d`

**Why it is a digital fingerprint:**
- **Deterministic:** the same file always gives the same 256-bit hash.
- **Unique in practice:** finding two different files with the same SHA-256 hash (a collision) is computationally infeasible.
- **One-way:** the file can't be reconstructed from the hash, so the hash can be published without revealing the work.
- **Fixed size:** a 1 KB script or a 4 GB video both produce a 64-character hex string, which is cheap to store on-chain.

## 2. Blockchain Timestamping for Proof of Ownership

A local proof-of-work blockchain was used (`blockchain.py`, difficulty 4), standing in for an Ethereum testnet.

| Field | Value |
|---|---|
| Block number | 1 |
| Transaction (block) hash | `000075dccb63b3f0dc8d58f014ce3c6c2688a33b9fee00e91717a6aba9f352c9` |
| Previous block hash | `00003e992f17b44da46e6be4a4a3dbf927969217a415822828f5f9ec06ef44e7` |
| Timestamp (UTC) | 2026-11-09T07:52:57Z |
| Owner | Aman Abraha Kasa |

**How this proves ownership at a point in time:**
The block holds the file hash, the owner and the timestamp, and its own hash covers all three plus the previous block's hash. Backdating or changing the record would change that block's hash, which would break the link from every later block, and all of them would have to be re-mined. On a public chain like Ethereum, that would also mean outpacing the entire network. So the record proves that whoever registered it **had this exact file no later than that timestamp**. It doesn't prove the registrant created the work, because someone could register a stolen file. It does give strong, independently checkable evidence of prior possession.

## 3. Tampering Detection

Change: `a, b = 0, 1` → `a, b = 1, 1` (one character)

| | SHA-256 |
|---|---|
| Original | `83187274d3c8227cbef28d39d8b53e9446e1b77889b1b493653b37ba90df388d` |
| Modified | `1247f7b583afd21b93e4fedfcec8ab9d7baf7f20a2f38e5cb891921abcdad862` |

**125 of 256 bits differ (49%).**

**What this implies:** this is the **avalanche effect**. A one-character change flips about half the output bits, so the two hashes look completely unrelated. Any modification, however small, is detectable, and there is no "almost matching" hash that would let an altered file pass. If the hash differs, the file's integrity is broken. The hash shows *that* a change happened, but not *where* or *what* changed.

## 4. Blockchain Verification Process

`task4_verify.py` result: the original **matches block 1**, and the modified file has **no match → altered**. When an attacker edits the owner name inside block 1, `is_valid()` reports `block 1 contents were altered`.

**Steps to prove originality in court or a licensing dispute:**
1. **Get the disputed file** exactly as it was shared, without opening and re-saving it (re-saving can change bytes).
2. **Hash it** with SHA-256 using a standard, independent tool (`sha256sum`, `certutil`), not only my own script, so the result doesn't depend on trusting my code.
3. **Look up the registration transaction** by its transaction hash on the blockchain. For a public chain, use a block explorer such as Etherscan, which the court can access independently.
4. **Verify the chain's integrity:** confirm the block is part of the valid chain and recompute the block's hash.
5. **Compare** the hash from step 2 with the hash stored on-chain.
   - **Match:** the shared file is byte-for-byte identical to what I registered at the block's timestamp. It is the original.
   - **No match:** the file has been altered, or it is a different work.
6. **Link the wallet to me:** sign a fresh message with the private key of the registering address to prove I control it, and add supporting evidence (drafts, version-control history, emails) to show authorship and not just possession.
7. **Present the record:** the transaction hash, block number, timestamp and a reproducible hash comparison that the other side can re-run themselves.

## 5. Digital Rights Enforcement

**Proposed mechanism** (`contracts/IPLicense.sol`):
- `registerWork(fileHash)`: the owner registers the hash. The first registration wins and duplicates are rejected.
- `grantAccess(fileHash, user, duration)`: **user-specific and time-limited** licenses. Only the owner can call it.
- `revokeAccess(fileHash, user)`: the owner can revoke a license early.
- `requestAccess(fileHash)`: returns whether the caller holds a valid license **and emits `AccessRequested` for every attempt**, granted or denied.
- `requestVerification(fileHash)`: logs `VerificationRequested` and returns the owner and registration time.

**Simulation results** (`task5_license.py`):

| Step | Event | Result |
|---|---|---|
| StudioX, day 1 (30-day license) | AccessRequested | granted |
| Bob (no license) | AccessRequested | **denied, logged** |
| Court verifies hash | VerificationRequested | registered = True |
| StudioX, day 31 | AccessRequested | **denied, license expired** |
| Bob tries to grant himself a license | — | rejected: "Not the owner" |

**How this helps track misuse and enforce rights:**
- **Tamper-proof audit trail:** every access or verification request is a permanent on-chain event with a requester address and timestamp. Unlicensed attempts (Bob) and use after a license expired (StudioX on day 31) are recorded and can't be erased.
- **Automatic enforcement:** license expiry and revocation are enforced by code. No reminder or manual check is needed, and only the owner can grant rights.
- **Evidence for disputes:** if a work turns up being used by someone with no `AccessGranted` event, or after their expiry date, the chain shows they were never licensed at that time.
- **Can be extended** to a payable `requestAccess` that sends royalties to the owner automatically.

**Limitations:** the contract controls access to the *record*, not to the file itself. Once someone downloads a copy, they can share it off-chain. In practice it is combined with encrypted storage, where the decryption key is released only to licensed addresses, and with monitoring, such as KodakOne's web crawler, to find copies being used outside the system.

---
*Note: the project uses a local mock blockchain because the work environment had no network access to an Ethereum testnet. To deploy for real, paste `contracts/IPLicense.sol` into remix.ethereum.org, compile with 0.8.20+, and deploy to Sepolia with MetaMask. Pass the file hash as `bytes32` with a `0x` prefix: `0x8318...388d`.*
