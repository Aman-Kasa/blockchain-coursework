# Requirement Traceability Matrix — Individual Assignment 1

This matrix links assignment requirements to concrete implementation locations and repository evidence.

## 1. Functional requirement traceability

| Requirement area | Implementation evidence | Validation evidence |
|---|---|---|
| Registry loading and validation | `src/registry.c`, `src/registry.h`, startup in `src/main.c` | `tests/run_tests.sh` (REGISTRY section) |
| Genesis creation | `blockchain_create_genesis()` in `src/blockchain.c` | `tests/run_tests.sh` (genesis creation/reload checks) |
| Borrow workflow | `handle_borrow()` (`src/cli.c`), `blockchain_borrow()` (`src/blockchain.c`) | `tests/run_tests.sh` (BORROW section) |
| Return workflow | `handle_return()` (`src/cli.c`), `blockchain_return()` (`src/blockchain.c`) | `tests/run_tests.sh` (RETURN section) |
| Hashing and block sealing | `serialize_for_hash()`, `seal_block()` (`src/blockchain.c`), SHA helpers in `src/crypto.c` | `tests/run_tests.sh` (BLOCKCHAIN/TAMPERING/CRYPTOGRAPHY sections) |
| Signature generation and verification | `crypto_sign_hash()`, `crypto_verify_hash()` in `src/crypto.c` | `tests/run_tests.sh` (CRYPTOGRAPHY section) |
| Chain validation | `blockchain_validate()` in `src/blockchain.c`; status printing in `src/cli.c` | `tests/run_tests.sh` (`validate chain` and compromise checks) |
| Persistence | `persistence_save()`, `persistence_load()` in `src/persistence.c` | `tests/run_tests.sh` (reload and tamper scenarios) |
| Tamper detection and write refusal | startup warning path in `src/main.c`, write guard in `refuse_if_compromised()` (`src/cli.c`) | `tests/run_tests.sh` (tamper + refused write checks) |
| CLI command interface | `cli_run()` in `src/cli.c` | `tests/run_tests.sh` (command-path coverage) |

## 2. Assignment deliverable traceability

| Deliverable | Repository location |
|---|---|
| Implementation source code | `src/` |
| Build and test procedure | `Makefile`, `tests/run_tests.sh` |
| README | `README.md` |
| Architecture, report, requirements, traceability docs | `docs/ARCHITECTURE.md`, `docs/REPORT.md`, `docs/REQUIREMENTS.md`, `docs/TRACEABILITY.md` |
| Assignment PDF artifact | `Individual-Assignment 1-[F:A](Aman-kasa).pdf` |

## 3. Quality gate summary

- The implementation covers the assignment’s lending-chain functionality: registry validation, block creation/linking, cryptographic integrity/authentication, persistence, CLI operations, and tamper detection.
- The existing automated test workflow (`make test`) exercises these areas through end-to-end command-path scenarios.
- Documentation in this folder has been aligned to the implemented behavior and repository evidence.
