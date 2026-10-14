#include "cli.h"
#include "persist.h"
#include "lending.h"
#include "merkle.h"
#include "mining.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#define LINE_LEN   512
#define MAX_TOKENS 16

typedef void (*Handler)(App *app, int argc, char **argv);

typedef struct {
    const char *name;
    Handler fn;
    const char *usage;
    const char *help;
} Command;

static void usage_of(const char *name);

/* ===================================================================== */
/* Membership and book management                                         */
/* ===================================================================== */

static void cmd_register_member(App *app, int argc, char **argv) {
    if (argc != 4) { usage_of(argv[0]); return; }
    char err[256];
    if (!registry_add_member(&app->reg, argv[1], argv[2], argv[3], err, sizeof(err))) {
        printf("%s\n", err);
        return;
    }
    state_sync_registry(app->state, &app->reg);
    const Member *m = registry_member(&app->reg, argv[1]);
    printf("Registered member %s (%s, %s).\n", m->member_id, m->full_name, m->course_code);
    printf("  New ECDSA P-256 key pair generated: private key in %s/%s.pem\n", KEYS_DIR, m->member_id);
    printf("  Public key (on-chain identity): %s\n", m->pubkey_hex);
}

static void print_fines(const App *app, const char *member_id);

static void cmd_view_member(App *app, int argc, char **argv) {
    if (argc != 2) { usage_of(argv[0]); return; }
    const Member *m = registry_member(&app->reg, argv[1]);
    if (!m) { printf("ERROR: Book or Member not found (member %s).\n", argv[1]); return; }
    const SignerState *ss = state_signer_c(app->state, m->member_id);
    printf("Member %s\n  Name       : %s\n  Course     : %s\n  Public key : %s\n",
           m->member_id, m->full_name, m->course_code, m->pubkey_hex);
    printf("  Nonce      : %ld confirmed, next request must carry %ld\n", ss ? ss->nonce : 0,
           lending_expected_nonce(app, m->member_id));
    printf("  Holding    :");
    int any = 0;
    for (int i = 0; i < app->state->ncopies; i++) {
        const CopyState *c = &app->state->copies[i];
        if (strcmp(c->holder, m->member_id) != 0) continue;
        char d[16] = "";
        if (c->status == ST_BORROWED) format_date(c->due_date, d, sizeof(d));
        printf(" %s (%s%s%s)", c->copy_id, copy_status_name(c->status), d[0] ? ", due " : "", d);
        any = 1;
    }
    printf("%s\n", any ? "" : " nothing");
    print_fines(app, m->member_id);
}

static void cmd_register_book(App *app, int argc, char **argv) {
    if (argc != 4) { usage_of(argv[0]); return; }
    char err[256];
    if (!registry_add_book(&app->reg, argv[1], argv[2], argv[3], err, sizeof(err))) {
        printf("%s\n", err);
        return;
    }
    state_sync_registry(app->state, &app->reg);
    printf("Registered book %s: \"%s\" by %s. Add physical copies with add_copy %s.\n",
           argv[1], argv[2], argv[3], argv[1]);
}

static void print_copy_state(const CopyState *c) {
    char d[16] = "";
    if (c->status == ST_BORROWED) format_date(c->due_date, d, sizeof(d));
    printf("    %-12s %-9s condition %-4s", c->copy_id, copy_status_name(c->status), condition_name(c->condition));
    if (c->status == ST_BORROWED)
        printf("  held by %s, due %s, renewed %d/%d%s", c->holder, d, c->renewals, MAX_RENEWALS,
               c->overdue_flagged ? ", OVERDUE" : (days_late(c->due_date, sim_now()) > 0 ? ", past due" : ""));
    else if (c->status == ST_RESERVED)
        printf("  held for %s (reservation)", c->holder);
    printf("\n");
}

static void print_queue(const App *app, const char *book_id) {
    const BookQueue *q = state_queue_c(app->state, book_id);
    printf("  Reservation queue (%d):", q ? q->count : 0);
    if (!q || q->count == 0) { printf(" empty\n"); return; }
    printf("\n");
    for (int i = 0; i < q->count; i++) {
        char d[32];
        format_time(q->q[i].reservation_date, d, sizeof(d));
        printf("    %d. %s since %s\n", i + 1, q->q[i].member_id, d);
    }
}

static void cmd_view_book(App *app, int argc, char **argv) {
    if (argc != 2) { usage_of(argv[0]); return; }
    const Book *b = registry_book(&app->reg, argv[1]);
    if (!b) { printf("ERROR: Book or Member not found (book %s).\n", argv[1]); return; }
    printf("Book %s: \"%s\" by %s\n  Copies (%d available):\n", b->book_id, b->title, b->author,
           state_available_copies(app->state, b->book_id));
    int n = 0;
    for (int i = 0; i < app->state->ncopies; i++) {
        const CopyState *c = &app->state->copies[i];
        if (strcmp(c->book_id, b->book_id) != 0) continue;
        print_copy_state(c);
        n++;
    }
    if (!n) printf("    (no copies registered)\n");
    print_queue(app, b->book_id);
}

static void cmd_add_copy(App *app, int argc, char **argv) {
    if (argc < 2 || argc > 3) { usage_of(argv[0]); return; }
    Condition cond = COND_NEW;
    if (argc == 3 && !condition_parse(argv[2], &cond)) {
        printf("ERROR: condition must be NEW, GOOD or WORN.\n");
        return;
    }
    char id[COPY_ID_LEN], err[256];
    if (!registry_add_copy(&app->reg, argv[1], cond, id, err, sizeof(err))) {
        printf("%s\n", err);
        return;
    }
    state_sync_registry(app->state, &app->reg);
    printf("Added copy %s of %s (condition %s), state AVAILABLE.\n", id, argv[1], condition_name(cond));
}

static void cmd_view_copy(App *app, int argc, char **argv) {
    if (argc != 2) { usage_of(argv[0]); return; }
    const CopyState *c = state_copy_c(app->state, argv[1]);
    if (!c) { printf("ERROR: copy %s not found.\n", argv[1]); return; }
    const Book *b = registry_book(&app->reg, c->book_id);
    printf("Copy %s of %s \"%s\" (confirmed state)\n", c->copy_id, c->book_id, b ? b->title : "?");
    print_copy_state(c);
    for (size_t i = 0; i < app->pool.count; i++) {
        const PoolEntry *e = &app->pool.items[i];
        if (strcmp(e->rec.copy_id, c->copy_id) == 0)
            printf("  Pending: %s %s by %s (%s)\n", action_name(e->rec.action), e->rec.record_id,
                   e->rec.member_id, pool_status_name(e->status));
    }
}

/* ===================================================================== */
/* Lending, reservations, fines                                           */
/* ===================================================================== */

static void cmd_borrow_book(App *app, int argc, char **argv) {
    if (argc < 3 || argc > 4) { usage_of(argv[0]); return; }
    lending_borrow(app, argv[1], argv[2], argc == 4 ? argv[3] : NULL);
}

static void cmd_return_book(App *app, int argc, char **argv) {
    if (argc < 2 || argc > 4) { usage_of(argv[0]); return; }
    lending_return(app, argv[1], argc >= 3 ? argv[2] : NULL, argc == 4 ? argv[3] : NULL);
}

static void cmd_renew_book(App *app, int argc, char **argv) {
    if (argc != 2) { usage_of(argv[0]); return; }
    lending_renew(app, argv[1]);
}

static void cmd_reserve_book(App *app, int argc, char **argv) {
    if (argc != 3) { usage_of(argv[0]); return; }
    lending_reserve(app, argv[1], argv[2]);
}

static void cmd_reservation_status(App *app, int argc, char **argv) {
    if (argc > 2) { usage_of(argv[0]); return; }
    for (size_t i = 0; i < app->reg.book_count; i++) {
        const Book *b = &app->reg.books[i];
        if (argc == 2 && strcmp(b->book_id, argv[1]) != 0) continue;
        const BookQueue *q = state_queue_c(app->state, b->book_id);
        int held = 0;
        for (int k = 0; k < app->state->ncopies; k++)
            if (app->state->copies[k].status == ST_RESERVED && strcmp(app->state->copies[k].book_id, b->book_id) == 0) held++;
        if (argc == 1 && (!q || q->count == 0) && held == 0) continue;
        printf("%s \"%s\": %d available\n", b->book_id, b->title, state_available_copies(app->state, b->book_id));
        for (int k = 0; k < app->state->ncopies; k++) {
            const CopyState *c = &app->state->copies[k];
            if (c->status == ST_RESERVED && strcmp(c->book_id, b->book_id) == 0)
                printf("  Copy %s is offered to %s (they can borrow it now)\n", c->copy_id, c->holder);
        }
        print_queue(app, b->book_id);
    }
    int pending = 0;
    for (size_t i = 0; i < app->pool.count; i++) {
        const Record *r = &app->pool.items[i].rec;
        if (r->action != ACT_RESERVED || (argc == 2 && strcmp(r->book_id, argv[1]) != 0)) continue;
        if (!pending++) printf("Pending reservations (not yet mined):\n");
        printf("  %s wants %s (%s)\n", r->member_id, r->book_id, r->record_id);
    }
    if (argc == 1 && pending == 0) printf("(end of reservation report)\n");
}

static void cmd_check_overdue(App *app, int argc, char **argv) {
    (void)argc; (void)argv;
    int n = lending_scan_overdue(app, 0);
    int flagged = 0;
    time_t now = sim_now();
    for (int i = 0; i < app->state->ncopies; i++) {
        const CopyState *c = &app->state->copies[i];
        if (c->status != ST_BORROWED || days_late(c->due_date, now) < 1) continue;
        if (!flagged++) printf("Copies past their due date:\n");
        long late = days_late(c->due_date, now);
        printf("  %-12s held by %-8s %ld day(s) late, fine accrued %ld coins%s\n", c->copy_id, c->holder,
               late, late * FINE_PER_DAY, c->overdue_flagged ? " (OVERDUE on chain)" : " (OVERDUE request pending)");
    }
    if (!flagged) printf("No borrowed copy is past its due date.\n");
    else if (n == 0) printf("(no new OVERDUE requests were needed)\n");
}

/* Fines owed from confirmed late returns, minus payments, plus what is still
 * accruing on copies that are overdue and not yet returned. */
static void print_fines(const App *app, const char *member_id) {
    const SignerState *s = state_signer_c(app->state, member_id);
    if (!s) return;
    long accruing = 0;
    time_t now = sim_now();
    for (int i = 0; i < app->state->ncopies; i++) {
        const CopyState *c = &app->state->copies[i];
        if (c->status == ST_BORROWED && strcmp(c->holder, member_id) == 0 && days_late(c->due_date, now) > 0)
            accruing += days_late(c->due_date, now) * FINE_PER_DAY;
    }
    printf("  Fines      : owed %ld, paid %ld, outstanding %ld", s->fines_owed, s->fines_paid,
           s->fines_owed - s->fines_paid);
    if (accruing) printf(" (+%ld accruing on overdue copies, payable once returned)", accruing);
    printf("\n");
}

static void cmd_view_fines(App *app, int argc, char **argv) {
    if (argc > 2) { usage_of(argv[0]); return; }
    printf("Fine rate: %d coin(s) per day late.\n", FINE_PER_DAY);
    for (size_t i = 0; i < app->reg.member_count; i++) {
        const Member *m = &app->reg.members[i];
        if (argc == 2 && strcmp(m->member_id, argv[1]) != 0) continue;
        printf("%s (%s)\n", m->member_id, m->full_name);
        print_fines(app, m->member_id);
    }
}

static void cmd_settle_fine(App *app, int argc, char **argv) {
    long amount;
    if (argc != 3 || !parse_long(argv[2], &amount)) { usage_of(argv[0]); return; }
    lending_settle_fine(app, argv[1], amount);
}

static void cmd_mark_lost(App *app, int argc, char **argv) {
    if (argc < 2 || argc > 3) { usage_of(argv[0]); return; }
    lending_mark_lost(app, argv[1], argc == 3 ? argv[2] : NULL);
}

static void cmd_member_nonce(App *app, int argc, char **argv) {
    if (argc != 2) { usage_of(argv[0]); return; }
    const SignerState *s = state_signer_c(app->state, argv[1]);
    if (!s) { printf("ERROR: Book or Member not found (%s).\n", argv[1]); return; }
    long next = lending_expected_nonce(app, argv[1]);
    printf("%s: confirmed nonce %ld (requests mined so far); %ld pending; next request must carry nonce %ld.\n",
           argv[1], s->nonce, next - s->nonce, next);
    printf("  A request with any other nonce is rejected (replay protection). The nonce only advances\n"
           "  when a request is confirmed in a block.\n");
}

static void cmd_advance_time(App *app, int argc, char **argv) {
    (void)app;
    long days;
    if (argc != 2 || !parse_long(argv[1], &days) || days < 1 || days > 365) { usage_of(argv[0]); return; }
    sim_set_offset(sim_get_offset() + days * SECONDS_PER_DAY);
    char now[32];
    format_time(sim_now(), now, sizeof(now));
    printf("Simulated clock moved forward %ld day(s). It is now %s.\n", days, now);
}

/* ===================================================================== */
/* Pool and mining                                                        */
/* ===================================================================== */

static void cmd_pool_view(App *app, int argc, char **argv) {
    (void)argc; (void)argv;
    Pool *p = &app->pool;
    printf("Pending pool: %zu request(s), in mining priority order (top %d form the next block):\n",
           p->count, app->batch_size);
    if (!p->count) return;
    size_t *order = malloc(p->count * sizeof(size_t));
    if (!order) return;
    pool_priority_order(p, order);
    printf("  %-3s %-16s %-9s %-9s %-8s %-5s %-6s %-19s %s\n", "#", "request_id", "action", "copy/book",
           "member", "prio", "nonce", "sort date", "status");
    for (size_t k = 0; k < p->count; k++) {
        const PoolEntry *e = &p->items[order[k]];
        char d[32];
        format_time(e->sort_date, d, sizeof(d));
        const char *cb = e->rec.copy_id[0] ? e->rec.copy_id : (e->rec.book_id[0] ? e->rec.book_id : "-");
        printf("  %-3zu %-16s %-9s %-9s %-8s %-5s %-6ld %-19s %s\n", k + 1, e->rec.record_id,
               action_name(e->rec.action), cb, e->rec.member_id, e->priority == 0 ? "HIGH" : "norm",
               e->rec.nonce, d, pool_status_name(e->status));
        if (e->status == PS_SUSPICIOUS) printf("      flag: %s\n", e->flag);
    }
    free(order);
}

static void cmd_mine_solo(App *app, int argc, char **argv) {
    (void)argc; (void)argv;
    mine_solo(app);
}

static void cmd_mine_pool(App *app, int argc, char **argv) {
    long n = POOL_DEFAULT_MINERS;
    if (argc > 2 || (argc == 2 && !parse_long(argv[1], &n))) { usage_of(argv[0]); return; }
    mine_pool(app, (int)n);
}

static void cmd_mine_cloud(App *app, int argc, char **argv) {
    long rounds;
    if (argc != 2 || !parse_long(argv[1], &rounds)) { usage_of(argv[0]); return; }
    mine_cloud(app, (int)rounds);
}

static void cmd_miner_earnings(App *app, int argc, char **argv) {
    (void)argc; (void)argv;
    printf("Mining payouts (block reward %d coins per block):\n", app->block_reward);
    if (!app->nminers) { printf("  (none yet)\n"); return; }
    for (int i = 0; i < app->nminers; i++)
        printf("  %-20s %10.2f coins  %d block(s)\n", app->miners[i].id, app->miners[i].earned, app->miners[i].blocks);
}

/* ===================================================================== */
/* Tokens and ledgers                                                     */
/* ===================================================================== */

static int need_model(const App *app, LedgerModel m, const char *cmd) {
    if (app->ledger.model == m) return 1;
    printf("'%s' belongs to the %s model; this session uses the %s model (restart with --model %s).\n",
           cmd, ledger_model_name(m), ledger_model_name(app->ledger.model), m == MODEL_UTXO ? "utxo" : "account");
    return 0;
}

static void cmd_token_transfer(App *app, int argc, char **argv) {
    long amount;
    if (argc != 4 || !parse_long(argv[3], &amount)) { usage_of(argv[0]); return; }
    lending_transfer(app, argv[1], argv[2], amount, -1);
}

static void cmd_account_transfer(App *app, int argc, char **argv) {
    long amount, nonce;
    if (argc != 5 || !parse_long(argv[3], &amount) || !parse_long(argv[4], &nonce)) { usage_of(argv[0]); return; }
    if (!need_model(app, MODEL_ACCOUNT, argv[0])) return;
    if (nonce < 0) { printf("ERROR: nonce must not be negative.\n"); return; }
    lending_transfer(app, argv[1], argv[2], amount, nonce);
}

static long pending_in_out(const App *app, const char *member, long *out) {
    long in = 0;
    *out = 0;
    for (size_t i = 0; i < app->pool.count; i++) {
        const Record *r = &app->pool.items[i].rec;
        if (r->action == ACT_TRANSFER && strcmp(r->member_id, member) == 0) *out += r->amount + TX_FEE;
        if (r->action == ACT_TRANSFER && strcmp(r->counterparty, member) == 0) in += r->amount;
        if (r->action == ACT_RETURNED && strcmp(r->member_id, member) == 0) in += r->token_reward - TX_FEE;
    }
    return in;
}

static void cmd_token_balance(App *app, int argc, char **argv) {
    if (argc > 2) { usage_of(argv[0]); return; }
    printf("Token balances (%s model; only confirmed transactions count):\n", ledger_model_name(app->ledger.model));
    for (size_t i = 0; i < app->reg.member_count; i++) {
        const char *id = app->reg.members[i].member_id;
        if (argc == 2 && strcmp(id, argv[1]) != 0) continue;
        long out, in = pending_in_out(app, id, &out);
        printf("  %-9s %6ld coins", id, ledger_balance(&app->ledger, id));
        if (in || out) printf("   (pending: +%ld incoming, -%ld outgoing, not yet mined)", in, out);
        printf("\n");
    }
}

static void cmd_utxo_view(App *app, int argc, char **argv) {
    (void)argc;
    if (!need_model(app, MODEL_UTXO, argv[0])) return;
    utxo_print(&app->ledger.utxo);
    printf("    Spent outputs (cannot be spent again):\n");
    int any = 0;
    for (const Utxo *u = app->ledger.utxo.head; u; u = u->next) {
        if (!u->spent) continue;
        printf("      #%-4d %-9s %-7ld spent by tx %.16s...\n", u->id, u->owner, u->amount, u->spent_by);
        any = 1;
    }
    if (!any) printf("      (none)\n");
}

static void cmd_account_balance(App *app, int argc, char **argv) {
    if (argc > 2) { usage_of(argv[0]); return; }
    if (!need_model(app, MODEL_ACCOUNT, argv[0])) return;
    printf("  %-9s %-8s %-10s %s\n", "Account", "Balance", "Next nonce", "Outgoing transfers");
    for (size_t i = 0; i < app->reg.member_count; i++) {
        const char *id = app->reg.members[i].member_id;
        if (argc == 2 && strcmp(id, argv[1]) != 0) continue;
        const Account *a = account_find(&app->ledger.acc, id);
        const SignerState *s = state_signer_c(app->state, id);
        printf("  %-9s %-8ld %-10ld %d\n", id, a ? a->balance : 0, s ? s->nonce : 0, a ? a->outgoing : 0);
    }
}

static void cmd_account_nonce(App *app, int argc, char **argv) {
    if (argc != 2) { usage_of(argv[0]); return; }
    if (!need_model(app, MODEL_ACCOUNT, argv[0])) return;
    const SignerState *s = state_signer_c(app->state, argv[1]);
    if (!s) { printf("ERROR: Book or Member not found\n"); return; }
    const Account *a = account_find(&app->ledger.acc, argv[1]);
    printf("Account %s: next nonce %ld (confirmed); next submission must use %ld; %d outgoing transfer(s).\n",
           argv[1], s->nonce, lending_expected_nonce(app, argv[1]), a ? a->outgoing : 0);
    printf("  The account nonce is the member's request nonce: it increments with every confirmed\n"
           "  outgoing transaction, and a transfer with a wrong or reused nonce is rejected.\n");
}

static void cmd_transaction_history(App *app, int argc, char **argv) {
    if (argc != 2) { usage_of(argv[0]); return; }
    if (!registry_member(&app->reg, argv[1])) { printf("ERROR: Book or Member not found\n"); return; }
    if (app->ledger.model == MODEL_ACCOUNT) {
        account_print_history(&app->ledger.acc, argv[1]);
        return;
    }
    printf("Token transactions involving %s (UTXO model; from the chain):\n", argv[1]);
    int n = 0;
    for (const Block *b = app->chain.head; b; b = b->next) {
        for (int i = 0; i < b->record_count; i++) {
            const Record *r = &b->records[i];
            if (r->action == ACT_RETURNED && r->token_reward && strcmp(r->member_id, argv[1]) == 0)
                printf("  block %-3d REWARD   SYSTEM -> %-8s %d (fee %d)\n", b->index, r->member_id, r->token_reward - TX_FEE, TX_FEE), n++;
            if (r->action == ACT_TRANSFER && (strcmp(r->member_id, argv[1]) == 0 || strcmp(r->counterparty, argv[1]) == 0))
                printf("  block %-3d TRANSFER %s -> %-8s %ld (fee %d, nonce %ld)\n", b->index, r->member_id,
                       r->counterparty, r->amount, TX_FEE, r->nonce), n++;
        }
    }
    if (!n) printf("  (none)\n");
    printf("  Balance now: %ld\n", ledger_balance(&app->ledger, argv[1]));
}

/* ===================================================================== */
/* Fraud review                                                           */
/* ===================================================================== */

static void cmd_fraud_review(App *app, int argc, char **argv) {
    (void)argc; (void)argv;
    int n = 0;
    for (size_t i = 0; i < app->pool.count; i++) {
        const PoolEntry *e = &app->pool.items[i];
        if (e->status != PS_SUSPICIOUS) continue;
        if (!n++) printf("Requests flagged SUSPICIOUS (withheld from mining until reviewed):\n");
        char ts[32];
        format_time(e->rec.timestamp, ts, sizeof(ts));
        printf("  %s  %-9s %-9s %-10s signer %-9s nonce %ld  %s\n", e->rec.record_id, action_name(e->rec.action),
               e->rec.member_id, e->rec.copy_id[0] ? e->rec.copy_id : e->rec.book_id, e->rec.signer_id,
               e->rec.nonce, ts);
        printf("    reason: %s\n", e->flag);
    }
    if (!n) printf("No SUSPICIOUS requests in the pool.\n");
    else printf("Use approve_suspicious <id> to allow mining, or reject_suspicious <id> to discard.\n");
}

static void cmd_approve_suspicious(App *app, int argc, char **argv) {
    if (argc != 2) { usage_of(argv[0]); return; }
    lending_approve(app, argv[1]);
}

static void cmd_reject_suspicious(App *app, int argc, char **argv) {
    if (argc != 2) { usage_of(argv[0]); return; }
    lending_reject(app, argv[1]);
}

static void cmd_replay_request(App *app, int argc, char **argv) {
    if (argc != 2) { usage_of(argv[0]); return; }
    lending_replay(app, argv[1]);
}

/* ===================================================================== */
/* Audit                                                                  */
/* ===================================================================== */

/* Prints chain records matching a filter, oldest first. */
static void history(const App *app, const char *member, const char *book) {
    int n = 0;
    for (const Block *b = app->chain.head; b; b = b->next) {
        for (int i = 0; i < b->record_count; i++) {
            const Record *r = &b->records[i];
            if (member && strcmp(r->member_id, member) != 0 && strcmp(r->signer_id, member) != 0 &&
                strcmp(r->counterparty, member) != 0) continue;
            if (book && strcmp(r->book_id, book) != 0) continue;
            char ts[32];
            format_time(r->timestamp, ts, sizeof(ts));
            printf("  block %-3d %s %-9s %-10s %-8s %-30.30s", b->index, ts, action_name(r->action),
                   r->copy_id[0] ? r->copy_id : "-", r->member_id, r->book_title[0] ? r->book_title : "-");
            if (r->fine_amount) printf(" fine %ld", r->fine_amount);
            if (r->token_reward) printf(" reward %d", r->token_reward);
            if (r->amount) printf(" amount %ld", r->amount);
            if (r->counterparty[0]) printf(" to %s", r->counterparty);
            if (r->reason[0]) printf(" reason %s", r->reason);
            printf("  sig %s\n", record_verify(r) ? "VALID" : "INVALID");
            n++;
        }
    }
    if (!n) printf("  (no confirmed records)\n");
}

static void cmd_lending_history(App *app, int argc, char **argv) {
    (void)argc; (void)argv;
    printf("All confirmed lending records:\n");
    history(app, NULL, NULL);
}

static void cmd_member_history(App *app, int argc, char **argv) {
    if (argc != 2) { usage_of(argv[0]); return; }
    if (!registry_member(&app->reg, argv[1])) { printf("ERROR: Book or Member not found\n"); return; }
    printf("Confirmed records for member %s:\n", argv[1]);
    history(app, argv[1], NULL);
}

static void cmd_book_history(App *app, int argc, char **argv) {
    if (argc != 2) { usage_of(argv[0]); return; }
    if (!registry_book(&app->reg, argv[1])) { printf("ERROR: Book or Member not found\n"); return; }
    printf("Confirmed records for book %s:\n", argv[1]);
    history(app, NULL, argv[1]);
}

/* ===================================================================== */
/* Blockchain                                                             */
/* ===================================================================== */

static void print_record_line(const Record *r) {
    char due[16] = "-";
    if (r->due_date) format_date(r->due_date, due, sizeof(due));
    printf("      %-16s %-9s %-9s %-10s %-8s due %-10s fine %-3ld reward %-2d nonce %ld signer %s\n",
           r->record_id, action_name(r->action), r->copy_id[0] ? r->copy_id : "-",
           r->member_id, r->book_id[0] ? r->book_id : "-", due, r->fine_amount, r->token_reward,
           r->nonce, r->signer_id);
}

static void print_block(const Block *b, int with_records) {
    char ts[32];
    format_time(b->timestamp, ts, sizeof(ts));
    printf("  Block %d  sealed by %s  at %s\n", b->index, b->sealed_by, ts);
    printf("    records=%d  token_reward=%d  transaction_id=%s\n", b->record_count, b->token_reward,
           b->transaction_id[0] ? b->transaction_id : "none");
    printf("    merkle_root=%s\n    previous   =%s\n    hash       =%s\n", b->merkle_root,
           b->previous_hash, b->hash);
    printf("    difficulty=%d  nonce=%lu\n", b->difficulty, b->nonce);
    if (with_records)
        for (int i = 0; i < b->record_count; i++) print_record_line(&b->records[i]);
}

static void cmd_chain_view(App *app, int argc, char **argv) {
    if (argc > 2) { usage_of(argv[0]); return; }
    if (argc == 2) {
        long idx;
        const Block *b = parse_long(argv[1], &idx) ? chain_block(&app->chain, (int)idx) : NULL;
        if (!b) { printf("ERROR: no block %s.\n", argv[1]); return; }
        print_block(b, 1);
        return;
    }
    printf("Chain: %zu block(s)\n", app->chain.length);
    for (const Block *b = app->chain.head; b; b = b->next) print_block(b, 1);
}

static void cmd_merkle_view(App *app, int argc, char **argv) {
    long idx;
    if (argc != 2 || !parse_long(argv[1], &idx)) { usage_of(argv[0]); return; }
    const Block *b = chain_block(&app->chain, (int)idx);
    if (!b) { printf("ERROR: no block %s.\n", argv[1]); return; }
    printf("Merkle tree of block %d (%d record%s):\n", b->index, b->record_count, b->record_count == 1 ? "" : "s");
    char (*leaves)[HASH_HEX_LEN] = NULL;
    if (b->record_count > 0) {
        leaves = malloc((size_t)b->record_count * HASH_HEX_LEN);
        if (!leaves) return;
        for (int i = 0; i < b->record_count; i++) record_hash(&b->records[i], leaves[i]);
    }
    merkle_print(leaves, (size_t)b->record_count);
    char root[HASH_HEX_LEN];
    block_merkle(b, root);
    printf("  Recomputed root: %s\n  Stored root    : %s  -> %s\n", root, b->merkle_root,
           strcmp(root, b->merkle_root) == 0 ? "MATCH" : "MISMATCH");
    free(leaves);
}

static void cmd_validate_chain(App *app, int argc, char **argv) {
    (void)argc; (void)argv;
    FullCheck v = persist_validate(app, NULL);
    printf("Chain status: %zu block(s), %s\n", app->chain.length, v.valid ? "VALID" : "COMPROMISED");
    if (v.valid) {
        printf("  Checked for every block: index order, previous_hash link, strictly increasing timestamp,\n"
               "  each record's ID and ECDSA signature against the signer's registered public key, the\n"
               "  Merkle root recomputed from the records, token_reward and transaction_id, the header\n"
               "  hash and proof of work; then every record was replayed through the state machine\n"
               "  (nonces, copy states, fines and rewards).\n");
        if (app->read_only) {
            app->read_only = 0;
            printf("  The session is no longer read-only.\n");
        }
    } else {
        printf("  First problem at block %d: %s\n", v.bad_index, v.reason);
        app->read_only = 1;
    }
}

static void cmd_chain_save(App *app, int argc, char **argv) {
    (void)argc; (void)argv;
    if (app->read_only) { printf("ERROR: refusing to save a chain that failed validation.\n"); return; }
    if (persist_save(app))
        printf("Saved %s (%zu blocks), %s (inventory, queues, nonces, fines, balances, settings) and %s (%zu pending).\n",
               CHAIN_PATH, app->chain.length, STATE_PATH, POOL_PATH, app->pool.count);
    else
        printf("ERROR: could not save.\n");
}

static void cmd_chain_load(App *app, int argc, char **argv) {
    (void)argc; (void)argv;
    LedgerModel model = app->ledger.model;
    chain_free(&app->chain);
    pool_free(&app->pool);
    ledger_free(&app->ledger);
    free(app->state);
    app->state = NULL;
    app->nminers = 0;
    app->read_only = 0;
    printf("Discarding the in-memory state and reloading from disk...\n");
    if (!persist_load(app, model, 0, 0)) {
        printf("FATAL: reload failed.\n");
        exit(1);
    }
}

static void cmd_batch_size(App *app, int argc, char **argv) {
    long n;
    if (argc == 2) {
        if (!parse_long(argv[1], &n) || n < MIN_BATCH_SIZE || n > MAX_BATCH_SIZE) {
            printf("ERROR: batch size must be %d-%d.\n", MIN_BATCH_SIZE, MAX_BATCH_SIZE);
            return;
        }
        app->batch_size = (int)n;
    } else if (argc != 1) {
        usage_of(argv[0]);
        return;
    }
    printf("Blocks are filled with up to %d record(s): fixed-size batching, taking the top of the pool.\n", app->batch_size);
}

static void cmd_difficulty_status(App *app, int argc, char **argv) {
    if (argc == 3 && strcasecmp(argv[1], "set") == 0) {
        long d;
        if (!parse_long(argv[2], &d) || d < MIN_DIFFICULTY || d > MAX_DIFFICULTY) {
            printf("ERROR: difficulty must be a whole number from %d to %d.\n", MIN_DIFFICULTY, MAX_DIFFICULTY);
            return;
        }
        app->difficulty = (int)d;
        printf("Difficulty set to %d.\n", app->difficulty);
    } else if (argc != 1) {
        usage_of(argv[0]);
        return;
    }
    printf("Difficulty %d: block hashes must start with %d zero%s (about %lu attempts expected).\n",
           app->difficulty, app->difficulty, app->difficulty == 1 ? "" : "s", 1UL << (4 * app->difficulty));
}

/* ===================================================================== */
/* Command table                                                          */
/* ===================================================================== */

static void cmd_help(App *app, int argc, char **argv);

static const Command COMMANDS[] = {
    { "#", NULL, "", "Membership" },
    { "register_member", cmd_register_member, "register_member <id> \"<full name>\" <course>", "Register a member; generates their ECDSA key pair" },
    { "view_member",     cmd_view_member,     "view_member <member_id>", "Member, public key, nonce, holdings, fines" },
    { "member_nonce",    cmd_member_nonce,    "member_nonce <member_id>", "Confirmed and expected next nonce" },
    { "#", NULL, "", "Books and copies" },
    { "register_book",   cmd_register_book,   "register_book <book_id> \"<title>\" \"<author>\"", "Register a title" },
    { "view_book",       cmd_view_book,       "view_book <book_id>", "A title, its copies and its queue" },
    { "add_copy",        cmd_add_copy,        "add_copy <book_id> [NEW|GOOD|WORN]", "Add a physical copy" },
    { "view_copy",       cmd_view_copy,       "view_copy <copy_id>", "One copy and its pending requests" },
    { "mark_lost",       cmd_mark_lost,       "mark_lost <copy_id> <reason_code>", "Librarian override: copy becomes LOST" },
    { "#", NULL, "", "Lending" },
    { "borrow_book",     cmd_borrow_book,     "borrow_book <member_id> <book_id> [copy_id]", "Request to borrow a copy" },
    { "return_book",     cmd_return_book,     "return_book <copy_id> [NEW|GOOD|WORN] [reason]", "Request to return a copy" },
    { "renew_book",      cmd_renew_book,      "renew_book <copy_id>", "Extend the due date" },
    { "reserve_book",    cmd_reserve_book,    "reserve_book <member_id> <book_id>", "Join the title's reservation queue" },
    { "reservation_status", cmd_reservation_status, "reservation_status [book_id]", "Queues and copies held for members" },
    { "#", NULL, "", "Fines and overdue" },
    { "check_overdue",   cmd_check_overdue,   "check_overdue", "Flag overdue copies (also runs before every command)" },
    { "view_fines",      cmd_view_fines,      "view_fines [member_id]", "Fines owed, paid and accruing" },
    { "settle_fine",     cmd_settle_fine,     "settle_fine <member_id> <amount>", "Pay a fine" },
    { "advance_time",    cmd_advance_time,    "advance_time <days>", "Move the simulated clock forward (demo)" },
    { "#", NULL, "", "Tokens and ledgers" },
    { "token_transfer",  cmd_token_transfer,  "token_transfer <from> <to> <amount>", "Send tokens (either model)" },
    { "token_balance",   cmd_token_balance,   "token_balance [member_id]", "Token balances (either model)" },
    { "utxo_view",       cmd_utxo_view,       "utxo_view", "UTXO model: unspent and spent outputs" },
    { "account_balance", cmd_account_balance, "account_balance [member_id]", "Account model: balances and nonces" },
    { "account_transfer", cmd_account_transfer, "account_transfer <from> <to> <amount> <nonce>", "Account model: transfer with an explicit nonce" },
    { "account_nonce",   cmd_account_nonce,   "account_nonce <member_id>", "Account model: a member's nonce" },
    { "transaction_history", cmd_transaction_history, "transaction_history <member_id>", "A member's token transactions" },
    { "#", NULL, "", "Blockchain and mining" },
    { "pool_view",       cmd_pool_view,       "pool_view", "Pending pool in priority order" },
    { "mine_solo",       cmd_mine_solo,       "mine_solo", "Solo-mine the next block" },
    { "mine_pool",       cmd_mine_pool,       "mine_pool [miners 2-8]", "Pool-mine the next block" },
    { "mine_cloud",      cmd_mine_cloud,      "mine_cloud <rounds 1-5>", "Rent cloud mining for N rounds" },
    { "miner_earnings",  cmd_miner_earnings,  "miner_earnings", "Mining payouts so far" },
    { "chain_view",      cmd_chain_view,      "chain_view [block_index]", "Blocks and their records" },
    { "merkle_view",     cmd_merkle_view,     "merkle_view <block_index>", "A block's Merkle tree" },
    { "validate_chain",  cmd_validate_chain,  "validate_chain", "Full chain verification" },
    { "chain_save",      cmd_chain_save,      "chain_save", "Save chain, state and pool to disk" },
    { "chain_load",      cmd_chain_load,      "chain_load", "Reload everything from disk and re-validate" },
    { "difficulty_status", cmd_difficulty_status, "difficulty_status [set <1-4>]", "Show or set the difficulty" },
    { "batch_size",      cmd_batch_size,      "batch_size [1-16]", "Show or set records per block" },
    { "#", NULL, "", "Fraud and audit" },
    { "fraud_review",    cmd_fraud_review,    "fraud_review", "Requests flagged SUSPICIOUS" },
    { "approve_suspicious", cmd_approve_suspicious, "approve_suspicious <request_id>", "Allow a flagged request to be mined" },
    { "reject_suspicious", cmd_reject_suspicious, "reject_suspicious <request_id>", "Discard a flagged request" },
    { "replay_request",  cmd_replay_request,  "replay_request <request_id>", "Attack demo: resubmit a captured signed request" },
    { "lending_history", cmd_lending_history, "lending_history", "All confirmed records" },
    { "member_history",  cmd_member_history,  "member_history <member_id>", "One member's confirmed records" },
    { "book_history",    cmd_book_history,    "book_history <book_id>", "One title's confirmed records" },
    { "#", NULL, "", "Other" },
    { "help",            cmd_help,            "help", "This list" },
};
#define N_COMMANDS (sizeof(COMMANDS) / sizeof(COMMANDS[0]))

static void usage_of(const char *name) {
    for (size_t i = 0; i < N_COMMANDS; i++) {
        if (COMMANDS[i].fn && strcasecmp(COMMANDS[i].name, name) == 0) { printf("Usage: %s\n", COMMANDS[i].usage); return; }
    }
}

static void cmd_help(App *app, int argc, char **argv) {
    (void)app; (void)argc; (void)argv;
    for (size_t i = 0; i < N_COMMANDS; i++) {
        if (!COMMANDS[i].fn) { printf("%s:\n", COMMANDS[i].help); continue; }
        printf("  %-50s %s\n", COMMANDS[i].usage, COMMANDS[i].help);
    }
    printf("  %-50s %s\n", "exit", "Save and quit");
}

int cli_run(App *app) {
    char line[LINE_LEN];
    printf("ALU Library Lending Chain%s. Type 'help' for commands.\n", app->read_only ? " (READ-ONLY: chain failed validation)" : "");
    for (;;) {
        printf("> ");
        fflush(stdout);
        if (!fgets(line, sizeof(line), stdin)) { printf("\n"); break; }
        str_trim(line);
        if (!line[0]) continue;
        printf("\n");
        char *argv[MAX_TOKENS];
        int argc = str_tokenize(line, argv, MAX_TOKENS);
        if (argc == 0) continue;
        if (strcasecmp(argv[0], "exit") == 0 || strcasecmp(argv[0], "quit") == 0) break;

        const Command *cmd = NULL;
        for (size_t i = 0; i < N_COMMANDS; i++)
            if (COMMANDS[i].fn && strcasecmp(COMMANDS[i].name, argv[0]) == 0) { cmd = &COMMANDS[i]; break; }
        if (!cmd) { printf("Unknown command '%s'. Type 'help'.\n", argv[0]); continue; }
        /* Overdue detection runs on every command (check_overdue does it itself). */
        if (cmd->fn != cmd_check_overdue && cmd->fn != cmd_help) lending_scan_overdue(app, 0);
        cmd->fn(app, argc, argv);
        /* Persist after every command so nothing is lost if the program is
         * killed (a read-only session never writes). */
        if (!app->read_only && cmd->fn != cmd_chain_load) persist_save(app);
    }
    return 0;
}
