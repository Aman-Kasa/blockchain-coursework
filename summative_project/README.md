# ALU Library Lending Chain – Summative Project

A blockchain-based library book lending tracker in C. It combines Formative 1 (registries, SHA-256
hash chain, ECDSA signatures, validation) and Formative 2 (token rewards, pending pool, proof-of-work
mining, UTXO and account ledgers, solo/pool/cloud mining), and adds the summative extensions:

- copy-level inventory with an availability state machine;
- reservation queues, due dates, renewals, overdue detection and fines;
- multiple lending records per block, with a Merkle tree built from scratch;
- a real ECDSA key pair for every member, and per-member nonces for replay protection;
- misuse and fraud detection with a manual review workflow;
- full disk persistence with re-validation on every start.

---

## 1. Dependencies

| Requirement | Notes |
|---|---|
| GCC (C11) and `make` | Clang also works: `make CC=clang` |
| OpenSSL 3 development headers (`libcrypto`) | SHA-256 and ECDSA P-256. Ubuntu/Debian: `sudo apt install build-essential libssl-dev`. macOS: `brew install openssl@3` |
| Python 3 *(tests only)* | Used by `tests/forge_chain.py`, the attacker simulation |

No other libraries are used. The Merkle tree is implemented from scratch.

## 2. Build and run

```bash
make                       # builds ./library_chain with -Wall -Wextra -Wpedantic (no warnings)
./library_chain            # asks which token ledger model to use
```

Options:

```bash
./library_chain --model utxo        # UTXO model
./library_chain --model account     # account-based model
./library_chain --difficulty 3      # proof-of-work difficulty 1-4 (default 2)
./library_chain --batch 6           # lending records per block 1-16 (default 4)
./library_chain --seed 7            # fixed seed: pool-mining hash rates repeat between runs
```

On macOS with Homebrew OpenSSL:
`make CFLAGS+="-I$(brew --prefix openssl@3)/include" LDLIBS="-L$(brew --prefix openssl@3)/lib -lcrypto"`

Other targets: `make test` runs the test suite, `make unit` builds the core unit tests,
`make asan` builds with AddressSanitizer and UndefinedBehaviorSanitizer, and `make clean` removes the
build output.

**Start over:** `rm -f data/*.dat`. Keep `keys/` to keep the same member identities. If you also
delete `keys/`, delete `data/*.dat` too, because the old signatures will no longer verify.

## 3. Data files

| File | Contents |
|---|---|
| `data/books.txt` | `book_id,title,author` (Formative 1 format) |
| `data/members.txt` | `member_id,full_name,course_code` (Formative 1 format) |
| `data/copies.txt` | `copy_id,book_id,condition` with condition `NEW` / `GOOD` / `WORN` |
| `keys/<id>.pem` | each member's ECDSA P-256 private key (owner-only permissions), created automatically; `keys/LIBRARIAN.pem` is the librarian node |
| `data/chain.dat` | the blockchain: `B\|...` block headers, each followed by its `R\|...` records |
| `data/state.dat` | settings, chain metadata, inventory, queues, nonces, fines, balances or UTXO set, and mining payouts |
| `data/pool.dat` | the pending pool: one signed request per line |

The `.dat` files are written after every command and on exit. On start the chain is re-validated
and replayed from the genesis block before any command is allowed.

## 4. Switching between the UTXO and account-based models

The model is fixed for the whole session. Choose it with `--model utxo` / `--model account`, or
answer `1` / `2` at the startup prompt. To switch, exit and restart with the other model. Balances
are rebuilt from the chain on every start, so both models work on the same chain.

| | UTXO | Account |
|---|---|---|
| Balances | `token_balance`, `utxo_view` | `token_balance`, `account_balance` |
| Transfer | `token_transfer <from> <to> <amount>` | `token_transfer ...` or `account_transfer <from> <to> <amount> <nonce>` |
| History | `transaction_history <member>` (from the chain) | `transaction_history <member>` (linked list in memory) |

## 5. Setting the mining difficulty

Difficulty is the number of leading `0` hex characters a block hash needs (1–4, default 2).
Expected work is 16^difficulty attempts: 16, 256, 4,096, 65,536.

- At startup: `./library_chain --difficulty 3`
- During a session: `difficulty_status set 3`; `difficulty_status` shows the current value.

Each block stores the difficulty it was mined at, so blocks mined at different difficulties all
validate. The setting is saved in `state.dat`.

## 6. Commands

Run `help` inside the program for the full list. Summary:

| Area | Commands |
|---|---|
| Membership | `register_member <id> "<full name>" <course>`, `view_member <id>`, `member_nonce <id>` |
| Books and copies | `register_book <id> "<title>" "<author>"`, `view_book <id>`, `add_copy <book_id> [NEW\|GOOD\|WORN]`, `view_copy <copy_id>`, `mark_lost <copy_id> <reason_code>` |
| Lending | `borrow_book <member> <book> [copy]`, `return_book <copy> [NEW\|GOOD\|WORN] [reason_code]`, `renew_book <copy>`, `reserve_book <member> <book>`, `reservation_status [book]` |
| Fines and overdue | `check_overdue`, `view_fines [member]`, `settle_fine <member> <amount>`, `advance_time <days>` |
| Tokens | `token_transfer`, `token_balance`, `utxo_view`, `account_balance`, `account_transfer`, `account_nonce`, `transaction_history <member>` |
| Chain and mining | `pool_view`, `mine_solo`, `mine_pool [2-8]`, `mine_cloud <1-5>`, `miner_earnings`, `chain_view [index]`, `merkle_view <index>`, `validate_chain`, `chain_save`, `chain_load`, `difficulty_status [set n]`, `batch_size [n]` |
| Fraud and audit | `fraud_review`, `approve_suspicious <id>`, `reject_suspicious <id>`, `replay_request <id>`, `lending_history`, `member_history <id>`, `book_history <id>` |

Every lending command and token transfer only creates a **signed request in the pending pool**.
Nothing changes in the inventory, nonces or balances until the request is **mined** into a block.

## 7. How to test the mining simulations

Run the automated suite:

```bash
make test
```

It runs 91 checks: core unit tests (ECDSA, Merkle, blocks), registries, lending, reservations,
renewals, fines, the state machine, pool priority, nonces, both ledgers, all three mining methods,
fraud detection, persistence, tamper detection, and an AddressSanitizer run.

To try it by hand, start fresh (`rm -f data/*.dat`), run `./library_chain --model utxo --seed 7`,
and enter:

```text
borrow_book ALU001 BK001
borrow_book ALU002 BK002
borrow_book ALU999 BK001          # rejected: member not in the registry
reserve_book ALU003 BK002         # BK002's only copy is requested, so ALU003 joins the queue
pool_view
mine_solo                         # one block, hash attempts shown, UTXO set after the block
return_book BK002-C1              # on time: 10-coin reward once mined
mine_pool 4                       # reward table with 2% pool fee; copy offered to ALU003
advance_time 20                   # BK001-C1 is now overdue: OVERDUE request created automatically
borrow_book ALU003 BK002          # ALU003 takes the copy held for them
pool_view                         # overdue-related requests are at the top
return_book BK001-C1              # late: fine 12 coins, reward 5
mine_cloud 2                      # multi-round earnings summary
view_fines ALU001
settle_fine ALU001 12
difficulty_status set 4
borrow_book ALU001 BK003
mine_solo                         # roughly 65,000 attempts at difficulty 4
```

Fraud review:

```text
borrow_book ALU001 BK001          # within 24h of returning it: rapid re-borrow -> SUSPICIOUS
fraud_review
approve_suspicious <request_id>   # or reject_suspicious <request_id>
```

Tamper detection:

```bash
python3 tests/forge_chain.py      # pushes a due date back 30 days, redoes Merkle roots and PoW
./library_chain --model utxo      # validation fails: the altered record's ECDSA signature is invalid
```

Restore with `cp` from a backup, or delete `data/*.dat` to start over.

## 8. Design choices (summary; details in the technical report)

- **Batching:** fixed-size: each block takes the top *N* requests of the priority-ordered pool (default 4).
- **One nonce per member** for every request (lending and token transfers). A request must carry
  exactly the next nonce, and the nonce advances only when the request is mined. The account
  model's nonce is this same counter.
- **Extra record actions:** `LOST`, `FINE_PAID` and `TRANSFER` alongside the five in the brief, so
  that librarian overrides, fine payments and token transfers are also signed and on the chain.
- **Block reward fields:** `token_reward` is the total of member rewards in the block.
  `transaction_id` is SHA-256 over the IDs of the block's reward transactions.
- **ECDSA:** OpenSSL 3's `EVP_PKEY` interface on curve P-256. The `EC_KEY` API named in the brief
  is deprecated in OpenSSL 3 and would break the clean `-Wall` build.
- **Simulated clock:** `advance_time` moves time forward so overdue cases can be demonstrated; the
  offset is saved with the state.

All numbers (loan period, fines, rewards, fees, limits) are in `src/config.h`.

## 9. Source layout

```
src/
  main.c        arguments, model choice, startup
  cli.c         command table and handlers
  config.h      every tunable number
  util.c        parsing, validation, simulated clock
  crypto.c      SHA-256, ECDSA keys, signing, verification (OpenSSL EVP)
  registry.c    books, members, copies; key pairs
  record.c      lending record: canonical content, signing, hashing, text form
  merkle.c      Merkle tree (from scratch)
  chain.c       blocks, proof of work, structural validation, chain.dat
  state.c       copy state machine, reservation queues, nonces, fines
  pool.c        pending pool, priority order, projection, pool.dat
  lending.c     building, validating, signing and submitting requests; overdue scan
  fraud.c       misuse heuristics
  mining.c      block construction, solo / pool / cloud mining
  ledger.c      model selection; utxo.c and account.c implement the two models
  persist.c     state.dat, startup re-validation, save/load
tests/
  run_tests.sh  91-check suite
  test_core.c   unit tests for ECDSA, Merkle tree and blocks
  forge_chain.py attacker simulation
```
