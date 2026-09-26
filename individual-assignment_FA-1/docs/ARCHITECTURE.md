# Architecture — Individual Assignment 1

This document describes the final implementation in `individual-assignment_FA-1/src/`, aligned with the assignment PDF and the delivered code.

## 1) Module structure

```text
src/
├── main.c         # startup sequence, wiring, lifecycle
├── registry.c/.h  # book/member registry loading and lookup
├── blockchain.c/.h# block model, borrow/return logic, validation
├── crypto.c/.h    # SHA-256 + ECDSA key/sign/verify helpers (OpenSSL EVP)
├── persistence.c/.h # chain file serialization/deserialization
└── cli.c/.h       # command parsing and terminal interaction
```

Supporting directories:

- `data/`: `books.txt`, `members.txt`, runtime `chain.txt`
- `keys/`: runtime-generated `private.pem`, `public.pem`
- `tests/`: integration-style shell tests (`run_tests.sh`)

## 2) Core data structures

### Registry data (`registry.h`)
- `Book { book_id, title, author }`
- `Member { member_id, full_name, course_code }`
- `Registry` with fixed-size arrays (`MAX_BOOKS`, `MAX_MEMBERS`)

### Blockchain data (`blockchain.h`)
- `Block` fields include assignment-required chain fields:
  - `index`, `timestamp`, `book_id`, `book_title`, `member_id`, `member_name`, `action`, `previous_hash`, `signature`, `hash`
- In-memory-only fields:
  - `sig_len` (actual DER signature length)
  - `next` (linked-list pointer)
- `Blockchain { head, tail, length }`

## 3) Startup and lifecycle flow

`main.c` executes this sequence:

1. Load registries (`data/books.txt`, `data/members.txt`)
2. Load or create ECDSA keypair (`keys/private.pem`, `keys/public.pem`)
3. Load chain from `data/chain.txt`
   - if not found: create and persist genesis block
   - if parse-corrupt: fail fast
4. Validate chain cryptographically
5. Enter CLI loop
6. Free chain and key resources on exit

## 4) Operation flow

### Borrow
1. Re-validate chain (`refuse_if_compromised`)
2. Parse and validate IDs against registries
3. Reject if book is already on loan
4. Create block (`BORROWED`), set `previous_hash`
5. Hash and sign block
6. Append block and persist chain

### Return
1. Re-validate chain
2. Validate `book_id`
3. Find most recent record for the book
4. Reject if book is not currently borrowed
5. Create `RETURNED` block from canonical previous lending info
6. Hash/sign, append, and persist

## 5) Cryptographic design

- Hash function: SHA-256 (OpenSSL EVP)
- Signature: ECDSA P-256 (OpenSSL EVP)
- Hash input serialization order:

```text
index|timestamp|book_id|book_title|member_id|member_name|action|previous_hash
```

- `hash` stores SHA-256 output as lowercase hex
- `signature` stores DER bytes (persisted as hex in file)

## 6) Validation model

`blockchain_validate()` checks each block in order:

1. Index sequence correctness
2. `previous_hash` linkage correctness
3. Recomputed SHA-256 equals stored `hash`
4. Signature verifies against stored `hash`

The first error is returned with block index and reason.

## 7) Persistence format and behavior

`persistence_save()` writes one line per block:

```text
index|timestamp|book_id|book_title|member_id|member_name|action|previous_hash|signature_hex|hash
```

`persistence_load()` validates parse-level integrity (shape/types/lengths) and reconstructs blocks. Cryptographic trust checks are intentionally centralized in `blockchain_validate()` after load.

## 8) Tamper-detection behavior

- Any edit to hashed fields changes recomputed hash
- Any edit that breaks lineage causes `previous_hash` mismatch
- Signature validation provides independent integrity/authenticity check
- On compromised chain, the CLI still allows inspection (`view`, `validate`) but blocks new writes (`borrow`, `return`)

## 9) Assignment-completion alignment

The implemented architecture satisfies the assignment’s core requirements for:
- registry-based validation,
- blockchain record creation and linking,
- SHA-256 + ECDSA protection,
- persisted chain reload,
- validation and tamper reporting,
- command-line operation.
