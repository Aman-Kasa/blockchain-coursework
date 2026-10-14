#ifndef POOL_H
#define POOL_H

#include <stddef.h>
#include <time.h>

#include "record.h"
#include "state.h"

/* Pending pool. Every lending action and token transaction waits here, as a
 * fully signed record, until a miner includes it in a block. */

typedef enum { PS_PENDING = 0, PS_CONFIRMED, PS_SUSPICIOUS } PoolStatus;

#define FLAG_LEN 120

typedef struct {
    Record rec;                 /* rec.record_id is the request_id               */
    unsigned long seq;          /* submission order                              */
    int priority;               /* 0 = overdue-related, 1 = everything else      */
    time_t sort_date;           /* reservation date (or submission time)         */
    PoolStatus status;
    char flag[FLAG_LEN];        /* why it was flagged SUSPICIOUS (kept for audit) */
} PoolEntry;

typedef struct {
    PoolEntry *items;
    size_t count;
    size_t cap;
    unsigned long next_seq;
} Pool;

void pool_init(Pool *p);
void pool_free(Pool *p);

PoolEntry *pool_add(Pool *p, const Record *r, int priority, time_t sort_date,
                    PoolStatus status, const char *flag);
PoolEntry *pool_find(Pool *p, const char *request_id);
/* A replayed request shares its request_id with the original, so the review
 * commands look entries up by ID *and* status, and removal is by seq. */
PoolEntry *pool_find_status(Pool *p, const char *request_id, PoolStatus status);
void pool_remove_seq(Pool *p, unsigned long seq);

/* Fills `order` with indices into p->items sorted by the brief's rules:
 * priority first (overdue-related requests first), then sort_date ascending
 * (the member who has waited longest first), then submission order. */
void pool_priority_order(const Pool *p, size_t *order);

/* The pending view of the library: confirmed state with every pool entry
 * (pending or suspicious) applied in submission order. Used for the
 * expected next nonce and to validate new requests. Entries that no longer
 * apply are skipped (their IDs are optionally reported via `invalid`). */
void pool_project(const Pool *p, const LibState *confirmed, const Registry *reg,
                  LibState *out, char (*invalid)[RECORD_ID_LEN], size_t *n_invalid);

const char *pool_status_name(PoolStatus s);

/* pool.dat: one line per entry. */
int pool_save(const Pool *p, const char *path);
int pool_load(Pool *p, const char *path);   /* 1 loaded, 0 missing, -1 corrupt */

#endif
