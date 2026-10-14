#include "state.h"

#include <stdio.h>
#include <string.h>

static const char *STATUS_NAMES[] = { "AVAILABLE", "BORROWED", "RESERVED", "LOST" };

const char *copy_status_name(CopyStatus st) {
    return (st >= ST_AVAILABLE && st <= ST_LOST) ? STATUS_NAMES[st] : "?";
}

/* ---- Lookups ---------------------------------------------------------------- */

CopyState *state_copy(LibState *s, const char *copy_id) {
    for (int i = 0; i < s->ncopies; i++)
        if (strcmp(s->copies[i].copy_id, copy_id) == 0) return &s->copies[i];
    return NULL;
}
const CopyState *state_copy_c(const LibState *s, const char *copy_id) {
    return state_copy((LibState *)s, copy_id);
}

BookQueue *state_queue(LibState *s, const char *book_id) {
    for (int i = 0; i < s->nqueues; i++)
        if (strcmp(s->queues[i].book_id, book_id) == 0) return &s->queues[i];
    return NULL;
}
const BookQueue *state_queue_c(const LibState *s, const char *book_id) {
    return state_queue((LibState *)s, book_id);
}

SignerState *state_signer(LibState *s, const char *id) {
    for (int i = 0; i < s->nsigners; i++)
        if (strcmp(s->signers[i].id, id) == 0) return &s->signers[i];
    return NULL;
}
const SignerState *state_signer_c(const LibState *s, const char *id) {
    return state_signer((LibState *)s, id);
}

int state_holdings(const LibState *s, const char *member_id) {
    int n = 0;
    for (int i = 0; i < s->ncopies; i++)
        if (s->copies[i].status == ST_BORROWED && strcmp(s->copies[i].holder, member_id) == 0) n++;
    return n;
}

int state_available_copies(const LibState *s, const char *book_id) {
    int n = 0;
    for (int i = 0; i < s->ncopies; i++)
        if (s->copies[i].status == ST_AVAILABLE && strcmp(s->copies[i].book_id, book_id) == 0) n++;
    return n;
}

time_t state_last_return(const LibState *s, const char *member_id, const char *book_id) {
    time_t last = 0;
    for (int i = 0; i < s->nreturns; i++)
        if (strcmp(s->returns[i].member_id, member_id) == 0 &&
            strcmp(s->returns[i].book_id, book_id) == 0 && s->returns[i].when > last)
            last = s->returns[i].when;
    return last;
}

/* ---- Construction ------------------------------------------------------------- */

void state_init(LibState *s, const Registry *reg) {
    memset(s, 0, sizeof(*s));
    SignerState *lib = &s->signers[s->nsigners++];
    snprintf(lib->id, ID_LEN, "%s", LIBRARIAN_ID);
    state_sync_registry(s, reg);
}

void state_sync_registry(LibState *s, const Registry *reg) {
    for (size_t i = 0; i < reg->copy_count; i++) {
        if (state_copy(s, reg->copies[i].copy_id) || s->ncopies >= MAX_COPIES) continue;
        CopyState *c = &s->copies[s->ncopies++];
        memset(c, 0, sizeof(*c));
        snprintf(c->copy_id, COPY_ID_LEN, "%s", reg->copies[i].copy_id);
        snprintf(c->book_id, ID_LEN, "%s", reg->copies[i].book_id);
        c->condition = reg->copies[i].condition;
        c->status = ST_AVAILABLE;
    }
    for (size_t i = 0; i < reg->book_count; i++) {
        if (state_queue(s, reg->books[i].book_id) || s->nqueues >= MAX_BOOKS) continue;
        BookQueue *q = &s->queues[s->nqueues++];
        memset(q, 0, sizeof(*q));
        snprintf(q->book_id, ID_LEN, "%s", reg->books[i].book_id);
    }
    for (size_t i = 0; i < reg->member_count; i++) {
        if (state_signer(s, reg->members[i].member_id) || s->nsigners >= MAX_MEMBERS + 1) continue;
        SignerState *m = &s->signers[s->nsigners++];
        memset(m, 0, sizeof(*m));
        snprintf(m->id, ID_LEN, "%s", reg->members[i].member_id);
    }
}

/* ---- Queue helpers ------------------------------------------------------------ */

static int queue_position(const BookQueue *q, const char *member_id) {
    for (int i = 0; i < q->count; i++)
        if (strcmp(q->q[i].member_id, member_id) == 0) return i;
    return -1;
}

static void queue_remove_at(BookQueue *q, int pos) {
    memmove(&q->q[pos], &q->q[pos + 1], (size_t)(q->count - pos - 1) * sizeof(QueueEntry));
    q->count--;
}

/* ---- The state machine ---------------------------------------------------------- */

#define FAIL(...) do { snprintf(err, errcap, __VA_ARGS__); return 0; } while (0)

/* Checks shared by every record: the signer, the nonce, the registry data
 * copied into the record, and who is allowed to sign which action. */
static int check_common(const LibState *s, const Registry *reg, const Record *r,
                        char *err, size_t errcap) {
    const SignerState *signer = state_signer_c(s, r->signer_id);
    if (!signer) FAIL("unknown signer %s", r->signer_id);
    if (r->nonce != signer->nonce)
        FAIL("nonce %ld for %s is not the expected next nonce %ld (replayed or out-of-order request)",
             r->nonce, r->signer_id, signer->nonce);

    int librarian_action = (r->action == ACT_OVERDUE || r->action == ACT_LOST);
    if (librarian_action && strcmp(r->signer_id, LIBRARIAN_ID) != 0)
        FAIL("%s records must be signed by the librarian", action_name(r->action));
    if (!librarian_action && strcmp(r->signer_id, r->member_id) != 0)
        FAIL("a %s request must be signed by the member it is for", action_name(r->action));

    if (r->member_id[0]) {
        const Member *m = registry_member(reg, r->member_id);
        if (!m) FAIL("Book or Member not found (member %s)", r->member_id);
        if (strcmp(m->full_name, r->member_name) != 0) FAIL("member_name does not match the registry");
    }
    if (r->copy_id[0]) {
        const CopyInfo *ci = registry_copy(reg, r->copy_id);
        const Book *b = ci ? registry_book(reg, ci->book_id) : NULL;
        if (!ci || !b) FAIL("Book or Member not found (copy %s)", r->copy_id);
        if (strcmp(ci->book_id, r->book_id) != 0 || strcmp(b->title, r->book_title) != 0)
            FAIL("book_id / book_title do not match the registry for copy %s", r->copy_id);
    } else if (r->book_id[0]) {
        const Book *b = registry_book(reg, r->book_id);
        if (!b) FAIL("Book or Member not found (book %s)", r->book_id);
        if (strcmp(b->title, r->book_title) != 0) FAIL("book_title does not match the registry");
    }
    if (r->fine_amount < 0 || r->amount < 0 || r->token_reward < 0) FAIL("amounts must not be negative");
    return 1;
}

static int apply_borrow(LibState *s, const Record *r, char *err, size_t errcap) {
    CopyState *c = state_copy(s, r->copy_id);
    BookQueue *q = state_queue(s, r->book_id);
    if (!c || !q) FAIL("copy %s not in inventory", r->copy_id);
    if (c->status == ST_RESERVED) {
        if (strcmp(c->holder, r->member_id) != 0)
            FAIL("copy %s is held for %s (reservation)", c->copy_id, c->holder);
    } else if (c->status == ST_AVAILABLE) {
        /* Members already waiting in the queue are served first. */
        if (q->count > 0 && strcmp(q->q[0].member_id, r->member_id) != 0)
            FAIL("%s has a reservation queue; %s is next in line", r->book_id, q->q[0].member_id);
    } else {
        FAIL("copy %s is %s, not AVAILABLE (AVAILABLE -> BORROWED is the only way to borrow)",
             c->copy_id, copy_status_name(c->status));
    }
    if (r->due_date != r->timestamp + (time_t)LOAN_DAYS * SECONDS_PER_DAY)
        FAIL("due_date must be %d days after the borrow", LOAN_DAYS);

    int pos = queue_position(q, r->member_id);
    if (pos >= 0) queue_remove_at(q, pos);   /* served: leaves the queue */

    c->status = ST_BORROWED;
    snprintf(c->holder, ID_LEN, "%s", r->member_id);
    c->due_date = r->due_date;
    c->borrowed_at = r->timestamp;
    c->renewals = 0;
    c->overdue_flagged = 0;
    c->reserved_since = 0;
    return 1;
}

static int apply_return(LibState *s, const Record *r, char *err, size_t errcap, char offer[ID_LEN]) {
    CopyState *c = state_copy(s, r->copy_id);
    BookQueue *q = state_queue(s, r->book_id);
    if (!c || !q) FAIL("copy %s not in inventory", r->copy_id);
    if (c->status != ST_BORROWED || strcmp(c->holder, r->member_id) != 0)
        FAIL("copy %s has no open BORROWED record for %s (never borrowed or already returned)",
             r->copy_id, r->member_id);
    Condition cond;
    if (!condition_parse(r->condition, &cond) || cond == COND_LOST)
        FAIL("return condition must be NEW, GOOD or WORN (use mark_lost for lost copies)");

    long late = days_late(c->due_date, r->timestamp);
    long fine = late * FINE_PER_DAY;
    int reward = late > 0 ? REWARD_LATE : REWARD_ON_TIME;
    if (r->fine_amount != fine) FAIL("fine_amount must be %ld (%ld day(s) late)", fine, late);
    if (r->token_reward != reward) FAIL("token_reward must be %d", reward);

    SignerState *m = state_signer(s, r->member_id);
    m->fines_owed += fine;
    if (s->nreturns < MAX_RETURN_EVENTS) {
        ReturnEvent *e = &s->returns[s->nreturns++];
        snprintf(e->member_id, ID_LEN, "%s", r->member_id);
        snprintf(e->book_id, ID_LEN, "%s", r->book_id);
        e->when = r->timestamp;
    }

    c->condition = cond;
    c->due_date = 0;
    c->renewals = 0;
    c->overdue_flagged = 0;
    /* BORROWED -> RESERVED if someone is waiting (the copy is offered to the
     * head of the queue first), otherwise BORROWED -> AVAILABLE. */
    if (q->count > 0) {
        c->status = ST_RESERVED;
        snprintf(c->holder, ID_LEN, "%s", q->q[0].member_id);
        c->reserved_since = q->q[0].reservation_date;
        if (offer) snprintf(offer, ID_LEN, "%s", q->q[0].member_id);
        queue_remove_at(q, 0);
    } else {
        c->status = ST_AVAILABLE;
        c->holder[0] = '\0';
    }
    return 1;
}

static int apply_renew(LibState *s, const Record *r, char *err, size_t errcap) {
    CopyState *c = state_copy(s, r->copy_id);
    const BookQueue *q = state_queue_c(s, r->book_id);
    if (!c || !q) FAIL("copy %s not in inventory", r->copy_id);
    if (c->status != ST_BORROWED || strcmp(c->holder, r->member_id) != 0)
        FAIL("copy %s is not borrowed by %s", r->copy_id, r->member_id);
    if (days_late(c->due_date, r->timestamp) > 0) FAIL("copy %s is already overdue; return it instead", r->copy_id);
    if (c->renewals >= MAX_RENEWALS) FAIL("copy %s has already been renewed %d times (maximum)", r->copy_id, MAX_RENEWALS);
    for (int i = 0; i < q->count; i++)
        if (strcmp(q->q[i].member_id, r->member_id) != 0)
            FAIL("another member (%s) has reserved %s, so it cannot be renewed", q->q[i].member_id, r->book_id);
    if (r->due_date != c->due_date + (time_t)RENEW_DAYS * SECONDS_PER_DAY)
        FAIL("renewal must extend the due date by exactly %d days", RENEW_DAYS);
    c->due_date = r->due_date;
    c->renewals++;
    return 1;
}

static int apply_reserve(LibState *s, const Record *r, char *err, size_t errcap) {
    BookQueue *q = state_queue(s, r->book_id);
    if (!q) FAIL("Book or Member not found (book %s)", r->book_id);
    if (state_available_copies(s, r->book_id) > 0)
        FAIL("%s has an available copy; borrow it instead of reserving", r->book_id);
    if (queue_position(q, r->member_id) >= 0) FAIL("%s is already in the queue for %s", r->member_id, r->book_id);
    for (int i = 0; i < s->ncopies; i++) {
        const CopyState *c = &s->copies[i];
        if (strcmp(c->book_id, r->book_id) == 0 && strcmp(c->holder, r->member_id) == 0 &&
            (c->status == ST_BORROWED || c->status == ST_RESERVED))
            FAIL("%s already holds or has been offered a copy of %s", r->member_id, r->book_id);
    }
    if (q->count >= MAX_QUEUE) FAIL("the reservation queue for %s is full", r->book_id);
    QueueEntry *e = &q->q[q->count++];
    snprintf(e->member_id, ID_LEN, "%s", r->member_id);
    e->reservation_date = r->timestamp;
    return 1;
}

static int apply_overdue(LibState *s, const Record *r, char *err, size_t errcap) {
    CopyState *c = state_copy(s, r->copy_id);
    if (!c) FAIL("copy %s not in inventory", r->copy_id);
    if (c->status != ST_BORROWED || strcmp(c->holder, r->member_id) != 0)
        FAIL("copy %s is not borrowed by %s", r->copy_id, r->member_id);
    if (c->overdue_flagged) FAIL("copy %s is already flagged OVERDUE", r->copy_id);
    long late = days_late(c->due_date, r->timestamp);
    if (late <= 0) FAIL("copy %s is not past its due date", r->copy_id);
    if (r->fine_amount != late * FINE_PER_DAY) FAIL("OVERDUE fine_amount must be %ld", late * FINE_PER_DAY);
    c->overdue_flagged = 1;
    return 1;
}

static int apply_lost(LibState *s, const Record *r, char *err, size_t errcap) {
    CopyState *c = state_copy(s, r->copy_id);
    if (!c) FAIL("copy %s not in inventory", r->copy_id);
    if (c->status != ST_AVAILABLE && c->status != ST_BORROWED)
        FAIL("copy %s is %s; only AVAILABLE or BORROWED copies can be marked LOST", r->copy_id,
             copy_status_name(c->status));
    if (!r->reason[0]) FAIL("marking a copy LOST requires a librarian reason code");
    c->status = ST_LOST;
    c->condition = COND_LOST;
    c->holder[0] = '\0';
    c->due_date = 0;
    return 1;
}

static int apply_fine_paid(LibState *s, const Record *r, char *err, size_t errcap) {
    SignerState *m = state_signer(s, r->member_id);
    long outstanding = m->fines_owed - m->fines_paid;
    if (r->amount <= 0) FAIL("payment must be a positive amount");
    if (r->amount > outstanding) FAIL("%s owes %ld; cannot pay %ld", r->member_id, outstanding, r->amount);
    m->fines_paid += r->amount;
    return 1;
}

static int apply_transfer(LibState *s, const Registry *reg, const Record *r, char *err, size_t errcap) {
    (void)s;
    if (!registry_member(reg, r->counterparty)) FAIL("Book or Member not found (recipient %s)", r->counterparty);
    if (strcmp(r->counterparty, r->member_id) == 0) FAIL("sender and recipient must differ");
    if (r->amount <= 0) FAIL("transfer amount must be positive");
    return 1;   /* balances are checked and moved by the ledger */
}

int state_apply(LibState *s, const Registry *reg, const Record *r,
                char *err, size_t errcap, char offer[ID_LEN]) {
    if (offer) offer[0] = '\0';
    if (!check_common(s, reg, r, err, errcap)) return 0;

    /* Every apply_* function finishes all of its checks before it changes
     * anything, so a rejected record leaves `s` untouched. */
    int ok = 0;
    switch (r->action) {
        case ACT_BORROWED:  ok = apply_borrow(s, r, err, errcap); break;
        case ACT_RETURNED:  ok = apply_return(s, r, err, errcap, offer); break;
        case ACT_RENEWED:   ok = apply_renew(s, r, err, errcap); break;
        case ACT_RESERVED:  ok = apply_reserve(s, r, err, errcap); break;
        case ACT_OVERDUE:   ok = apply_overdue(s, r, err, errcap); break;
        case ACT_LOST:      ok = apply_lost(s, r, err, errcap); break;
        case ACT_FINE_PAID: ok = apply_fine_paid(s, r, err, errcap); break;
        case ACT_TRANSFER:  ok = apply_transfer(s, reg, r, err, errcap); break;
        default: snprintf(err, errcap, "unknown action"); ok = 0;
    }
    if (ok) state_signer(s, r->signer_id)->nonce++;   /* nonce advances only when applied */
    return ok;
}
