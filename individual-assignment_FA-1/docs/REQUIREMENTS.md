# Requirements Checklist — Individual Assignment 1

Source references used for this checklist:
- `Individual-Assignment 1-[F:A](Aman-kasa).pdf`
- implemented code in `src/`
- automated evidence in `tests/run_tests.sh`

Verification labels:
- **(test)** validated in `make test`
- **(inspection)** validated from source and behavior review
- **(doc)** documented architecture/design behavior

## R1 — Registries

- [x] Load books registry at startup (test)
- [x] Load members registry at startup (test)
- [x] Reject unknown `book_id` in borrow/return paths (test)
- [x] Reject unknown `member_id` in borrow path (test)
- [x] Handle missing registry files with clear fatal errors (test)
- [x] Handle empty registry files with clear fatal errors (test)
- [x] Skip malformed/oversized lines safely (test)

## R2 — Block structure

- [x] Block includes assignment-required lending-chain fields (inspection)
- [x] Genesis block has index 0 and zero `previous_hash` (test)
- [x] Action values used by workflow are `BORROWED` / `RETURNED` (+ `GENESIS`) (inspection/test)
- [x] Chain structure is linked-list based (inspection)

## R3 — Borrow operation

- [x] Validates book and member IDs before creating a record (test)
- [x] Rejects duplicate borrow of already-loaned book (test)
- [x] Copies canonical title/member name from registry records (test)
- [x] Computes hash/signature and appends block on success (test)
- [x] Persists updated chain after successful append (test)

## R4 — Return operation

- [x] Validates return request against chain state (test)
- [x] Rejects returning books not currently borrowed (test)
- [x] Appends signed `RETURNED` block on valid return (test)
- [x] Persists updated chain after successful append (test)

## R5 — Chain validation

- [x] Checks index sequence (test)
- [x] Checks `previous_hash` linkage (test)
- [x] Recomputes and verifies stored hash (test)
- [x] Verifies signatures with public key (test)
- [x] Reports first failing block with reason when compromised (test)

## R6 — View records

- [x] Displays lending-chain records from in-memory chain (inspection)
- [x] Shows signature validity per record (inspection/test)

## R7 — Tamper detection

- [x] Persisted chain file can be externally modified for demonstration (test)
- [x] Tampering is detected after reload/validation (test)
- [x] Writes are refused when chain is compromised (test)

## R8 — CLI behavior

- [x] Supports assignment-required commands (`borrow`, `return`, `view records`, `validate chain`) (test)
- [x] Initializes chain at startup (load or genesis creation) (test)
- [x] Supports exit/quit behavior (inspection/test)
- [x] Rejects unknown commands with helpful message (test)

## R9 — Persistence

- [x] Chain survives program restart (test)
- [x] Startup sequence validates chain before trusting records (inspection)
- [x] Parse-level corruption is handled as fatal load error (inspection/test)
- [x] Serialization format is documented and implemented consistently (doc/inspection)

## R10 — Cryptography

- [x] SHA-256 implemented through OpenSSL EVP APIs (inspection)
- [x] ECDSA signing/verification implemented through OpenSSL EVP APIs (inspection/test)
- [x] Private key is not hardcoded in source (inspection)
- [x] Public/private keys are loaded/generated from runtime key files (inspection)

## R11 — Safety and robustness

- [x] Bounded parsing/copying used for fixed-size buffers (inspection)
- [x] Invalid input paths return errors without crashes (test)
- [x] Memory-safety session included in test workflow (`make asan`) (test)

## Deliverables in this repository

- [x] C source implementation (`src/`)
- [x] Build/test automation (`Makefile`, `tests/run_tests.sh`)
- [x] README and supporting documentation (`README.md`, `docs/*.md`)
- [x] Assignment report PDF (`Individual-Assignment 1-[F:A](Aman-kasa).pdf`)

## Status

The assignment implementation in this repository is complete for the documented project scope, and its behavior is backed by code inspection plus the existing automated tests.
