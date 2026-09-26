# Requirements Checklist — Phase 1

Source of truth: `individual assignment.pdf` (graded spec + rubric). `prompt.pdf` (engineering workflow) cross-checked against it — where they conflict, the assignment PDF wins, per the project plan's Standing Rule 1.

Naming note: the assignment's block table uses `Member_name` and calls this an "attendance chain" — both are leftover template wording. This checklist and the implementation use `member_name` and "lending chain" throughout. See the project plan for the full note.

**Verification key**: every `[x]` below is backed by one of:
- **(test)** — an automated assertion in `tests/run_tests.sh` (`make test`, 44/44 passing)
- **(inspection)** — confirmed by reading the code/output directly, not by an automated assertion (used only where a test would be redundant with a one-line source read, e.g. "is X ever printed")
- **(doc)** — a documentation-only requirement, satisfied by content in `docs/ARCHITECTURE.md`

## R1 — Registries

- [x] R1.1 Load `books.txt` into an array of `Book` structs at startup (test)
- [x] R1.2 Load `members.txt` into an array of `Member` structs at startup (test)
- [x] R1.3 Reject a borrow/return referencing an unknown `book_id` (test)
- [x] R1.4 Reject a borrow/return referencing an unknown `member_id` (test)
- [x] R1.5 Print a clear error if `books.txt` is missing (test)
- [x] R1.6 Print a clear error if `members.txt` is missing (test)
- [x] R1.7 Print a clear error if either file is present but empty (test, both files)
- [x] R1.8 Malformed lines (wrong field count, oversized fields) handled without crashing (test, both cases)

## R2 — Block structure

- [x] R2.1 Block contains all 10 spec'd fields (inspection: `src/blockchain.h`, plus a persisted-file field-count/length check during manual verification)
- [x] R2.2 `action` holds `BORROWED`/`RETURNED` (test, both values exercised). `OVERDUE` is structurally supported (`ACTION_LEN` fits it) but never produced — see ambiguity #3, this is intentional, not a gap.
- [x] R2.3 `previous_hash` holds the SHA-256 hash of the previous block (test — linkage checked directly, and tamper tests confirm it's actually enforced, not just present)
- [x] R2.4 `hash` is the SHA-256 hash of the block's other fields (test — tamper-block-data test proves recomputation actually happens)
- [x] R2.5 `signature` is an ECDSA signature of the block data (test — valid-signature, corrupted-signature, and wrong-public-key tests)
- [x] R2.6 Genesis block: `index = 0`, `previous_hash` = 64 zeros (test, both the positive case and a corrupted-genesis negative case)

## R3 — Borrow

- [x] R3.1 Validate `book_id` against the book registry (test)
- [x] R3.2 Validate `member_id` against the member registry (test)
- [x] R3.3 On either invalid: print `ERROR: Book or Member not found`, abort (test)
- [x] R3.4 Reject if the book is already on loan (test)
- [x] R3.5 Copy canonical `book_title` / `member_name` from the registries, not user input (test — reload test asserts the exact copied title/name)
- [x] R3.6 Set timestamp, `previous_hash`, compute `hash`, compute `signature` (test, via chain-validity checks on the resulting block)
- [x] R3.7 Append to chain, persist to disk, report success clearly (test)

## R4 — Return

- [x] R4.1 Validate `book_id` (test)
- [x] R4.2 Find the book's most recent `BORROWED` entry (test, via the already-returned/never-borrowed cases)
- [x] R4.3 Error if none exists — no block created (test)
- [x] R4.4 Create a `RETURNED` block, preserving book/member info (test)
- [x] R4.5 Set `previous_hash`, compute `hash`, compute `signature` (test, via chain-validity checks)
- [x] R4.6 Append, persist, report success (test)

## R5 — Validate chain

- [x] R5.1 Verify genesis block correctness (test — corrupted-genesis-previous_hash case)
- [x] R5.2 Recompute and compare every block's `hash` (test — modify-block-data and modify-stored-hash cases)
- [x] R5.3 Verify every block's `previous_hash` matches the prior block's `hash` (test — modify-previous_hash case)
- [x] R5.4 Verify signature validity per block (test — corrupted-signature and wrong-public-key cases)
- [x] R5.5 Verify index/order consistency (test — duplicated-index case)
- [x] R5.6 Report clearly whether the chain is VALID or COMPROMISED (test, throughout)

## R6 — View records

- [x] R6.1 Print index, book title, book ID, member name, member ID, action, timestamp, hash, previous hash, signature validity (inspection: `cli.c: print_records` — every field is printed; covered indirectly by the R3.5 reload test, which greps this exact output)
- [x] R6.2 Output is readable in a terminal (inspection — plain aligned text, no binary/control characters)

## R7 — Tamper detection

- [x] R7.1 Chain persists in a format that can be modified externally (test — sed/python-based edits against the real file used throughout)
- [x] R7.2 Reloading a tampered file and running Validate Chain reports failure (test — single-external-byte case specifically, plus 5 other tamper variants)
- [x] R7.3 A working chain and a deliberately broken one, both demonstrable (test — "untouched chain has no INVALID signatures" vs. every tamper case; the demo script in `docs/REPORT.md` walks this live)

## R8 — CLI

- [x] R8.1 `borrow`, `return`, `view records`, `validate chain` (test, all four)
- [x] R8.2 Load/init on startup (test — genesis-creation and reload-without-recreating cases)
- [x] R8.3 Exit command (inspection: `cli.c` — `exit`/`quit` break the loop; exercised in every test)
- [x] R8.4 Invalid commands produce a helpful message (test — memory-safety session includes `badcommand`, and inspection confirms the specific message)
- [x] R8.5 (optional, adopted) `help`, `list books`, `list members`, `chain status` (test, all four exercised)

## R9 — Persistence

- [x] R9.1 Chain survives program termination (test — separate-process reload case)
- [x] R9.2 Startup order: registries → chain (or genesis) → validate before trusting (inspection: `main.c`, matches exactly; the validation step itself is test-covered via R5/R7)
- [x] R9.3 Corrupted/tampered persisted data is flagged, not silently trusted (test, throughout Tampering section)
- [x] R9.4 Serialization format documented (doc: `docs/ARCHITECTURE.md` "Persistence strategy" — plain-text delimited, chosen specifically to sidestep padding/endianness/fixed-width concerns)

## R10 — Cryptography

- [x] R10.1 SHA-256 via OpenSSL EVP API (inspection: `crypto.c` uses `EVP_Digest`, no deprecated `SHA256_*` calls)
- [x] R10.2 ECDSA used correctly for signing and verification (test — sign/verify exercised on every block; corrupted-signature and wrong-key tests confirm verification actually discriminates)
- [x] R10.3 Private key never stored in source code (inspection: `git grep` for key material in `src/` — none; key is generated/loaded at runtime into `keys/`, which is gitignored)
- [x] R10.4 Private key never printed unnecessarily (inspection: `git grep -n "priv"` across `src/` — the private key's bytes are never passed to any `printf`/`fprintf`)
- [x] R10.5 What is hashed vs. signed is documented and distinct (doc: `docs/ARCHITECTURE.md` "Cryptographic flow")

## R11 — Memory safety / C quality

- [x] R11.1 No buffer overflows; fixed buffers bounds-checked (test — ASan session; inspection — every copy uses `snprintf` with the destination's `sizeof`)
- [x] R11.2 No unsafe functions (inspection: `git grep` for `strcpy\|strcat\|gets(\|scanf(` outside of bounded `%Ns`/`sscanf` forms. This check caught one real instance — a `strcpy` in `blockchain.c`'s validation loop, both buffers bounded so not exploitable, but non-compliant with this rule regardless — replaced with `snprintf`. Re-checked clean after the fix; all 44 tests still pass.)
- [x] R11.3 Allocations checked for failure; freed exactly once **for the paths exercised by the test session** — the `calloc` failure branch itself (out-of-memory) and the `seal_block`-fails-so-free-and-return-error branch (crypto failure) are not exercised by any test, since neither is reachable without fault injection. Not marking this a full pass beyond what was actually run.
- [x] R11.4 Clean under AddressSanitizer/UBSan for the exercised session (test — zero AddressSanitizer/UBSan/LeakSanitizer output across a session covering every command and both success/error paths of borrow and return)

## Deliverables (Compliance criterion, 4 pts)

- [x] D1 Complete C source code, pushed to a GitHub repo, 
- [ ] D2 Demo video, 3–5 min — **not recorded** (script ready, see `docs/REPORT.md`)
- [x] D3 Technical report — `docs/REPORT.md`
- [x] D4 README — `README.md`

## Ambiguities / risks identified

1. **"Attendance chain" / `Member_name`** — resolved: library lending chain, `member_name` lowercase. (see plan)
2. **`action[10]` (char array, not `char[10]`)** — spec typo; treated as `char action[10]`, matching every other field's declared style.
3. **OVERDUE is listed as a valid action but no functional requirement defines when/how it's triggered** — no automatic overdue logic is specified anywhere (no due-date field exists on the block or registries). Resolution: implement `action` as an enum-safe string field that accepts OVERDUE for future extensibility, but do not build automatic overdue detection — it's not a stated requirement and inventing one would violate Standing Rule 3. Documented in the report as an intentionally unimplemented, spec-absent feature.
4. **Signature buffer `unsigned char[72]`** — ECDSA (P-256) DER signatures are variable-length, up to ~72 bytes; stored left-aligned in the fixed buffer with an explicit length tracked separately in memory (`Block.sig_len`), since the struct has no length field. Not persisted or hashed. See `docs/ARCHITECTURE.md`.
5. **"Digital signatures to authenticate lending actions" vs. rubric "proper use of cryptographic keys"** — implies a persistent ECDSA keypair, stored outside source (in `keys/`), not regenerated per run. Adopted.

## Status

Phase 8 (testing) is complete: 44/44 automated tests pass (`make test`), covering every R1–R11 item above via test, inspection, or doc reference as marked. Remaining gap is D2, the demo video — recording requires screen-capture tooling outside this session, tracked in the project plan, not silently dropped.
