# Library Lending Chain


A blockchain-backed library book lending tracker written in C. Every borrow and
return is recorded as a SHA-256-hashed, ECDSA-signed block, chained to the one
before it, so a tampered lending history can be detected instead of quietly
trusted.

> **Naming note:** the original assignment spec calls this an "attendance
> chain" in one table and every requirement is actually about book lending —
> this is leftover wording from a different template. See
> [`docs/REPORT.md`](docs/REPORT.md) for the full note. This codebase uses
> "lending chain" throughout.

## Features

- Book and member registries loaded from `data/books.txt` / `data/members.txt`, validated at startup
- A linked-list blockchain: genesis block, SHA-256-linked blocks, ECDSA (P-256) signatures
- `borrow` / `return` commands that reject unknown IDs and double-borrows/returns
- `validate chain` — recomputes every hash, checks every link, verifies every signature
- Plain-text, human-editable persistence — designed so a single-byte external edit is both easy to demonstrate and reliably detected on reload
- A CLI that refuses to write new records on top of a chain that's already been found compromised

## Architecture

See [`docs/ARCHITECTURE.md`](docs/ARCHITECTURE.md) for the full design (module
responsibilities, data flow, cryptographic flow, persistence format). In short:

```
books.txt ──► registry.c ──┐
                             ├──► blockchain.c ──► crypto.c (SHA-256 + ECDSA)
members.txt ─► registry.c ─┘            │
                                         ▼
                                  persistence.c ──► data/chain.txt
```

## Requirements

- GCC (or another C11 compiler) and GNU Make
- OpenSSL 3.x development headers (`libssl-dev` on Debian/Ubuntu, `openssl-devel` on Fedora)
- Bash + `sed`, `python3`, and the `openssl` CLI tool (only for `make test`, not for running the program)

Verified on Linux with gcc and GNU Make against OpenSSL 3.6.4 and OpenSSL 3.0.13.

## Installation & compilation

```sh
git clone https://github.com/Aman-Kasa/blockchain-coursework.git
cd blockchain-coursework/individual-assignment_FA-1
make
```

This produces a `lending_tracker` binary in `individual-assignment_FA-1/` (the project root for everything below). `make clean`
removes build artifacts.

## Running

```sh
./lending_tracker
```

Run it from the project root — it expects `data/books.txt` and
`data/members.txt` to exist there, and will create `data/chain.txt` and a
`keys/` ECDSA keypair on first run if they don't already exist.

## CLI commands

| Command | Effect |
|---|---|
| `borrow <book_id> <member_id>` | Record a book as borrowed |
| `return <book_id>` | Record a book as returned |
| `view records` | Show every block on the chain, with signature validity |
| `validate chain` | Recompute hashes/links/signatures; report VALID or COMPROMISED |
| `list books` | Show the book registry |
| `list members` | Show the member registry |
| `chain status` | One-line block count + validity |
| `help` | Show the command list |
| `exit` | Quit |

Example session:

```
> borrow BK001 ALU001
OK: BK001 borrowed by ALU001 (block 1 recorded).
> borrow BK001 ALU002
ERROR: this book is already on loan
> return BK001
OK: BK001 returned (block 2 recorded).
> validate chain
Chain status: 3 block(s), VALID
```

## Test procedure

```sh
make test
```

Runs `tests/run_tests.sh`: 44 automated assertions across registry loading,
borrow, return, blockchain validation, cryptography, tampering, and a full
AddressSanitizer/UndefinedBehaviorSanitizer session — each test runs in its
own throwaway sandbox directory, so nothing touches the real `data/`/`keys/`.

`make asan` builds a sanitizer-instrumented binary directly, if you want to
run your own session under it (`./lending_tracker`, then check stderr).

## Tamper-detection procedure

This is the core security demonstration the assignment requires. To reproduce
it manually:

1. `make && ./lending_tracker`, then `borrow BK001 ALU001`, `exit`.
2. Open `data/chain.txt` in a text editor. Each line is one block:
   `index|timestamp|book_id|book_title|member_id|member_name|action|previous_hash|signature_hex|hash`
3. Change one visible character in the second line's `book_title` field (e.g. "Things Fall Apart" → "Xhings Fall Apart"). Save.
4. `./lending_tracker`, then `validate chain`.

Expected result: a `WARNING: chain integrity check FAILED` on startup, and
`Chain status: N block(s), COMPROMISED` with the exact block index and reason
(`stored hash does not match recomputed hash`) from `validate chain`. A
subsequent `borrow` or `return` is refused with an explicit error rather than
silently appending onto the broken chain.

`tests/run_tests.sh`'s Tampering section automates six variants of this
(block-data edit, stored-hash edit, previous_hash edit, a genuine
single-external-byte flip via a small Python snippet, a corrupted signature
with the hash untouched, and a wrong public key).

## Cryptographic design

- **Hashing**: SHA-256 over every block field except `signature` and `hash`
  itself (`index, timestamp, book_id, book_title, member_id, member_name,
  action, previous_hash`), via OpenSSL's `EVP_Digest`.
- **Signing**: the resulting hash is ECDSA-signed (P-256 curve) via
  `EVP_DigestSign`, using a keypair generated once and stored as PEM under
  `keys/` (gitignored — never committed, never embedded in source, never
  printed).
- **Verification**: `EVP_DigestVerify` against the stored public key.
  Verification never fails open — any error path is treated as "invalid."

Full rationale, including exactly why tampering with an earlier block breaks
every later block's link, is in [`docs/ARCHITECTURE.md`](docs/ARCHITECTURE.md)
and [`docs/REPORT.md`](docs/REPORT.md).

## Project structure

```
individual-assignment_FA-1/
├── src/                 main.c, registry, blockchain, crypto, persistence, cli
├── data/                books.txt, members.txt (chain.txt is generated, gitignored)
├── keys/                ECDSA keypair, generated on first run (gitignored)
├── docs/                REQUIREMENTS.md, ARCHITECTURE.md, REPORT.md, TRACEABILITY.md
├── tests/               run_tests.sh — the automated test matrix
├── Makefile
└── README.md
```

## Known limitations

- `view records` checks each block's signature against the block's *stored*
  hash. A block whose data was edited but whose stored hash was not changed
  still shows `signature=VALID` there. Use `validate chain`, which recomputes
  every hash, to decide whether the chain is intact.
- Deleting the newest block(s) from the end of `data/chain.txt` is not
  detected: blocks only link backwards and nothing outside the file records
  the latest hash or the chain length. Deleting the whole file starts a new chain.
- One ECDSA key pair per installation, stored unencrypted (mode `0600`) next to
  the chain. It authenticates the installation, not individual users. There
  is no login or role-based access control.
- `OVERDUE` fits in the `action` field but is never produced, because the
  assignment defines no due-date rule.

## Troubleshooting

- **`fatal error: openssl/evp.h: No such file or directory`** — install
  OpenSSL development headers (see Requirements above), not just the runtime library.
- **`FATAL: could not load registries`** — run the binary from the project
  root, or confirm `data/books.txt` / `data/members.txt` exist and aren't empty.
- **`WARNING: chain integrity check FAILED` on a chain you didn't intend to
  tamper with** — the persisted `data/chain.txt` or `keys/public.pem` was
  edited or replaced since the chain was written. Restore from git or delete
  `data/chain.txt` to start a fresh chain (this also invalidates prior
  history — that's the point).
- **Permission denied writing to `keys/`** — the private key file is created
  with owner-only permissions (`chmod 600`); if it already exists with
  different ownership (e.g. copied from another machine), fix its
  permissions or delete it to regenerate.
