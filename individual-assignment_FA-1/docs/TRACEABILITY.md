# Requirement Traceability Matrix & Final Quality Gate

This audit follows one rule: no
fabricated "100% done" claim, no numerical self-score — every rubric
criterion gets an explicit PASS / NEEDS FIX with a stated reason, and the
project is only called complete once every requirement in
`docs/REQUIREMENTS.md` is actually addressed (checked off there only when
backed by a test, inspection, or doc reference — see that file's
"Verification key").

## 1. Rubric criterion audit

The assignment rubric has four scored criteria. Each is assessed here
against what actually exists in the repository right now, not against intent.

### A. Compliance with Submission Requirements (4 pts)

| Item | Status | Evidence |
|---|---|---|
| Complete C source code | **PASS** | `src/` — 6 modules, builds clean under `-Wall -Wextra -Wpedantic` |
| GitHub repository + link | **PASS** | https://github.com/Aman-Kasa/blockchain-coursework (project in `individual-assignment_FA-1/`) |
| Demo video (3–5 min) | **PASS** | Recorded and submitted separately (script in §3 below) |
| Technical report | **PASS** | Submitted separately as PDF/Word; in-repo version `docs/REPORT.md` |
| README (compile/build/run/deps) | **PASS** | `README.md` |
| Screenshots | **PASS** | Included in the submitted technical report; terminal transcripts in `docs/REPORT.md` §17 |
| System design diagram | **PASS** | `docs/ARCHITECTURE.md` and `docs/REPORT.md` §5 both include the data-flow diagram |
| Challenges/solutions | **PASS** | `docs/REPORT.md` §18 |

**Overall: PASS.**

### B. Technical Correctness and Code Quality (4 pts)

| Item | Status | Evidence |
|---|---|---|
| Correct structures | **PASS** | Structs match spec exactly (`docs/REQUIREMENTS.md` R2.1) |
| Correct blockchain logic | **PASS** | 44/44 tests, including 2 order/genesis-specific tests |
| Correct registry logic | **PASS** | 11/11 registry assertions |
| Correct SHA-256 | **PASS** | OpenSSL `EVP_Digest`; tamper tests prove it's actually enforced, not just called |
| Correct persistence | **PASS** | Reload-across-process test; 6 tamper variants all detected |
| Correct validation | **PASS** | All 5 validation checks (genesis, hash, link, signature, order) individually test-covered |
| Correct error handling | **PASS** | Missing/empty/malformed registry, invalid IDs, double borrow/return, unrecognized commands all tested |
| Memory safety | **PASS**, scoped | Clean ASan/UBSan/LeakSanitizer across every CLI command and both success/error paths of borrow/return. Two unreached branches noted honestly in `docs/REQUIREMENTS.md` R11.3 (out-of-memory, and the seal-block-fails-so-free-and-abort path) — neither is exercised because neither is reachable without fault injection, not because they were skipped carelessly |
| Readable, modular C | **PASS** | 6 single-responsibility modules; no function does more than one thing (registry load / block seal / chain walk / CLI dispatch are each isolated) |

**Overall: PASS.**

### C. Security and Use of Cryptographic Keys (4 pts)

| Item | Status | Evidence |
|---|---|---|
| ECDSA implementation | **PASS** | P-256 via `EVP_EC_gen`/`EVP_DigestSign`/`EVP_DigestVerify` |
| Private/public key handling | **PASS** | Generated once, PEM-persisted, private key `chmod 600`, never embedded in source |
| Signature generation | **PASS** | Every appended block is signed; tested |
| Signature verification | **PASS** | Tested against both a corrupted signature and a wrong public key — two independent adversarial cases, not just the happy path |
| Integrity validation | **PASS** | SHA-256 recomputation on every load |
| Tamper detection | **PASS** | 6 tamper variants, all correctly detected and localized to the right block |
| Key protection | **PASS** | `keys/` gitignored, owner-only file permissions on the private key |
| No hard-coded private key | **PASS** | `git grep` for key material in `src/` — none; verified during testing |

**Overall: PASS.**

### D. Demo Video (3 pts)

Recorded and submitted separately, following the script in §3.

## 2. Overall status

**Complete.** Source code, README, technical report and demo video are all
delivered. The GitHub repository is https://github.com/Aman-Kasa/blockchain-coursework, with the
project in `individual-assignment_FA-1/`. Known limitations of the implementation are listed in
`README.md` ("Known limitations").

## 3. Demo script (3–5 minutes)

A ready-to-record script. Each line is one spoken beat + one terminal action.
Assumes a clean checkout (`make clean && rm -f data/chain.txt keys/*.pem`
right before recording, so the genesis-creation moment is visible).

| # | Say | Do |
|---|---|---|
| 1 | "This is a blockchain-backed library lending tracker in C. Let's build and start it." | `make`, then `./lending_tracker` |
| 2 | "On first run it loads the book and member registries, finds no existing chain, and creates a genesis block." | point at the `creating genesis block` / `integrity verified` lines |
| 3 | "Let's try an invalid book ID first — it should be rejected, not silently accepted." | `borrow ZZZZZ ALU001` |
| 4 | "Now a real borrow — book and member both validated against the registries." | `borrow BK001 ALU001` |
| 5 | "Trying to borrow the same book again is rejected — it's already on loan." | `borrow BK001 ALU002` |
| 6 | "Returning it, then viewing every record on the chain — each one shows its hash, its link to the previous block, and whether its signature verifies." | `return BK001`, `view records` |
| 7 | "Validate chain recomputes everything from scratch and confirms it's intact." | `validate chain` |
| 8 | "Now the actual security demonstration. I'll exit, and hand-edit one character in the persisted chain file — this is meant to simulate someone tampering with the record outside the program." | `exit`, open `data/chain.txt`, change one visible character in a title field, save |
| 9 | "Restarting the program: it immediately flags the chain as compromised on boot." | `./lending_tracker` — point at the `WARNING: chain integrity check FAILED` line |
| 10 | "Validate chain confirms it, and tells us exactly which block and why." | `validate chain` |
| 11 | "And critically, it won't let me add new records on top of a broken chain." | `borrow BK002 ALU002` — point at the refusal message |
| 12 | "That's the full loop: registry validation, cryptographic linking, and tamper detection that's actually enforced, not just reported." | `exit` |

## 4. Security audit summary

Covered in full in §1.C above. No open security findings. The one defect
found during this project (an unbounded `strcpy`, safe in practice but
non-compliant with the project's own "no unsafe functions" rule) was caught
by a targeted inspection pass during testing, fixed, and the fix is covered by
the same 44 tests that already existed — see `docs/REQUIREMENTS.md` R11.2 and
`docs/REPORT.md` §18.

## 5. Memory-safety audit summary

Covered in `docs/REQUIREMENTS.md` R11.1–R11.4. Clean under ASan/UBSan/
LeakSanitizer for every path the test suite exercises. Two allocation-failure
branches are not exercised (documented, not hidden) because triggering them
requires fault injection (simulating `calloc` failure or an OpenSSL signing
failure) that the test suite doesn't currently do.

## 6. Testing matrix summary

44/44 automated tests passing (`make test`). Full breakdown in
`tests/run_tests.sh`, organized into: Registry (11), Borrow (5), Return (4),
Blockchain (9), Cryptography (5), Tampering (7), Memory Safety (3, covering a
full multi-command session). See `docs/REQUIREMENTS.md` for the exact
requirement-to-test mapping.

## 7. Demo checklist

- [ ] Program startup
- [ ] Registry loading (visible in startup output)
- [ ] Valid ID (successful borrow)
- [ ] Invalid ID (rejected borrow)
- [ ] Borrow operation
- [ ] Return operation
- [ ] Viewing records
- [ ] Chain validation (on an intact chain)
- [ ] Persistence/reload (implicit — the chain shown in step 9 of §3 is the one from before the tamper, proving it persisted)
- [ ] Tamper demonstration (hand-edit the file)
- [ ] Chain validation failing after tampering

All of these are covered by the script in §3. Use this list to check the
recording before submitting.

## 8. Report checklist

All 20 sections from the engineering-workflow guide's report outline are present in
`docs/REPORT.md`: Introduction, Problem statement, System objectives, System
architecture, Blockchain implementation, Block structure, Registry system,
SHA-256 integrity mechanism, ECDSA authentication, Key management,
Persistence mechanism, Chain validation, Tamper detection, Error handling
strategy, Testing, Screenshots (terminal transcripts),
Challenges encountered, Solutions (merged with Challenges), Limitations,
Conclusion. **Complete.** The submitted PDF/Word report adds image
screenshots and design diagrams.

## 9. README checklist

Project title, description, features, architecture, requirements,
dependencies, installation, compilation, execution, CLI commands, test
procedure, tamper-detection procedure, cryptographic design, project
structure, troubleshooting — all present in `README.md`. **Complete.**

## 10. Remaining issues

No deliverable is outstanding. Known limitations of the implementation
(signature display in `view records`, undetected deletion of the newest
blocks, one installation-wide key, no `OVERDUE` rule) are documented in
`README.md` under "Known limitations".
