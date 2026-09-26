# Technical Report — Blockchain-Based Library Book Lending Tracker

## 1. Introduction

This project implements Individual Assignment 1: a blockchain-based library lending tracker in C. The system records borrow and return events as append-only blocks, protected by SHA-256 hashing and ECDSA signatures.

## 2. Problem statement

Traditional lending logs can be modified without clear evidence. The assignment requires a tamper-evident ledger where changes to historical records are detectable through hash-linking and signature verification.

## 3. Objectives delivered

- Build a blockchain-style linked list in C
- Validate books and members from file-based registries
- Record borrow/return operations as chained blocks
- Persist and reload chain state from disk
- Validate chain integrity and report compromise reasons
- Use OpenSSL SHA-256 and ECDSA for integrity/authentication
- Provide a CLI for core operations

## 4. System architecture

Implementation is split into six modules:
- `main.c` (startup orchestration)
- `registry.c` (registries)
- `blockchain.c` (block lifecycle and validation)
- `crypto.c` (OpenSSL wrappers)
- `persistence.c` (file format and parsing)
- `cli.c` (commands and output)

See `docs/ARCHITECTURE.md` for detailed design and flow.

## 5. Block and chain design

Each block stores:
- index, timestamp
- book and member identifiers/details
- action (`GENESIS`, `BORROWED`, `RETURNED`)
- `previous_hash`, `hash`, and `signature`

The chain is maintained as a linked list with `head`, `tail`, and `length`.

## 6. Registry management

At startup, `books.txt` and `members.txt` are loaded into bounded in-memory arrays. Borrow/return requests are accepted only when IDs exist in the corresponding registries.

Malformed or oversized registry lines are skipped with warnings; missing/empty registries are fatal startup errors.

## 7. Security mechanisms

### 7.1 SHA-256 hashing

Blocks are serialized deterministically and hashed with SHA-256 (OpenSSL EVP). The hash covers all block content fields except signature and hash fields.

### 7.2 ECDSA signatures and keys

- One P-256 keypair is generated/loaded from `keys/`
- Each block hash is signed
- Validation verifies signatures with the public key
- Private key is runtime-managed, not hardcoded in source

## 8. Persistence

Chain records are persisted in `data/chain.txt` (one line per block):

```text
index|timestamp|book_id|book_title|member_id|member_name|action|previous_hash|signature_hex|hash
```

`persistence_load()` checks structural validity; cryptographic trust is verified by `blockchain_validate()`.

## 9. Core operations and validation

- `borrow <book_id> <member_id>`: validates IDs and loan state, appends signed block
- `return <book_id>`: validates current state and appends signed block
- `view records`: prints records and per-block signature status
- `validate chain` / `chain status`: reports `VALID` or `COMPROMISED` with first failing index and reason

If the chain is compromised, new writes are refused.

## 10. Error handling

Three levels are implemented:
- **Fatal startup errors:** unusable registries, key load/create failures, parse-corrupt chain file
- **Non-fatal integrity warnings:** chain parses but fails cryptographic validation
- **Command-level errors:** invalid IDs, invalid command forms, invalid lending state transitions

## 11. Testing and results

`make test` runs `tests/run_tests.sh`, which executes the built binary in isolated sandboxes and checks:
- registry loading and validation behavior
- borrow/return rule enforcement
- chain creation/reload behavior
- signature and key verification behavior
- tamper scenarios and write refusal on compromised chains
- sanitizer-assisted memory-safety session (`make asan` path)

The test script is the authoritative evidence source for implemented behavior.

## 12. Challenges and resolutions

- **Variable-length ECDSA signatures:** handled via in-memory `sig_len`
- **Portable persistence format:** plain text serialization avoids struct-layout portability issues
- **Tamper demo vs safety:** startup warns on compromised chains, but writes are blocked
- **POSIX API availability under C11:** `-D_POSIX_C_SOURCE=200809L` added in `Makefile`

## 13. Conclusion

The repository contains a completed implementation of the assignment’s library lending blockchain tracker, including registry validation, hashed and signed block creation, persistence, chain validation, tamper detection, and CLI operations, with supporting documentation and automated tests.
