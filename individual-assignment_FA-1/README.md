# Library Lending Chain (Individual Assignment 1)

A blockchain-based library book lending tracker written in C for **Introduction to Blockchain Development**. The application records borrowing and returning events as chained, SHA-256-hashed and ECDSA-signed blocks, then validates the chain to detect tampering.

This implementation is complete for the assignment scope and is aligned with:
- `individual-assignment_FA-1/Individual-Assignment 1-[F:A](Aman-kasa).pdf`
- the shipped source code in `src/`
- the test suite in `tests/run_tests.sh`

## What the program does

- Loads `data/books.txt` and `data/members.txt` registries at startup
- Creates a genesis block when no chain file exists
- Supports `borrow` and `return` operations with registry and loan-state validation
- Persists chain records to `data/chain.txt`
- Validates chain integrity (index order, hash linkage, hash recomputation, signature verification)
- Detects tampering and refuses new writes on a compromised chain

## Requirements

- GCC (C11) and GNU Make
- OpenSSL development package (for `-lcrypto`)
- Bash, `sed`, `python3`, and `openssl` CLI (for `make test`)

## Build and run

From the project root (`individual-assignment_FA-1`):

```sh
make
./lending_tracker
```

Useful targets:

```sh
make clean
make asan
make test
```

## CLI commands

| Command | Description |
|---|---|
| `borrow <book_id> <member_id>` | Add a `BORROWED` block |
| `return <book_id>` | Add a `RETURNED` block |
| `view records` (or `view`) | Print chain records and signature validity |
| `validate chain` (or `validate`) | Validate and report chain status |
| `chain status` | One-line validity summary |
| `list books` | Show loaded book registry |
| `list members` | Show loaded member registry |
| `help` | Show available commands |
| `exit` / `quit` | End program |

## Project structure

```text
individual-assignment_FA-1/
├── src/                    # C modules: main, registry, blockchain, crypto, persistence, cli
├── data/                   # books.txt, members.txt (chain.txt generated at runtime)
├── keys/                   # private/public PEM keys generated at runtime
├── tests/                  # run_tests.sh
├── docs/                   # ARCHITECTURE.md, REPORT.md, REQUIREMENTS.md, TRACEABILITY.md
├── Makefile
├── README.md
└── Individual-Assignment 1-[F:A](Aman-kasa).pdf
```

## Testing and evidence

Run:

```sh
make test
```

`tests/run_tests.sh` executes the real binary in isolated sandboxes and verifies registry handling, borrow/return logic, persistence, cryptography, tamper detection, and ASan/UBSan behavior.

## Cryptographic design

- **Hashing:** SHA-256 over block content fields (excluding `signature` and `hash`)
- **Signing:** ECDSA (P-256) signature over the block hash
- **Verification:** each block is checked during validation
- **Key handling:** keys are generated/loaded from `keys/private.pem` and `keys/public.pem`; private key is runtime-managed and not embedded in source

## Persistence and tamper detection

- Chain storage format (one block per line):

```text
index|timestamp|book_id|book_title|member_id|member_name|action|previous_hash|signature_hex|hash
```

- On startup, the program loads and validates the chain
- If tampering is detected, it reports the first failing block and reason
- `borrow` / `return` are blocked until chain integrity is restored

## Completion status

The implementation, documentation set, and automated test workflow for Individual Assignment 1 are present in this repository and aligned with the submitted assignment scope.
