#include "lending.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---- Projected state ------------------------------------------------------------- */

/* The projected state is large, so it lives in one static buffer. */
static LibState *projected(const App *app) {
    static LibState *buf = NULL;
    if (!buf) buf = malloc(sizeof(LibState));
    if (!buf) { fprintf(stderr, "FATAL: out of memory\n"); exit(1); }
    pool_project(&app->pool, app->state, &app->reg, buf, NULL, NULL);
    return buf;
}

long lending_expected_nonce(const App *app, const char *signer_id) {
    const SignerState *s = state_signer_c(projected(app), signer_id);
    return s ? s->nonce : -1;
}

/* ---- Common submission path ----------------------------------------------------- */

static void fill_member(const App *app, Record *r, const char *member_id) {
    const Member *m = registry_member(&app->reg, member_id);
    snprintf(r->member_id, ID_LEN, "%s", member_id);
    snprintf(r->member_name, NAME_LEN, "%s", m ? m->full_name : "");
}

static void fill_copy(const App *app, Record *r, const char *copy_id) {
    const CopyInfo *ci = registry_copy(&app->reg, copy_id);
    const Book *b = ci ? registry_book(&app->reg, ci->book_id) : NULL;
    snprintf(r->copy_id, COPY_ID_LEN, "%s", copy_id);
    snprintf(r->book_id, ID_LEN, "%s", ci ? ci->book_id : "");
    snprintf(r->book_title, TITLE_LEN, "%s", b ? b->title : "");
}

/* Priority rules: overdue-related requests (OVERDUE records, returns of
 * overdue copies, fine settlements) come first; then the oldest reservation
 * date. For a borrow of a copy held for a reservation, the reservation date
 * is used, so whoever waited longest is served first. */
static void priority_of(const LibState *proj, const Record *r, int *prio, time_t *date) {
    *prio = 1;
    *date = r->timestamp;
    const CopyState *c = r->copy_id[0] ? state_copy_c(proj, r->copy_id) : NULL;
    if (r->action == ACT_OVERDUE || r->action == ACT_FINE_PAID) *prio = 0;
    if (r->action == ACT_RETURNED && c && c->due_date && days_late(c->due_date, r->timestamp) > 0) *prio = 0;
    if (r->action == ACT_BORROWED && c && c->status == ST_RESERVED && c->reserved_since)
        *date = c->reserved_since;
}

/* Hook for the misuse heuristics (fraud.c). Returns 1 and fills `why` if the
 * request should be flagged SUSPICIOUS. */
int fraud_check(const App *app, const LibState *proj, const Record *r, char *why, size_t cap);

static int submit(App *app, Record *r, int already_signed) {
    if (app->read_only) {
        printf("ERROR: the chain failed validation; no new requests are accepted until it is fixed.\n");
        return 0;
    }
    LibState *proj = projected(app);
    char err[256], why[FLAG_LEN] = "";

    if (!already_signed) {
        const SignerState *signer = state_signer_c(proj, r->signer_id);
        const char *pub = registry_pubkey(&app->reg, r->signer_id);
        if (!signer || !pub) { printf("ERROR: Book or Member not found (signer %s).\n", r->signer_id); return 0; }
        if (r->nonce < 0) r->nonce = signer->nonce;       /* use the expected next nonce */
        snprintf(r->pubkey, PUBKEY_HEX_LEN, "%s", pub);
    }

    /* Heuristics look at the state *before* this request is applied. */
    int suspicious = fraud_check(app, proj, r, why, sizeof(why));

    /* A replayed record is a duplicate: it is flagged and parked for review
     * without being validated, since it would fail the nonce check anyway. */
    if (!(already_signed && suspicious)) {
        LibState *trial = malloc(sizeof(LibState));
        if (!trial) return 0;
        memcpy(trial, proj, sizeof(LibState));
        int ok = state_apply(trial, &app->reg, r, err, sizeof(err), NULL);
        free(trial);
        if (!ok) { printf("REJECTED: %s.\n", err); return 0; }
    }

    if (!already_signed) {
        EVP_PKEY *priv = registry_private_key(&app->reg, r->signer_id);
        if (!priv || !record_sign(r, priv)) { printf("ERROR: could not sign the request.\n"); return 0; }
    }
    /* Every request is verified before it may enter the pool. */
    const char *registered = registry_pubkey(&app->reg, r->signer_id);
    if (!record_verify(r) || !registered || strcmp(registered, r->pubkey) != 0) {
        printf("REJECTED: invalid or missing ECDSA signature for %s.\n", r->signer_id);
        return 0;
    }

    int prio;
    time_t date;
    priority_of(proj, r, &prio, &date);
    PoolEntry *e = pool_add(&app->pool, r, prio, date, suspicious ? PS_SUSPICIOUS : PS_PENDING, why);
    if (!e) { printf("ERROR: out of memory.\n"); return 0; }

    char subject[64];
    const char *item = r->copy_id[0] ? r->copy_id : r->book_id;
    snprintf(subject, sizeof(subject), "%s%s%s", r->member_id, (r->member_id[0] && item[0]) ? " / " : "", item);
    printf("%s: %s request %s for %s signed by %s (nonce %ld, ECDSA signature verified).\n",
           suspicious ? "FLAGGED SUSPICIOUS" : "PENDING", action_name(r->action), r->record_id,
           subject, r->signer_id, r->nonce);
    if (suspicious)
        printf("  Reason: %s\n  It stays in the pool but will not be mined until a librarian runs "
               "approve_suspicious %s (or reject_suspicious %s).\n", why, r->record_id, r->record_id);
    else
        printf("  Waiting in the pending pool (priority %s). Nothing changes until it is mined.\n",
               prio == 0 ? "HIGH: overdue-related" : "normal");
    return 1;
}

static Record new_record(Action a, const char *signer) {
    Record r;
    memset(&r, 0, sizeof(r));
    r.timestamp = sim_now();
    r.action = a;
    r.nonce = -1;
    snprintf(r.signer_id, ID_LEN, "%s", signer);
    return r;
}

/* ---- Operations ------------------------------------------------------------------- */

int lending_borrow(App *app, const char *member_id, const char *book_id, const char *copy_id) {
    if (!registry_member(&app->reg, member_id) || !registry_book(&app->reg, book_id)) {
        printf("ERROR: Book or Member not found\n");
        return 0;
    }
    LibState *proj = projected(app);
    const CopyState *chosen = NULL;
    if (copy_id) {
        chosen = state_copy_c(proj, copy_id);
        if (!chosen || strcmp(chosen->book_id, book_id) != 0) {
            printf("ERROR: copy %s is not a copy of %s.\n", copy_id, book_id);
            return 0;
        }
    } else {
        /* A copy held for this member's reservation comes first, then any
         * available copy. */
        for (int i = 0; i < proj->ncopies && !chosen; i++) {
            const CopyState *c = &proj->copies[i];
            if (strcmp(c->book_id, book_id) == 0 && c->status == ST_RESERVED && strcmp(c->holder, member_id) == 0)
                chosen = c;
        }
        for (int i = 0; i < proj->ncopies && !chosen; i++) {
            const CopyState *c = &proj->copies[i];
            if (strcmp(c->book_id, book_id) == 0 && c->status == ST_AVAILABLE) chosen = c;
        }
        if (!chosen) {
            printf("No copy of %s is available. Use reserve_book %s %s to join the reservation queue.\n",
                   book_id, member_id, book_id);
            return 0;
        }
    }
    Record r = new_record(ACT_BORROWED, member_id);
    fill_member(app, &r, member_id);
    fill_copy(app, &r, chosen->copy_id);
    r.due_date = r.timestamp + (time_t)LOAN_DAYS * SECONDS_PER_DAY;
    return submit(app, &r, 0);
}

int lending_return(App *app, const char *copy_id, const char *condition, const char *reason) {
    LibState *proj = projected(app);
    const CopyState *c = state_copy_c(proj, copy_id);
    if (!c) { printf("ERROR: copy %s not found.\n", copy_id); return 0; }
    if (c->status != ST_BORROWED) {
        printf("ERROR: copy %s has no open BORROWED record (it is %s).\n", copy_id, copy_status_name(c->status));
        return 0;
    }
    Condition cond = c->condition;
    if (condition && !condition_parse(condition, &cond)) {
        printf("ERROR: condition must be NEW, GOOD or WORN.\n");
        return 0;
    }
    if (reason && !valid_id(reason)) { printf("ERROR: reason code must be a short word (letters, digits, '-', '_').\n"); return 0; }

    Record r = new_record(ACT_RETURNED, c->holder);
    fill_member(app, &r, c->holder);
    fill_copy(app, &r, copy_id);
    long late = days_late(c->due_date, r.timestamp);
    r.due_date = c->due_date;
    r.fine_amount = late * FINE_PER_DAY;
    r.token_reward = late > 0 ? REWARD_LATE : REWARD_ON_TIME;
    snprintf(r.condition, COND_LEN, "%s", condition_name(cond));
    if (reason) snprintf(r.reason, REASON_LEN, "%s", reason);
    if (late > 0)
        printf("Late return: %ld day(s) late, fine %ld coins; reward will be %d coins.\n", late, r.fine_amount, REWARD_LATE);
    else
        printf("On-time return: reward will be %d coins.\n", REWARD_ON_TIME);
    return submit(app, &r, 0);
}

int lending_renew(App *app, const char *copy_id) {
    LibState *proj = projected(app);
    const CopyState *c = state_copy_c(proj, copy_id);
    if (!c) { printf("ERROR: copy %s not found.\n", copy_id); return 0; }
    if (c->status != ST_BORROWED) { printf("ERROR: copy %s is not on loan.\n", copy_id); return 0; }
    Record r = new_record(ACT_RENEWED, c->holder);
    fill_member(app, &r, c->holder);
    fill_copy(app, &r, copy_id);
    r.due_date = c->due_date + (time_t)RENEW_DAYS * SECONDS_PER_DAY;
    char d[16];
    format_date(r.due_date, d, sizeof(d));
    printf("Renewal %d of %d: new due date %s.\n", c->renewals + 1, MAX_RENEWALS, d);
    return submit(app, &r, 0);
}

int lending_reserve(App *app, const char *member_id, const char *book_id) {
    const Book *b = registry_book(&app->reg, book_id);
    if (!registry_member(&app->reg, member_id) || !b) { printf("ERROR: Book or Member not found\n"); return 0; }
    Record r = new_record(ACT_RESERVED, member_id);
    fill_member(app, &r, member_id);
    snprintf(r.book_id, ID_LEN, "%s", book_id);
    snprintf(r.book_title, TITLE_LEN, "%s", b->title);
    return submit(app, &r, 0);
}

int lending_settle_fine(App *app, const char *member_id, long amount) {
    if (!registry_member(&app->reg, member_id)) { printf("ERROR: Book or Member not found\n"); return 0; }
    if (amount <= 0) { printf("ERROR: amount must be a positive whole number.\n"); return 0; }
    Record r = new_record(ACT_FINE_PAID, member_id);
    fill_member(app, &r, member_id);
    r.amount = amount;
    return submit(app, &r, 0);
}

int lending_mark_lost(App *app, const char *copy_id, const char *reason) {
    LibState *proj = projected(app);
    const CopyState *c = state_copy_c(proj, copy_id);
    if (!c) { printf("ERROR: copy %s not found.\n", copy_id); return 0; }
    if (!reason || !valid_id(reason)) {
        printf("ERROR: a reason code is required (e.g. DAMAGED, MISSING-AUDIT).\n");
        return 0;
    }
    Record r = new_record(ACT_LOST, LIBRARIAN_ID);
    if (c->status == ST_BORROWED) fill_member(app, &r, c->holder);
    fill_copy(app, &r, copy_id);
    snprintf(r.condition, COND_LEN, "%s", condition_name(COND_LOST));
    snprintf(r.reason, REASON_LEN, "%s", reason);
    return submit(app, &r, 0);
}

/* Coins the sender has committed to pending transfers (amount + fee each). */
static long pending_outgoing(const App *app, const char *member_id) {
    long total = 0;
    for (size_t i = 0; i < app->pool.count; i++) {
        const Record *r = &app->pool.items[i].rec;
        if (r->action == ACT_TRANSFER && strcmp(r->member_id, member_id) == 0) total += r->amount + TX_FEE;
    }
    return total;
}

int lending_transfer(App *app, const char *from, const char *to, long amount, long nonce) {
    if (!registry_member(&app->reg, from) || !registry_member(&app->reg, to)) {
        printf("ERROR: Book or Member not found\n");
        return 0;
    }
    if (amount <= 0) { printf("ERROR: amount must be a positive whole number.\n"); return 0; }
    /* Double-spend prevention at submission: coins already promised to
     * pending transfers cannot be promised again. */
    long confirmed = ledger_balance(&app->ledger, from);
    long spendable = confirmed - pending_outgoing(app, from);
    if (spendable < amount + TX_FEE) {
        printf("REJECTED: insufficient funds: %s has %ld confirmed, %ld already committed to pending "
               "transfers, needs amount %ld + fee %d = %ld.\n", from, confirmed, confirmed - spendable,
               amount, TX_FEE, amount + TX_FEE);
        return 0;
    }
    Record r = new_record(ACT_TRANSFER, from);
    fill_member(app, &r, from);
    snprintf(r.counterparty, ID_LEN, "%s", to);
    r.amount = amount;
    r.nonce = nonce;   /* -1 = use the expected next nonce */
    return submit(app, &r, 0);
}

/* A pending OVERDUE (or return) for this copy already exists in the pool. */
static int overdue_pending(const App *app, const char *copy_id) {
    for (size_t i = 0; i < app->pool.count; i++) {
        const Record *r = &app->pool.items[i].rec;
        if (strcmp(r->copy_id, copy_id) == 0 && (r->action == ACT_OVERDUE || r->action == ACT_RETURNED))
            return 1;
    }
    return 0;
}

int lending_scan_overdue(App *app, int quiet) {
    if (app->read_only) return 0;
    int made = 0;
    time_t now = sim_now();
    for (int i = 0; i < app->state->ncopies; i++) {
        const CopyState *c = &app->state->copies[i];
        if (c->status != ST_BORROWED || c->overdue_flagged || days_late(c->due_date, now) < 1) continue;
        if (overdue_pending(app, c->copy_id)) continue;
        Record r = new_record(ACT_OVERDUE, LIBRARIAN_ID);
        fill_member(app, &r, c->holder);
        fill_copy(app, &r, c->copy_id);
        r.due_date = c->due_date;
        long late = days_late(c->due_date, r.timestamp);
        r.fine_amount = late * FINE_PER_DAY;
        if (!quiet)
            printf("[overdue check] %s held by %s is %ld day(s) past due: fine so far %ld coins.\n",
                   c->copy_id, c->holder, late, r.fine_amount);
        if (submit(app, &r, 0)) made++;
    }
    return made;
}

int lending_replay(App *app, const char *request_id) {
    const Record *orig = NULL;
    for (const Block *b = app->chain.head; b && !orig; b = b->next)
        for (int i = 0; i < b->record_count; i++)
            if (strcmp(b->records[i].record_id, request_id) == 0) { orig = &b->records[i]; break; }
    PoolEntry *pe = orig ? NULL : pool_find(&app->pool, request_id);
    if (pe) orig = &pe->rec;
    if (!orig) { printf("ERROR: no record %s in the chain or the pool.\n", request_id); return 0; }
    Record copy = *orig;   /* byte-for-byte replay of a captured signed request */
    printf("Replaying captured request %s unchanged (simulated attacker)...\n", request_id);
    return submit(app, &copy, 1);
}

void lending_prune_pool(App *app) {
    size_t n = 0;
    char (*bad)[RECORD_ID_LEN] = malloc((app->pool.count + 1) * RECORD_ID_LEN);
    LibState *tmp = malloc(sizeof(LibState));
    if (!bad || !tmp) { free(bad); free(tmp); return; }
    pool_project(&app->pool, app->state, &app->reg, tmp, bad, &n);
    for (size_t i = 0; i < n; i++) {
        /* Only PENDING entries are dropped; SUSPICIOUS ones (including replayed
         * duplicates) stay for the librarian to review. */
        PoolEntry *e = pool_find_status(&app->pool, bad[i], PS_PENDING);
        if (!e) continue;
        printf("Dropped pending request %s (%s %s): it no longer applies to the current state.\n",
               e->rec.record_id, action_name(e->rec.action), e->rec.copy_id);
        pool_remove_seq(&app->pool, e->seq);
    }
    free(bad);
    free(tmp);
}

int lending_approve(App *app, const char *request_id) {
    PoolEntry *e = pool_find_status(&app->pool, request_id, PS_SUSPICIOUS);
    if (!e) { printf("ERROR: %s is not a SUSPICIOUS request in the pool.\n", request_id); return 0; }
    e->status = PS_PENDING;
    printf("Approved %s: status PENDING, so the next mining run may include it (it is still fully\n"
           "validated at that point). The original flag is kept for audit: %s\n", request_id, e->flag);
    return 1;
}

/* 1 if a record with this ID is on the chain or still in the pool. */
static int id_known(const App *app, const char *id) {
    for (const Block *b = app->chain.head; b; b = b->next)
        for (int i = 0; i < b->record_count; i++)
            if (strcmp(b->records[i].record_id, id) == 0) return 1;
    for (size_t i = 0; i < app->pool.count; i++)
        if (strcmp(app->pool.items[i].rec.record_id, id) == 0) return 1;
    return 0;
}

int lending_reject(App *app, const char *request_id) {
    PoolEntry *e = pool_find_status(&app->pool, request_id, PS_SUSPICIOUS);
    if (!e) { printf("ERROR: %s is not a SUSPICIOUS request in the pool.\n", request_id); return 0; }
    Record r = e->rec;
    pool_remove_seq(&app->pool, e->seq);
    printf("Rejected and discarded %s (%s by %s).\n", request_id, action_name(r.action), r.signer_id);

    /* If the discarded request held a real nonce slot (it was not a replay of
     * a record that still exists), later requests by the same signer used
     * the nonces after it and can never be mined: drop them too. */
    const SignerState *s = state_signer_c(app->state, r.signer_id);
    if (!id_known(app, r.record_id) && s && r.nonce >= s->nonce) {
        for (size_t i = 0; i < app->pool.count;) {
            PoolEntry *x = &app->pool.items[i];
            if (strcmp(x->rec.signer_id, r.signer_id) == 0 && x->rec.nonce > r.nonce) {
                printf("  Also dropped %s (%s, nonce %ld): it depended on the rejected nonce; resubmit it.\n",
                       x->rec.record_id, action_name(x->rec.action), x->rec.nonce);
                pool_remove_seq(&app->pool, x->seq);
            } else {
                i++;
            }
        }
    }
    lending_prune_pool(app);
    return 1;
}
