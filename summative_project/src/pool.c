#include "pool.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *STATUS_NAMES[] = { "PENDING", "CONFIRMED", "SUSPICIOUS" };

const char *pool_status_name(PoolStatus s) {
    return (s >= PS_PENDING && s <= PS_SUSPICIOUS) ? STATUS_NAMES[s] : "?";
}

void pool_init(Pool *p) {
    p->items = NULL;
    p->count = p->cap = 0;
    p->next_seq = 1;
}

void pool_free(Pool *p) {
    free(p->items);
    pool_init(p);
}

PoolEntry *pool_add(Pool *p, const Record *r, int priority, time_t sort_date,
                    PoolStatus status, const char *flag) {
    if (p->count == p->cap) {
        size_t cap = p->cap ? p->cap * 2 : 16;
        PoolEntry *grown = realloc(p->items, cap * sizeof(PoolEntry));
        if (!grown) return NULL;
        p->items = grown;
        p->cap = cap;
    }
    PoolEntry *e = &p->items[p->count++];
    memset(e, 0, sizeof(*e));
    e->rec = *r;
    e->seq = p->next_seq++;
    e->priority = priority;
    e->sort_date = sort_date;
    e->status = status;
    snprintf(e->flag, FLAG_LEN, "%s", flag ? flag : "");
    return e;
}

PoolEntry *pool_find(Pool *p, const char *request_id) {
    for (size_t i = 0; i < p->count; i++)
        if (strcmp(p->items[i].rec.record_id, request_id) == 0) return &p->items[i];
    return NULL;
}

PoolEntry *pool_find_status(Pool *p, const char *request_id, PoolStatus status) {
    for (size_t i = 0; i < p->count; i++)
        if (p->items[i].status == status && strcmp(p->items[i].rec.record_id, request_id) == 0)
            return &p->items[i];
    return NULL;
}

void pool_remove_seq(Pool *p, unsigned long seq) {
    for (size_t i = 0; i < p->count; i++) {
        if (p->items[i].seq != seq) continue;
        memmove(&p->items[i], &p->items[i + 1], (p->count - i - 1) * sizeof(PoolEntry));
        p->count--;
        return;
    }
}

/* qsort has no context argument, so the pool being sorted is held here. */
static const Pool *sort_pool;

static int by_priority(const void *a, const void *b) {
    const PoolEntry *x = &sort_pool->items[*(const size_t *)a];
    const PoolEntry *y = &sort_pool->items[*(const size_t *)b];
    if (x->priority != y->priority) return x->priority - y->priority;
    if (x->sort_date != y->sort_date) return x->sort_date < y->sort_date ? -1 : 1;
    return x->seq < y->seq ? -1 : (x->seq > y->seq);
}

void pool_priority_order(const Pool *p, size_t *order) {
    for (size_t i = 0; i < p->count; i++) order[i] = i;
    sort_pool = p;
    qsort(order, p->count, sizeof(size_t), by_priority);
}

static const Pool *seq_pool;
static int by_seq(const void *a, const void *b) {
    unsigned long x = seq_pool->items[*(const size_t *)a].seq, y = seq_pool->items[*(const size_t *)b].seq;
    return x < y ? -1 : (x > y);
}

void pool_project(const Pool *p, const LibState *confirmed, const Registry *reg,
                  LibState *out, char (*invalid)[RECORD_ID_LEN], size_t *n_invalid) {
    memcpy(out, confirmed, sizeof(LibState));
    if (n_invalid) *n_invalid = 0;
    if (p->count == 0) return;
    size_t *order = malloc(p->count * sizeof(size_t));
    if (!order) return;
    for (size_t i = 0; i < p->count; i++) order[i] = i;
    seq_pool = p;
    qsort(order, p->count, sizeof(size_t), by_seq);
    char err[256];
    for (size_t k = 0; k < p->count; k++) {
        const Record *r = &p->items[order[k]].rec;
        if (!state_apply(out, reg, r, err, sizeof(err), NULL) && invalid && n_invalid) {
            snprintf(invalid[*n_invalid], RECORD_ID_LEN, "%s", r->record_id);
            (*n_invalid)++;
        }
    }
    free(order);
}

/* ---- Persistence ---------------------------------------------------------------- */

int pool_save(const Pool *p, const char *path) {
    char tmp[256];
    snprintf(tmp, sizeof(tmp), "%s.tmp", path);
    FILE *f = fopen(tmp, "w");
    if (!f) return 0;
    fprintf(f, "# pending pool v1: P|seq|priority|sort_date|status|flag|<record>\n");
    char line[RECORD_LINE_LEN];
    for (size_t i = 0; i < p->count; i++) {
        const PoolEntry *e = &p->items[i];
        record_to_line(&e->rec, line, sizeof(line));
        fprintf(f, "P|%lu|%d|%ld|%s|%s|%s\n", e->seq, e->priority, (long)e->sort_date,
                pool_status_name(e->status), e->flag, line);
    }
    int ok = (fflush(f) == 0);
    fclose(f);
    return ok && rename(tmp, path) == 0;
}

int pool_load(Pool *p, const char *path) {
    FILE *f = fopen(path, "r");
    if (!f) return 0;
    char line[RECORD_LINE_LEN + 256];
    int ok = 1;
    while (fgets(line, sizeof(line), f)) {
        line[strcspn(line, "\r\n")] = '\0';
        if (!line[0] || line[0] == '#') continue;
        /* P | seq | priority | sort_date | status | flag | record... */
        char *field[6];
        char *cur = line;
        int n = 0;
        while (n < 6) {
            char *bar = strchr(cur, '|');
            if (!bar) break;
            *bar = '\0';
            field[n++] = cur;
            cur = bar + 1;
        }
        long seq, prio, date;
        Record r;
        PoolStatus st;
        if (n != 6 || strcmp(field[0], "P") != 0 || !parse_long(field[1], &seq) ||
            !parse_long(field[2], &prio) || !parse_long(field[3], &date) || !record_from_line(cur, &r)) {
            ok = 0;
            break;
        }
        if (strcmp(field[4], "PENDING") == 0) st = PS_PENDING;
        else if (strcmp(field[4], "SUSPICIOUS") == 0) st = PS_SUSPICIOUS;
        else { ok = 0; break; }
        PoolEntry *e = pool_add(p, &r, (int)prio, (time_t)date, st, field[5]);
        if (!e) { ok = 0; break; }
        e->seq = (unsigned long)seq;
        if ((unsigned long)seq >= p->next_seq) p->next_seq = (unsigned long)seq + 1;
    }
    fclose(f);
    if (!ok) { pool_free(p); return -1; }
    return 1;
}
