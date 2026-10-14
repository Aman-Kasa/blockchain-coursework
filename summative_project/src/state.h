#ifndef STATE_H
#define STATE_H

#include <stddef.h>
#include <time.h>

#include "record.h"
#include "registry.h"

/* Library state derived from the chain: copy inventory, reservation queues,
 * signer nonces and fines. It is a plain struct of fixed arrays, so it can be
 * copied with memcpy to build a "projected" state (confirmed state + pending
 * requests) when validating a new request or choosing records for a block. */

typedef enum { ST_AVAILABLE = 0, ST_BORROWED, ST_RESERVED, ST_LOST } CopyStatus;

typedef struct {
    char       copy_id[COPY_ID_LEN];
    char       book_id[ID_LEN];
    Condition  condition;
    CopyStatus status;
    char       holder[ID_LEN];      /* borrower, or the member a RESERVED copy is held for */
    time_t     due_date;            /* BORROWED only                                      */
    time_t     borrowed_at;
    int        renewals;            /* renewals used on the current loan                  */
    int        overdue_flagged;     /* an OVERDUE record exists for the current loan      */
    time_t     reserved_since;      /* RESERVED: when the holder joined the queue         */
} CopyState;

typedef struct {
    char   member_id[ID_LEN];
    time_t reservation_date;
} QueueEntry;

/* First-in-first-out reservation queue for one title. */
typedef struct {
    char       book_id[ID_LEN];
    QueueEntry q[MAX_QUEUE];
    int        count;
} BookQueue;

typedef struct {
    char id[ID_LEN];                /* member ID, or LIBRARIAN                            */
    long nonce;                     /* confirmed nonce: next request must carry this      */
    long fines_owed;                /* total fines from confirmed late returns            */
    long fines_paid;
} SignerState;

typedef struct {
    char   member_id[ID_LEN];
    char   book_id[ID_LEN];
    time_t when;
} ReturnEvent;

typedef struct {
    CopyState   copies[MAX_COPIES];
    int         ncopies;
    BookQueue   queues[MAX_BOOKS];
    int         nqueues;
    SignerState signers[MAX_MEMBERS + 1];
    int         nsigners;
    ReturnEvent returns[MAX_RETURN_EVENTS];   /* for the rapid re-borrow heuristic */
    int         nreturns;
} LibState;

/* Fresh state: every registered copy AVAILABLE, empty queues, nonces 0. */
void state_init(LibState *s, const Registry *reg);
/* Adds copies, titles and members registered since the state was built. */
void state_sync_registry(LibState *s, const Registry *reg);

/* Validates `r` against the state and, if it is a legal change, applies it
 * (including incrementing the signer's nonce). Returns 1 on success; on
 * failure leaves `s` unchanged and writes the reason to err. The checks use
 * the record's own timestamp, so replaying the chain is deterministic.
 * `offer` (optional) receives the member a returned copy was offered to. */
int state_apply(LibState *s, const Registry *reg, const Record *r,
                char *err, size_t errcap, char offer[ID_LEN]);

CopyState *state_copy(LibState *s, const char *copy_id);
const CopyState *state_copy_c(const LibState *s, const char *copy_id);
BookQueue *state_queue(LibState *s, const char *book_id);
const BookQueue *state_queue_c(const LibState *s, const char *book_id);
SignerState *state_signer(LibState *s, const char *id);
const SignerState *state_signer_c(const LibState *s, const char *id);

int state_holdings(const LibState *s, const char *member_id);            /* copies BORROWED */
int state_available_copies(const LibState *s, const char *book_id);
/* Most recent return of `book_id` by `member_id`, or 0 if none. */
time_t state_last_return(const LibState *s, const char *member_id, const char *book_id);

const char *copy_status_name(CopyStatus st);

#endif
