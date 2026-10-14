#!/usr/bin/env bash
# Test suite for the ALU Library Lending Chain (summative).
#
# Every test runs the real compiled program in its own scratch directory
# (its own data/, keys/ and .dat files), so tests never touch the project's
# data and never depend on each other. Covers every area of the brief:
# registries, lending, the copy state machine, reservations, renewals, fines,
# the pending pool and its priority order, nonces, ECDSA, Merkle trees, all
# three mining methods, both ledger models, fraud detection, persistence and
# tamper detection, plus core unit tests and an AddressSanitizer run.

set -uo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BIN="$ROOT/library_chain"
PASS=0
FAIL=0

check() {   # name, then a command that must succeed
    local name="$1"; shift
    if "$@"; then PASS=$((PASS + 1)); else FAIL=$((FAIL + 1)); echo "FAIL: $name"; fi
}
has()     { [[ "$1" == *"$2"* ]]; }
hasnt()   { [[ "$1" != *"$2"* ]]; }
matches() { grep -Eq -- "$2" <<< "$1"; }
count()   { grep -c -- "$2" <<< "$1"; }

box() {
    local d
    d=$(mktemp -d)
    mkdir -p "$d/data" "$d/keys"
    printf 'BK001,Things Fall Apart,Chinua Achebe\nBK002,Americanah,Chimamanda Ngozi Adichie\nBK003,The River Between,Ngugi wa Thiongo\n' > "$d/data/books.txt"
    printf 'ALU001,John Doe,BLK101\nALU002,Jane Smith,BLK101\nALU003,Amara Diallo,BLK101\nALU004,Kwame Mensah,BLK101\n' > "$d/data/members.txt"
    printf 'BK001-C1,BK001,NEW\nBK001-C2,BK001,GOOD\nBK002-C1,BK002,GOOD\nBK003-C1,BK003,NEW\nBK003-C2,BK003,GOOD\nBK003-C3,BK003,GOOD\nBK003-C4,BK003,GOOD\n' > "$d/data/copies.txt"
    echo "$d"
}

run() {     # dir model "cmd1" "cmd2" ... [extra args after --]
    local d="$1" model="$2"; shift 2
    local cmds=() extra=()
    while [[ $# -gt 0 && "$1" != "--" ]]; do cmds+=("$1"); shift; done
    [[ "${1:-}" == "--" ]] && { shift; extra=("$@"); }
    ( cd "$d" && printf '%s\n' "${cmds[@]}" exit | "$BIN" --model "$model" --seed 7 "${extra[@]}" ) 2>&1
}

pending_id() {   # dir action  -> request id of the first pool entry with that action
    grep "|$2|" "$1/data/pool.dat" 2>/dev/null | head -1 | cut -d'|' -f7
}

echo "== Build =="
make -C "$ROOT" -s clean >/dev/null
WARN=$(make -C "$ROOT" 2>&1 | grep -ciE 'warning|error')
check "compiles cleanly with -Wall -Wextra -Wpedantic" [ "$WARN" -eq 0 ]

echo "== Core unit tests (ECDSA, Merkle, blocks) =="
make -C "$ROOT" -s unit >/dev/null 2>&1
D=$(box)
OUT=$(cd "$D" && "$ROOT/build/test_core" 2>&1)
check "unit tests pass ($(grep -o '[0-9]* passed' <<< "$OUT"))" has "$OUT" ", 0 failed"

echo "== Registries =="
D=$(box); rm "$D/data/copies.txt"
OUT=$(run "$D" utxo); check "missing copies.txt is reported" has "$OUT" "is missing"
D=$(box); : > "$D/data/members.txt"
OUT=$(run "$D" utxo); check "empty members.txt is reported" has "$OUT" "is empty"
D=$(box)
OUT=$(run "$D" utxo 'register_member ALU005 "Zola Nkosi" BLK101' 'register_member ALU001 x y' 'register_member "bad id!" x y' \
      'register_book BK004 "Purple Hibiscus" "Chimamanda Ngozi Adichie"' 'add_copy BK004' 'add_copy BK999' 'view_book BK004')
check "register_member generates a key pair" [ -f "$D/keys/ALU005.pem" ]
check "member public key shown (compressed P-256)" matches "$OUT" "Public key \(on-chain identity\): 0[23][0-9a-f]{64}"
check "private key file is owner-only" [ "$(stat -c %a "$D/keys/ALU005.pem")" = "600" ]
check "duplicate member rejected" has "$OUT" "already exists"
check "malformed member ID rejected" has "$OUT" "invalid member ID"
check "copy ID generated and saved" grep -q "^BK004-C1,BK004,NEW" "$D/data/copies.txt"
check "copy of unknown book rejected" has "$OUT" "Book or Member not found (book BK999)"

echo "== Lending and the pending pool =="
D=$(box)
OUT=$(run "$D" utxo 'borrow_book ALU001 BK002' 'borrow_book ALU999 BK001' 'borrow_book ALU001 BK999' 'view_copy BK002-C1' 'borrow_book ALU002 BK002')
check "borrow enters the pool as PENDING" has "$OUT" "PENDING: BORROWED request"
check "invalid member ID rejected" [ "$(count "$OUT" 'ERROR: Book or Member not found')" -ge 2 ]
check "copy state unchanged until mined" has "$OUT" "BK002-C1     AVAILABLE"
check "only copy already requested: no copy available" has "$OUT" "No copy of BK002 is available"
check "chain still has only the genesis block" [ "$(grep -c '^B|' "$D/data/chain.dat")" -eq 1 ]
OUT=$(run "$D" utxo 'mine_solo' 'view_copy BK002-C1')
check "after mining the copy is BORROWED" has "$OUT" "BK002-C1     BORROWED"
check "solo mining shows hash attempts" matches "$OUT" "Valid hash after [0-9]+ hash attempts"

echo "== Reservations =="
OUT=$(run "$D" utxo 'reserve_book ALU001 BK001' 'reserve_book ALU003 BK002' 'reserve_book ALU004 BK002' 'reserve_book ALU001 BK002' 'mine_solo' 'reservation_status BK002')
check "reserving a title with available copies is rejected" has "$OUT" "has an available copy"
check "reservation queue is FIFO" matches "$OUT" "1\. ALU003 since"
check "second member queued behind the first" matches "$OUT" "2\. ALU004 since"
check "the current borrower cannot reserve the same title" has "$OUT" "ALU001 already holds or has been offered a copy of BK002"
OUT=$(run "$D" utxo 'return_book BK002-C1' 'mine_solo' 'view_copy BK002-C1' 'borrow_book ALU004 BK002' 'borrow_book ALU003 BK002' 'mine_solo')
check "returned copy offered to the head of the queue" has "$OUT" "now RESERVED for ALU003"
check "RESERVED copy shows who it is held for" has "$OUT" "held for ALU003"
check "another member cannot take a held copy" has "$OUT" "No copy of BK002 is available"
check "queue head borrows the held copy" matches "$OUT" "\+ BORROWED +[0-9a-f]+ +ALU003 / BK002-C1"

echo "== Renewals =="
D=$(box)
run "$D" utxo 'borrow_book ALU001 BK001' 'mine_solo' >/dev/null
OUT=$(run "$D" utxo 'renew_book BK001-C1' 'renew_book BK001-C1' 'renew_book BK001-C1')
check "two renewals accepted" [ "$(count "$OUT" 'PENDING: RENEWED')" -eq 2 ]
check "third renewal rejected (maximum 2)" has "$OUT" "already been renewed 2 times"
D=$(box)
run "$D" utxo 'borrow_book ALU001 BK002' 'mine_solo' 'reserve_book ALU002 BK002' 'mine_solo' >/dev/null
OUT=$(run "$D" utxo 'renew_book BK002-C1')
check "renewal blocked by another member's reservation" has "$OUT" "has reserved BK002, so it cannot be renewed"

echo "== Overdue detection and fines =="
D=$(box)
run "$D" utxo 'borrow_book ALU001 BK001' 'borrow_book ALU002 BK002' 'mine_solo' >/dev/null
OUT=$(run "$D" utxo 'advance_time 20' 'pool_view')
check "overdue copies flagged automatically on the next command" [ "$(count "$OUT" 'PENDING: OVERDUE')" -eq 2 ]
check "OVERDUE records are signed by the librarian" has "$OUT" "signed by LIBRARIAN"
check "overdue requests get HIGH priority" matches "$OUT" "OVERDUE .*HIGH"
check "fine accrues per day (6 days x 2)" has "$OUT" "fine so far 12 coins"
OUT=$(run "$D" utxo 'check_overdue' 'renew_book BK001-C1')
check "OVERDUE is raised only once per loan" hasnt "$OUT" "PENDING: OVERDUE"
check "an overdue copy cannot be renewed" has "$OUT" "already overdue"
OUT=$(run "$D" utxo 'return_book BK001-C1' 'mine_solo' 'view_fines ALU001' 'settle_fine ALU001 500' 'settle_fine ALU001 5' 'mine_solo' 'view_fines ALU001')
check "late return: fine computed and reward 5" has "$OUT" "6 day(s) late, fine 12 coins; reward will be 5"
check "late return rewards 5 coins (minus fee)" has "$OUT" "credited 5 - fee 1 = 4 coins"
check "overpaying a fine is rejected" has "$OUT" "owes 12; cannot pay 500"
check "fine settlement recorded" has "$OUT" "owed 12, paid 5, outstanding 7"

echo "== Copy state machine =="
D=$(box)
OUT=$(run "$D" utxo 'mark_lost BK001-C1' 'return_book BK001-C2' 'mark_lost BK001-C1 DAMAGED' 'mine_solo' 'view_copy BK001-C1' 'borrow_book ALU001 BK001 BK001-C1')
check "LOST requires a reason code" has "$OUT" "a reason code is required"
check "returning an AVAILABLE copy is rejected" has "$OUT" "has no open BORROWED record"
check "AVAILABLE -> LOST with a reason" has "$OUT" "BK001-C1     LOST"
check "a LOST copy cannot be borrowed" has "$OUT" "is LOST, not AVAILABLE"

echo "== Pool priority and multi-record blocks =="
D=$(box)
run "$D" utxo 'borrow_book ALU001 BK001' 'mine_solo' >/dev/null
OUT=$(run "$D" utxo 'borrow_book ALU002 BK002' 'borrow_book ALU003 BK003' 'advance_time 15' 'borrow_book ALU004 BK003' 'pool_view' -- --batch 2)
FIRST=$(grep -E '^  1 ' <<< "$OUT")
check "overdue-related request sorted to the top" has "$FIRST" "OVERDUE"
OUT=$(run "$D" utxo 'mine_solo' -- --batch 2)
check "block holds at most batch-size records" has "$OUT" "2 record(s) (top of the pool, batch size 2)"
check "the overdue request was mined first" matches "$OUT" "\+ OVERDUE"
L=$(grep '^B|' "$D/data/chain.dat" | cut -d'|' -f3 | paste -sd' ')
check "block timestamps strictly increase" python3 -c "t=[int(x) for x in '$L'.split()]; assert all(a<b for a,b in zip(t,t[1:]))"

echo "== Nonces and replay protection =="
D=$(box)
OUT=$(run "$D" account 'borrow_book ALU001 BK001' 'member_nonce ALU001')
check "nonce does not advance until confirmation" has "$OUT" "confirmed nonce 0 (requests mined so far); 1 pending; next request must carry nonce 1"
run "$D" account 'mine_solo' 'return_book BK001-C1' 'mine_solo' >/dev/null
OUT=$(run "$D" account 'account_transfer ALU001 ALU002 1 0' 'account_transfer ALU001 ALU002 1 9' 'account_transfer ALU001 ALU002 1 2')
check "reused nonce rejected" has "$OUT" "nonce 0 for ALU001 is not the expected next nonce 2"
check "skipped nonce rejected" has "$OUT" "nonce 9 for ALU001 is not the expected next nonce 2"
check "correct nonce accepted" has "$OUT" "PENDING: TRANSFER request"

echo "== Ledgers =="
D=$(box)
OUT=$(run "$D" utxo 'borrow_book ALU001 BK001' 'mine_solo' 'return_book BK001-C1' 'mine_solo' 'token_transfer ALU001 ALU002 5' 'token_transfer ALU001 ALU002 5' 'mine_solo' 'utxo_view')
check "UTXO set printed after every block" [ "$(count "$OUT" 'UTXO set (unspent outputs)')" -ge 3 ]
check "on-time reward: 10 - fee = 9 coin output" matches "$OUT" "#1 +ALU001 +9 "
check "over-committing pending transfers rejected (double spend)" has "$OUT" "6 already committed to pending transfers"
check "transfer produces a change output" has "$OUT" "3 change back to ALU001"
check "spent output listed as spent" matches "$OUT" "#1 +ALU001 +9 +spent by tx"
D=$(box)
OUT=$(run "$D" account 'borrow_book ALU001 BK001' 'mine_solo' 'return_book BK001-C1' 'mine_solo' 'account_transfer ALU001 ALU002 20 2' 'account_transfer ALU001 ALU002 3 2' 'mine_solo' 'transaction_history ALU001' 'account_balance')
check "account: insufficient balance rejected" has "$OUT" "insufficient funds"
check "account: history lists sender, recipient, amount, fee, nonce" matches "$OUT" "TRANSFER .*ALU001 +ALU002 +3 +1 +2"
check "account: balances debited and credited" matches "$OUT" "ALU001 +5 "
check "account: wrong-model command explained" has "$(run "$D" account 'utxo_view')" "belongs to the UTXO model"

echo "== Mining methods =="
D=$(box)
OUT=$(run "$D" utxo 'borrow_book ALU001 BK001' 'mine_pool 4' -- --difficulty 3)
check "pool table printed" has "$OUT" "| Miner ID | Hash rate | Attempts | Share % | Reward     |"
check "2% pool fee deducted" has "$OUT" "pool fee 2% = 1.00; distributed 49.00"
check "block hash meets difficulty 3" [ "$(grep '^B|1|' "$D/data/chain.dat" | cut -d'|' -f7 | cut -c1-3)" = "000" ]
OUT=$(run "$D" utxo 'mine_pool 1' 'mine_cloud 0' 'mine_cloud 6' 'difficulty_status set 5')
check "pool size / rental / difficulty ranges enforced" [ "$(count "$OUT" 'must be')" -ge 4 ]
# Difficulty 3 was saved by the earlier --difficulty 3 run; set it back to 2.
OUT=$(run "$D" utxo 'difficulty_status set 2' 'borrow_book ALU002 BK002' 'mine_cloud 2')
check "cloud mining summary printed" has "$OUT" "Gross earnings"
check "cloud mining profitable at difficulty 2" matches "$OUT" "Net profit +: [0-9]"
OUT=$(run "$D" utxo 'mine_cloud 2')
check "cloud rental with nothing to mine warns as unprofitable" has "$OUT" "WARNING: rental is unprofitable"

echo "== Fraud detection =="
D=$(box)
run "$D" utxo 'borrow_book ALU001 BK003' 'borrow_book ALU001 BK003' 'borrow_book ALU001 BK003' 'mine_solo' >/dev/null
OUT=$(run "$D" utxo 'borrow_book ALU001 BK003' 'mine_solo' 'fraud_review')
check "excessive concurrent borrows flagged" has "$OUT" "excessive concurrent borrows: ALU001 already holds 3 copies"
check "suspicious request not mined" has "$OUT" "Nothing to mine"
SID=$(grep '|SUSPICIOUS|' "$D/data/pool.dat" | cut -d'|' -f7)
OUT=$(run "$D" utxo "approve_suspicious $SID" 'mine_solo')
check "approved request is mined" matches "$OUT" "\+ BORROWED +$SID"
D=$(box)
run "$D" utxo 'borrow_book ALU001 BK001' 'mine_solo' >/dev/null
OUT=$(run "$D" utxo 'return_book BK001-C1 WORN' 'return_book BK001-C1 WORN DAMAGED-SPINE')
check "condition NEW->WORN without reason flagged" has "$OUT" "condition manipulation: BK001-C1 downgraded NEW -> WORN"
D=$(box)
run "$D" utxo 'borrow_book ALU001 BK001' 'mine_solo' >/dev/null
OUT=$(run "$D" utxo 'return_book BK001-C1 WORN DAMAGED-SPINE' 'mine_solo' 'borrow_book ALU001 BK001')
check "downgrade with a librarian reason code accepted" hasnt "$OUT" "condition manipulation"
check "rapid re-borrow within 24h flagged" has "$OUT" "rapid re-borrow: ALU001 returned BK001"
RID=$(grep '^R|' "$D/data/chain.dat" | head -1 | cut -d'|' -f2)
OUT=$(run "$D" utxo "replay_request $RID" 'fraud_review')
check "replayed request flagged as duplicate" has "$OUT" "duplicate request: $RID is already confirmed in block 1"
OUT=$(run "$D" utxo "reject_suspicious $RID" 'fraud_review')
check "reject_suspicious discards a request" has "$OUT" "Rejected and discarded $RID"

echo "== Persistence =="
D=$(box)
run "$D" utxo 'borrow_book ALU001 BK001' 'mine_solo' 'reserve_book ALU002 BK002' 'borrow_book ALU003 BK002' 'advance_time 3' >/dev/null
OUT=$(run "$D" utxo 'pool_view')
check "chain re-validated on startup" has "$OUT" "re-validating the whole chain"
check "saved state matches the chain" has "$OUT" "match the chain"
check "pending pool restored" has "$OUT" "Pending pool restored: 1 request(s)"
check "simulated clock restored" has "$(run "$D" utxo 'advance_time 1')" "It is now $(date -d '+4 days' +%Y-%m-%d)"
check "state file has every section" bash -c "grep -q '^META|' $D/data/state.dat && grep -q '^COPY|' $D/data/state.dat && grep -q '^SIGNER|' $D/data/state.dat && grep -q '^UTXO\\|^MINER|' $D/data/state.dat"

echo "== Tamper detection =="
D=$(box)
run "$D" utxo 'borrow_book ALU001 BK001' 'borrow_book ALU002 BK002' 'mine_solo' 'return_book BK001-C1' 'mine_solo' >/dev/null
cp "$D/data/chain.dat" "$D/chain.bak"
OUT=$(run "$D" utxo 'validate_chain'); check "untouched chain is VALID" has "$OUT" "VALID"
python3 "$ROOT/tests/forge_chain.py" "$D/data/chain.dat" >/dev/null
OUT=$(run "$D" utxo 'validate_chain' 'borrow_book ALU003 BK003')
check "re-mined forgery caught by the ECDSA signature" has "$OUT" "ID or ECDSA signature is invalid"
check "read-only session refuses new requests" has "$OUT" "no new requests are accepted"
cp "$D/chain.bak" "$D/data/chain.dat"
sed -i '0,/^R|/s/|BORROWED|BK001|/|BORROWED|BK002|/' "$D/data/chain.dat"
OUT=$(run "$D" utxo 'validate_chain'); check "edited record caught" has "$OUT" "COMPROMISED"
cp "$D/chain.bak" "$D/data/chain.dat"
awk -F'|' 'BEGIN{OFS="|"} /^B\|1\|/{$12=$12+1}1' "$D/chain.bak" > "$D/data/chain.dat"
OUT=$(run "$D" utxo 'validate_chain'); check "edited block header caught by its hash" has "$OUT" "recomputed header hash"
cp "$D/chain.bak" "$D/data/chain.dat"
run "$D" utxo >/dev/null
sed -i 's/^SIGNER|ALU001|2|/SIGNER|ALU001|0|/' "$D/data/state.dat"
OUT=$(run "$D" utxo); check "edited state.dat detected and rebuilt from the chain" has "$OUT" "does not match the chain"
run "$D" utxo 'borrow_book ALU003 BK003' >/dev/null
sed -i 's/|BORROWED|BK003|/|BORROWED|BK002|/' "$D/data/pool.dat"
OUT=$(run "$D" utxo); check "edited pool.dat entry discarded (bad signature)" has "$OUT" "invalid ECDSA signature: discarded"

echo "== Memory safety (ASan/UBSan) =="
make -C "$ROOT" -s asan >/dev/null 2>&1
D=$(box)
OUT=$(run "$D" utxo 'borrow_book ALU001 BK001' 'borrow_book ALU002 BK002' 'reserve_book ALU003 BK002' 'mine_pool 3' \
      'return_book BK002-C1' 'advance_time 20' 'mine_cloud 2' 'borrow_book ALU003 BK002' 'mine_solo' 'token_transfer ALU002 ALU001 2' \
      'mine_solo' 'replay_request x' 'chain_view' 'merkle_view 1' 'validate_chain' 'chain_save' 'chain_load' 'lending_history')
check "ASan: no memory errors (UTXO)" hasnt "$OUT" "AddressSanitizer"
check "ASan: no leaks (UTXO)" hasnt "$OUT" "LeakSanitizer"
check "UBSan: no undefined behaviour" hasnt "$OUT" "runtime error"
OUT=$(run "$D" account 'account_balance' 'transaction_history ALU002' 'borrow_book ALU001 BK003' 'mine_solo')
check "ASan: account model clean" hasnt "$OUT" "Sanitizer"
make -C "$ROOT" -s clean all >/dev/null

echo
echo "================================================================"
echo "  $PASS passed, $FAIL failed"
echo "================================================================"
[ "$FAIL" -eq 0 ]
