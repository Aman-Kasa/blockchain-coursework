# Technical Report — Library Lending Chain

## 1. Introduction

This report documents the design and implementation of a blockchain-based
library book lending tracker, built for the assignment "Design and
Implementation of a Blockchain-Based Library Book Lending Tracker." The system
replaces a mutable paper/database lending log with an append-only,
cryptographically verifiable chain of borrow/return records, so that neither a
librarian nor a borrower can quietly alter what actually happened.

## 2. Problem statement

Traditional library lending records live in a table or file that can be
edited after the fact — a librarian can mark a lost book "returned," or a
borrower can dispute what a paper log shows, and there's no way to prove
which version is authentic. The assignment asks for a system where every
lending event is recorded in a way that makes silent alteration detectable.

## 3. System objectives

1. Understand and implement a basic blockchain data structure in C.
2. Use SHA-256 to make block tampering detectable.
3. Use ECDSA digital signatures to authenticate who/what produced each block.
4. Validate book/member identities against file-based registries before any
   lending action is recorded.
5. Persist the chain across program runs, safely — never trusting persisted
   data without re-verifying it.
6. Apply defensive error handling throughout (missing files, malformed input,
   invalid IDs, invalid commands).

## 4. Spec inconsistency — noted, not silently followed

The assignment's Block Structure table labels this an "attendance chain" and
uses the field name `Member_name` (capitalized, inconsistent with the
otherwise snake_case fields). Every actual functional requirement — the
objectives, the description, Borrow/Return/Validate/View — describes a
**library lending** system, not attendance tracking. This is almost certainly
leftover wording copied from a different assignment template.

**Resolution:** the system is implemented and documented throughout as a
library lending chain, with the field named `member_name` (lowercase, for
consistency with `book_id`, `member_id`, etc.). This is flagged here
explicitly rather than either blindly copying the inconsistent wording or
silently "fixing" it without a note, per the project's working rules.

## 5. System architecture

```
books.txt ──► registry.c ──┐
                             ├──► blockchain.c ──► crypto.c (SHA-256 + ECDSA)
members.txt ─► registry.c ─┘            │
                                         ▼
                                  persistence.c ──► data/chain.txt
```

Six modules, each with one responsibility (full rationale in
`docs/ARCHITECTURE.md`):

| Module | Owns |
|---|---|
| `registry.c/h` | Book/Member structs, file loading, ID lookup |
| `crypto.c/h` | SHA-256 hashing, ECDSA keygen/sign/verify (OpenSSL EVP) |
| `blockchain.c/h` | Block/chain structs, genesis, append, borrow/return rules, validation |
| `persistence.c/h` | On-disk format, save, load, corruption detection |
| `cli.c/h` | Command parsing, dispatch, user-facing messages |
| `main.c` | Startup sequence: load registries → load/create chain → validate → run CLI |

This mirrors the project structure suggested in the engineering prompt, with
one deliberate deviation: there is no separate `validation.c` module.
Validation logic (registry line validation, chain validation) lives directly
inside the module that owns the data being validated (`registry.c`,
`blockchain.c`) rather than in a cross-cutting module, since in practice each
validation routine is tightly coupled to one data structure's invariants and
splitting it out added indirection without adding clarity.

## 6. Blockchain implementation

The chain is a singly linked list of `Block` structs (`Blockchain.head` /
`.tail` / `.length`), as the assignment's design guidance specifies. Each
block is heap-allocated (`calloc`) and freed exactly once, either individually
on an aborted append (e.g. a signing failure) or via `blockchain_free()`
walking the whole list at shutdown.

Appending a block (`borrow`/`return`/genesis) always follows the same
sequence: allocate → fill fields → serialize the hashable fields → SHA-256 →
ECDSA-sign the hash → link into the chain → return to the caller, who then
persists the chain to disk. This sequence is identical for genesis, borrow,
and return blocks — only the field values differ.

## 7. Block structure

```c
typedef struct Block {
    int index;
    time_t timestamp;
    char book_id[20];
    char book_title[80];
    char member_id[20];
    char member_name[50];
    char action[10];             /* BORROWED, RETURNED, (OVERDUE — unused, see §14) */
    char previous_hash[65];
    unsigned char signature[72]; /* ECDSA DER signature */
    char hash[65];

    size_t sig_len;   /* in-memory only — see §9 */
    struct Block *next;
} Block;
```

This matches the assignment's field list exactly (aside from the
`member_name` casing correction in §4), plus one in-memory-only addition
explained in §9.

## 8. Registry system

`books.txt`/`members.txt` are loaded once at startup into fixed-size arrays
(`MAX_BOOKS`/`MAX_MEMBERS` = 256, generous for a lab dataset). Loading is
defensive: each line is split on `,`, checked for exactly 3 fields and for
each field fitting its target buffer, and a bad line is skipped with a
warning rather than aborting the whole load or overflowing a buffer. The
registry is read-only for the program's lifetime — borrow/return only ever
query it (`registry_find_book`/`registry_find_member`), never mutate it.

## 9. SHA-256 integrity mechanism

Hashing covers every block field **except** `signature` and `hash` itself:
`index, timestamp, book_id, book_title, member_id, member_name, action,
previous_hash`, serialized into one delimited buffer and passed through
OpenSSL's `EVP_Digest` (SHA-256), producing a 64-character lowercase hex
string stored in `hash`.

**Why changing block data invalidates the hash:** `hash` is a pure function
of the other fields. If any hashed field changes after the block was written,
recomputing SHA-256 over the (now different) serialized buffer produces a
different digest than what's stored — `blockchain_validate` catches this by
recomputing and comparing on every load.

**Why changing an earlier block breaks later blocks too:** each block's
`previous_hash` was fixed at append time to the *pre-tamper* hash of its
predecessor. Changing block *N* changes what `hash` block *N* recomputes to —
but block *N+1*'s `previous_hash` still holds the old value, so the linkage
check (`previous_hash` must equal the prior block's recomputed `hash`) fails
at block *N+1* even if block *N+1* itself was never touched. Tampering
propagates forward through the chain by construction.

## 10. ECDSA authentication

A single ECDSA P-256 keypair, generated once via `EVP_EC_gen("P-256")` and
persisted as PEM files (`keys/private.pem`, `keys/public.pem`). Each block's
`hash` (not the raw fields, and not a re-hash of the whole block) is what
gets signed, via `EVP_DigestSign`; verification uses `EVP_DigestVerify`
against the public key and never treats an error as "valid" (fail-closed).

This is a second, independent check on top of hashing: even in the
(cryptographically infeasible) case of two different field sets producing the
same SHA-256 digest, the signature would only verify against the digest it
was actually produced for.

## 11. Key management

- Generated once at first startup if `keys/private.pem`/`keys/public.pem`
  don't both already exist; reused on every subsequent run.
- Private key file permissions restricted to the owner (`chmod 600`) at
  creation.
- Never embedded in source, never logged, never printed by any CLI path.
- `keys/` is gitignored — key material is runtime-generated, not shipped.

## 12. Persistence mechanism

Plain-text, one block per line, `|`-delimited:

```
index|timestamp|book_id|book_title|member_id|member_name|action|previous_hash|signature_hex|hash
```

Chosen over a raw binary struct dump specifically to sidestep struct padding,
endianness, and fixed-width-type portability concerns entirely — the file is
portable and human-editable, which also happens to make the required
tamper-detection demo trivial to perform and explain (open the file, change a
visible character, save).

Loading is split into two independently-testable concerns: `persistence_load`
only checks the file's *shape* (right number of fields, each within its
length limit, numeric fields actually numeric) and rejects anything that
doesn't parse safely as `PERSIST_CORRUPT`; `blockchain_validate`, run
separately right after a successful load, is what actually re-derives and
checks every hash/link/signature. This keeps "can I safely read this file at
all" separate from "is what's in this file trustworthy."

## 13. Chain validation

`blockchain_validate` walks the chain once, in order, checking per block:

1. Genesis correctness (`index == 0`, `previous_hash` = 64 zeros) — implicitly, as block 0's expected previous hash.
2. `index` matches the expected sequential position.
3. `previous_hash` matches the prior block's (recomputed) `hash`.
4. Recomputed SHA-256 matches the stored `hash`.
5. ECDSA signature verifies against the stored `hash`.

It stops and reports the *first* problem found (`ChainValidation{valid,
bad_index, reason}`), rather than continuing to scan — a compromised chain
is already unreliable past that point.

## 14. Tamper detection

Startup loads the chain and immediately runs `blockchain_validate`. A
validation failure at boot is a **warning, not a fatal error** — the program
still starts, so `validate chain`/`view records` can demonstrate the failure
interactively (this is what the assignment's demo explicitly requires: run
the program, then show validation reporting the compromise). What *is*
enforced is that `borrow`/`return` re-check chain integrity immediately
before writing and refuse if the chain is already compromised — otherwise a
new, perfectly valid-looking block appended on top of tampered history would
make the tamper harder to notice, not easier.

Six tamper variants are exercised in `tests/run_tests.sh`: a hashed field
changed, the stored hash changed directly, `previous_hash` changed, a genuine
single external byte flipped via script, a corrupted signature with the hash
left untouched, and a swapped-in wrong public key. All six are correctly
detected and reported with the specific block index and reason.

## 15. Error handling strategy

- **Fatal at startup** (exit 1, program does not proceed): missing/empty
  registry files, a chain file that doesn't even parse (shape-level
  corruption), inability to create/load the ECDSA keypair.
- **Non-fatal but flagged**: a chain that parses but fails cryptographic
  validation — reported loudly, writes disabled, reads still allowed.
- **Per-command, recoverable**: invalid book/member IDs, double
  borrow/return, unrecognized commands — a clear message, loop continues.
- Every fixed-size buffer copy uses `snprintf`/bounded `%Ns` scanning; every
  integer parsed from external input (`persistence.c`) uses `strtol` with
  full error checking, not `atoi`.

## 16. Testing

44 automated assertions in `tests/run_tests.sh` (`make test`), each in an
isolated sandbox, covering registries, borrow, return, blockchain structure,
cryptography (including two deliberately adversarial cases — a corrupted
signature and a wrong public key — not just "does the happy path work"),
six tamper variants, and one full AddressSanitizer/UndefinedBehaviorSanitizer
session across every command. See `docs/REQUIREMENTS.md` for the exact
requirement-to-test mapping. One real defect (an unbounded `strcpy` in the
chain validation loop) was caught by an R11.2 inspection pass during this
process and fixed before the checklist was marked complete.

## 17. Terminal output (see note)

The assignment asks for application screenshots. What follows are real
terminal transcripts captured from actual runs of the compiled binary during
development — not rendered image screenshots, and re-formatted with `$
command` lines for readability (the actual runs pipe commands into stdin
non-interactively, so the raw output interleaves differently); the message
text and ordering below is copied verbatim from the real runs, unedited.
Image screenshots are included in the technical report submitted as
PDF/Word, and the demo video is submitted separately.

```
$ ./lending_tracker
No existing chain found at 'data/chain.txt' — creating genesis block.
Loaded chain: 1 block(s), integrity verified.
Library Lending Chain — type 'help' for commands.
> borrow BK001 ALU001
OK: BK001 borrowed by ALU001 (block 1 recorded).
> borrow BK001 ALU002
ERROR: this book is already on loan
> return BK001
OK: BK001 returned (block 2 recorded).
> validate chain
Chain status: 3 block(s), VALID
```

```
$ # after hand-editing one character in data/chain.txt's block-1 title field
$ ./lending_tracker
WARNING: chain integrity check FAILED at block 1: stored hash does not match recomputed hash (block data was modified)
The chain file may have been tampered with. Borrow/return are disabled until this is resolved;
use 'view records' and 'validate chain' to inspect the damage.
Library Lending Chain — type 'help' for commands.
> validate chain
Chain status: 3 block(s), COMPROMISED
  first problem at block index 1: stored hash does not match recomputed hash (block data was modified)
> borrow BK002 ALU002
ERROR: chain integrity check failed at block 1 (stored hash does not match recomputed hash (block data was modified)) — refusing to add new records until this is resolved.
```

## 18. Challenges encountered and solutions

- **The `unsigned char[72]` signature field has no length field.** ECDSA P-256
  DER signatures are variable length (up to ~72 bytes), but nothing in the
  spec's struct tracks the actual length. Solution: added an in-memory-only
  `sig_len` field on `Block`, explicitly documented as not persisted and not
  hashed, so it doesn't change the spec-defined on-disk block shape — see
  `docs/ARCHITECTURE.md` and `REQUIREMENTS.md` ambiguity #4.
- **Demonstrating tamper detection without breaking the demo flow.** An
  early version treated any validation failure at startup as fatal, which
  would prevent the program from even reaching the CLI to show `validate
  chain` reporting the failure live. Solution: startup failures are now a
  warning, not an exit — the program starts, reads still work, and writes
  are the thing actually blocked (see §14).
- **`localtime_r`/`strtok`-family POSIX functions not visible under
  `-std=c11`.** Strict C11 hides POSIX extensions by default under glibc.
  Solution: added `-D_POSIX_C_SOURCE=200809L` to the Makefile's `CFLAGS`.
- **A `strcpy` slipped into the validation loop** despite the "no unsafe
  functions" rule, because both buffers happened to be safely bounded and it
  wasn't caught by compiler warnings. Found via a targeted `git grep` during
  the R11.2 check, replaced with `snprintf`. Documented in
  `docs/REQUIREMENTS.md` rather than silently fixed with no record.

## 19. Limitations

- `OVERDUE` is a structurally valid action value but no due-date concept or
  triggering logic exists anywhere in the spec or this implementation —
  intentionally left unimplemented (see §4/ambiguity #3), not a bug.
- The registry arrays are fixed-size (256 entries); fine for the assignment's
  scale, would need to become dynamic for a larger deployment.
- No multi-user/concurrent-access handling — the CLI assumes a single
  process operating on the chain file at a time.
- Private key has no passphrase (simpler for a lab demo; a production system
  would want one).
- `view records` checks each signature against the block's *stored* hash,
  so a block whose data was edited (hash untouched) still shows
  `signature=VALID` there; `validate chain` recomputes the hash and catches it.
- Deleting the newest block(s) from the end of `data/chain.txt` is not
  detected, because nothing outside the file records the latest hash or the
  chain length.
- A single installation-wide key authenticates blocks; there is no per-user
  authentication or access control.

## 20. Conclusion

The system meets every functional requirement in the assignment (registries,
genesis/borrow/return/validate/view, SHA-256 linking, ECDSA signing, tamper
detection, file persistence, CLI, defensive error handling), verified by 44
passing automated tests rather than asserted from a read-through. The one
spec inconsistency found (attendance-chain wording) was flagged and resolved
explicitly rather than copied or silently altered. The known limitations
in §19 are documented rather than hidden.
