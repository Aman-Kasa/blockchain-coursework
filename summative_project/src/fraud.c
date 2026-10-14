#include "app.h"

#include <stdio.h>
#include <string.h>

/* Misuse and fraud heuristics.
 *
 * Run on every new request against the projected state (confirmed state plus
 * everything already pending), before the request itself is applied. A match
 * does not reject the request: it enters the pool with status SUSPICIOUS,
 * shows up in fraud_review, and is never mined until a librarian runs
 * approve_suspicious <id> (or discards it with reject_suspicious <id>). */

int fraud_check(const App *app, const LibState *proj, const Record *r, char *why, size_t cap);

/* Where a request ID already exists: block index, -2 for the pool, -1 none. */
static int find_existing(const App *app, const char *id) {
    for (const Block *b = app->chain.head; b; b = b->next)
        for (int i = 0; i < b->record_count; i++)
            if (strcmp(b->records[i].record_id, id) == 0) return b->index;
    for (size_t i = 0; i < app->pool.count; i++)
        if (strcmp(app->pool.items[i].rec.record_id, id) == 0) return -2;
    return -1;
}

int fraud_check(const App *app, const LibState *proj, const Record *r, char *why, size_t cap) {
    /* 1. Duplicate request: the same request_id is already in the chain or
     *    the pool (a replayed, previously captured signed request). */
    if (r->record_id[0]) {
        int where = find_existing(app, r->record_id);
        if (where >= 0) {
            snprintf(why, cap, "duplicate request: %s is already confirmed in block %d", r->record_id, where);
            return 1;
        }
        if (where == -2) {
            snprintf(why, cap, "duplicate request: %s is already in the pending pool", r->record_id);
            return 1;
        }
    }

    if (r->action == ACT_BORROWED) {
        /* 2. Excessive concurrent borrows. */
        int held = state_holdings(proj, r->member_id);
        if (held >= MAX_CONCURRENT_BORROWS) {
            snprintf(why, cap, "excessive concurrent borrows: %s already holds %d copies (maximum %d)",
                     r->member_id, held, MAX_CONCURRENT_BORROWS);
            return 1;
        }
        /* 3. Rapid re-borrow: same title within 24 hours of returning it. */
        time_t last = state_last_return(proj, r->member_id, r->book_id);
        if (last && r->timestamp - last < REBORROW_WINDOW_SECONDS) {
            long mins = (long)(r->timestamp - last) / 60;
            snprintf(why, cap, "rapid re-borrow: %s returned %s %ldh %02ldm ago (within 24 hours)",
                     r->member_id, r->book_id, mins / 60, mins % 60);
            return 1;
        }
    }

    if (r->action == ACT_RETURNED) {
        /* 4. Condition manipulation: downgraded more than one level with no
         *    librarian override reason code. */
        const CopyState *c = state_copy_c(proj, r->copy_id);
        Condition after;
        if (c && condition_parse(r->condition, &after) && (int)after - (int)c->condition > 1 && !r->reason[0]) {
            snprintf(why, cap, "condition manipulation: %s downgraded %s -> %s (%d levels) without a "
                     "librarian reason code", r->copy_id, condition_name(c->condition), condition_name(after),
                     (int)after - (int)c->condition);
            return 1;
        }
    }
    return 0;
}
