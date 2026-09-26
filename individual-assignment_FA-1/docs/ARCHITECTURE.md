# Architecture

## Project structure

```
individual-assignment_FA-1/
├── src/
│   ├── main.c          Entry point: start-up sequence, then hands off to cli_run()
│   ├── registry.c/.h    Book/Member struct defs, file loading, ID lookup
│   ├── blockchain.c/.h  Block/chain structs, genesis, append, borrow/return logic, validation
│   ├── crypto.c/.h      SHA-256 hashing, ECDSA keygen/sign/verify (OpenSSL EVP)
│   ├── persistence.c/.h Save/load chain to/from disk, corruption detection
│   └── cli.c/.h         Command parsing, dispatch, user-facing messages
├── data/
│   ├── books.txt
│   └── members.txt
├── keys/                ECDSA keypair (PEM), generated once, gitignored
├── tests/               run_tests.sh — automated test suite
├── docs/                REQUIREMENTS.md, ARCHITECTURE.md, report source
├── Makefile
├── README.md
└── .gitignore
```

**Why each file exists:**
- `registry.c/h` — isolates "what is a valid book/member" from everything else. Borrow/Return only ever ask this module "does this ID exist, and what's its canonical data" — they never touch the raw files.
- `crypto.c/h` — isolates OpenSSL usage to one place. If the crypto approach changes (e.g. key format), nothing outside this file needs to change.
- `blockchain.c/h` — owns the chain's shape and rules: what a block is, how it links to the previous one, what makes the chain valid. Calls into `crypto.c` for hashing/signing but doesn't know how those are implemented.
- `persistence.c/h` — owns the on-disk format and the load/save/corruption-detection logic, separate from the in-memory chain logic, so tamper detection is a persistence-layer concern the way the assignment frames it (save → external modify → reload → validate → fail).
- `cli.c/h` — owns command parsing and user-facing text, kept separate from the underlying logic so the same blockchain/registry code could in principle be driven by a different interface without change.
- `main.c` — wires the above together: load registries → load or create chain → validate → enter command loop.

## Data structures

- `Book`, `Member` — as specified by the assignment, fixed-size arrays loaded once at startup, not resized at runtime (registries are read-only inputs, not something the program mutates).
- `Block` — as specified by the assignment (see REQUIREMENTS.md R2.1), plus one in-memory-only field: `size_t sig_len`, needed because ECDSA signatures are variable-length (see REQUIREMENTS.md ambiguity #4) but the spec's block struct has no length field. This field is **not** persisted or hashed — on load it is set to the number of bytes decoded from the hex `signature_hex` field (`crypto_hex_to_bytes()`).
- `Blockchain` — singly linked list of `Block`, per the design guidance to "design the blockchain as a linked list", plus a `size_t length` for O(1) length checks and a tail pointer for O(1) append.

## Data flow

```
books.txt  ──► load ──► Book[]  ──┐
                                    ├──► validate IDs ──► Borrow/Return ──► new Block
members.txt ─► load ──► Member[] ─┘                                          │
                                                                              ▼
                                                                    SHA-256 hash + ECDSA sign
                                                                              │
                                                                              ▼
                                                                    append to Blockchain (linked list)
                                                                              │
                                                                              ▼
                                                                    persistence.c writes chain file
```

On startup: registries load first (borrow/return cannot run without them) → chain file loads if present, else a genesis block is created → the loaded chain is run through Validate Chain immediately, before any command is accepted, so a tampered on-disk file is caught at boot, not just on demand.

## Cryptographic flow

- **Hashed**: all block fields except `signature` and `hash` itself — `index, timestamp, book_id, book_title, member_id, member_name, action, previous_hash`, serialized into one buffer in a fixed field order, then SHA-256'd. This is what `hash` stores.
- **Signed**: the same hashed buffer (i.e., the block's `hash`) is what gets ECDSA-signed — the signature authenticates "this exact set of block fields, in this exact chain position, was produced by the holder of the private key," not just "this data exists."
- **Keys**: one ECDSA (P-256) keypair, generated once via OpenSSL and stored as PEM files under `keys/` (private key gitignored, never embedded in source, never printed by the CLI except an explicit debug-only path if one is added later — none is planned).
- **Why tampering breaks it**: changing any hashed field in a stored block changes what SHA-256 produces for that block, so the stored `hash` no longer matches recomputation (R5.2) — that alone flags tampering. Changing an *earlier* block also breaks every later block's `previous_hash` linkage (R5.3), because each block's `previous_hash` was fixed at append time to the pre-tamper hash of its predecessor. The signature adds a second, independent check (R5.4): even a change that somehow reproduced a matching hash would still fail ECDSA verification against the original signature.

## Persistence strategy

Plain-text, line-oriented, delimited serialization (not raw struct dumps) — one line per block, fields separated by a delimiter unlikely to appear in the data (`|`), signature stored as hex. Chosen over binary struct writes because:
- avoids struct padding / endianness / fixed-width-type portability issues entirely (an explicit concern in the design guidance) — the file is readable and portable across machines/compilers without matching struct layout,
- makes the required tamper-detection demo trivial and legible: a grader can open the file in a text editor, change one visible character, save, and reload — no hex editor needed,
- keeps `hash`/`previous_hash`/`signature` as plain hex text, matching how they're already handled as C strings in memory.

Format (one block per line):
```
index|timestamp|book_id|book_title|member_id|member_name|action|previous_hash|signature_hex|hash
```

On load, each line is split, validated for field count and length limits (R1.8-style defensive parsing extended to the chain file too), and one `Block` is reconstructed per line. A malformed line makes the load fail as `PERSIST_CORRUPT` (fatal at start-up). The loader does not check hashes or signatures: `main()` runs `blockchain_validate()` straight after a successful load, and a recomputed hash that doesn't match is what R5.6/R7.2 report as chain compromise.

## Module responsibilities summary

| Module | Owns | Does not own |
|---|---|---|
| registry | Book/Member structs, file loading, ID → record lookup | block/chain logic |
| crypto | SHA-256, ECDSA keygen/sign/verify | what gets hashed/signed (caller decides) |
| blockchain | Block/chain structs, genesis, append, borrow/return business rules, validation | file I/O, CLI text |
| persistence | on-disk format, save, load, corruption detection | chain business rules |
| cli | command parsing, dispatch, user messages | crypto, file format |
| main | startup sequence, wiring | everything above |
