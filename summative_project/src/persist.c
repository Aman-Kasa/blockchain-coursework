#include "persist.h"
#include "lending.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define LINE_LEN 1600

/* ===================================================================== */
/* Text form of the derived state                                         */
/* ===================================================================== */

/* Inventory, queues, nonces/fines and return history. */
static void write_state_lines(const LibState *s, FILE *f) {
    for (int i = 0; i < s->ncopies; i++) {
        const CopyState *c = &s->copies[i];
        fprintf(f, "COPY|%s|%s|%s|%s|%s|%ld|%ld|%d|%d|%ld\n", c->copy_id, c->book_id, condition_name(c->condition),
                copy_status_name(c->status), c->holder, (long)c->due_date, (long)c->borrowed_at, c->renewals,
                c->overdue_flagged, (long)c->reserved_since);
    }
    for (int i = 0; i < s->nqueues; i++)
        for (int k = 0; k < s->queues[i].count; k++)
            fprintf(f, "QUEUE|%s|%s|%ld\n", s->queues[i].book_id, s->queues[i].q[k].member_id,
                    (long)s->queues[i].q[k].reservation_date);
    for (int i = 0; i < s->nsigners; i++)
        fprintf(f, "SIGNER|%s|%ld|%ld|%ld\n", s->signers[i].id, s->signers[i].nonce, s->signers[i].fines_owed,
                s->signers[i].fines_paid);
    for (int i = 0; i < s->nreturns; i++)
        fprintf(f, "RETURN|%s|%s|%ld\n", s->returns[i].member_id, s->returns[i].book_id, (long)s->returns[i].when);
}

static void write_ledger_lines(const Ledger *led, FILE *f) {
    if (led->model == MODEL_UTXO) {
        for (const Utxo *u = led->utxo.head; u; u = u->next)
            fprintf(f, "UTXO|%d|%s|%d|%s|%ld|%d|%s\n", u->id, u->txid, u->vout, u->owner, u->amount, u->spent, u->spent_by);
    } else {
        for (size_t i = 0; i < led->acc.count; i++) {
            const Account *a = &led->acc.accounts[i];
            fprintf(f, "ACCOUNT|%s|%ld|%ld|%d\n", a->member_id, a->balance, a->nonce, a->outgoing);
        }
    }
}

/* Renders lines into a heap string (caller frees). */
static char *render(void (*fn)(const void *, FILE *), const void *arg) {
    char *buf = NULL;
    size_t len = 0;
    FILE *f = open_memstream(&buf, &len);
    if (!f) return NULL;
    fn(arg, f);
    fclose(f);
    return buf;
}
static void state_fn(const void *s, FILE *f) { write_state_lines(s, f); }
static void ledger_fn(const void *l, FILE *f) { write_ledger_lines(l, f); }

/* ===================================================================== */
/* Validation                                                             */
/* ===================================================================== */

FullCheck persist_validate(const App *app, LibState *rebuilt) {
    FullCheck out = { .valid = 1, .bad_index = -1, .reason = "" };
    ChainCheck c = chain_validate(&app->chain, &app->reg);
    if (!c.valid) {
        out.valid = 0;
        out.bad_index = c.bad_index;
        snprintf(out.reason, sizeof(out.reason), "%s", c.reason);
        return out;
    }
    /* Replay every record through the state machine: each must be a legal
     * change (right nonce, right signer, valid state transition, correct
     * fine and reward). */
    LibState *s = malloc(sizeof(LibState));
    if (!s) { out.valid = 0; snprintf(out.reason, sizeof(out.reason), "out of memory"); return out; }
    state_init(s, &app->reg);
    char err[256];
    for (const Block *b = app->chain.head; b && out.valid; b = b->next) {
        for (int i = 0; i < b->record_count; i++) {
            if (!state_apply(s, &app->reg, &b->records[i], err, sizeof(err), NULL)) {
                out.valid = 0;
                out.bad_index = b->index;
                snprintf(out.reason, sizeof(out.reason), "record %s is not a legal state change: %s",
                         b->records[i].record_id, err);
                break;
            }
        }
    }
    if (out.valid && rebuilt) memcpy(rebuilt, s, sizeof(LibState));
    free(s);
    return out;
}

/* ===================================================================== */
/* Save                                                                   */
/* ===================================================================== */

int persist_save(const App *app) {
    if (app->read_only) return 0;
    if (!chain_save(&app->chain, CHAIN_PATH) || !pool_save(&app->pool, POOL_PATH)) return 0;

    char tmp[64];
    snprintf(tmp, sizeof(tmp), "%s.tmp", STATE_PATH);
    FILE *f = fopen(tmp, "w");
    if (!f) return 0;
    fprintf(f, "# library state v1 (derived from the chain; checked against it on every start)\n");
    fprintf(f, "META|%d|%d|%d|%ld|%s|%zu|%s\n", app->difficulty, app->batch_size, app->block_reward,
            sim_get_offset(), app->ledger.model == MODEL_UTXO ? "utxo" : "account", app->chain.length,
            app->chain.tail ? app->chain.tail->hash : "");
    write_state_lines(app->state, f);
    write_ledger_lines(&app->ledger, f);
    for (int i = 0; i < app->nminers; i++)
        fprintf(f, "MINER|%s|%.4f|%d\n", app->miners[i].id, app->miners[i].earned, app->miners[i].blocks);
    int ok = (fflush(f) == 0);
    fclose(f);
    return ok && rename(tmp, STATE_PATH) == 0;
}

/* ===================================================================== */
/* Load                                                                   */
/* ===================================================================== */

typedef struct {
    int    have_meta;
    long   difficulty, batch, reward, offset;
    char   model[16];
    char  *state_text;    /* the saved COPY/QUEUE/SIGNER/RETURN lines   */
    char  *ledger_text;   /* the saved UTXO or ACCOUNT lines            */
} Saved;

static void append(char **buf, size_t *len, const char *line) {
    size_t n = strlen(line);
    char *grown = realloc(*buf, *len + n + 2);
    if (!grown) return;
    *buf = grown;
    memcpy(*buf + *len, line, n);
    (*buf)[*len + n] = '\n';
    *len += n + 1;
    (*buf)[*len] = '\0';
}

static void read_saved(App *app, Saved *sv) {
    memset(sv, 0, sizeof(*sv));
    FILE *f = fopen(STATE_PATH, "r");
    if (!f) return;
    char line[LINE_LEN];
    size_t slen = 0, llen = 0;
    sv->state_text = calloc(1, 1);
    sv->ledger_text = calloc(1, 1);
    while (fgets(line, sizeof(line), f)) {
        line[strcspn(line, "\r\n")] = '\0';
        if (!strncmp(line, "META|", 5)) {
            char model[16] = "";
            if (sscanf(line, "META|%ld|%ld|%ld|%ld|%15[a-z]", &sv->difficulty, &sv->batch, &sv->reward,
                       &sv->offset, model) == 5) {
                sv->have_meta = 1;
                snprintf(sv->model, sizeof(sv->model), "%s", model);
            }
        } else if (!strncmp(line, "COPY|", 5) || !strncmp(line, "QUEUE|", 6) ||
                   !strncmp(line, "SIGNER|", 7) || !strncmp(line, "RETURN|", 7)) {
            append(&sv->state_text, &slen, line);
        } else if (!strncmp(line, "UTXO|", 5) || !strncmp(line, "ACCOUNT|", 8)) {
            append(&sv->ledger_text, &llen, line);
        } else if (!strncmp(line, "MINER|", 6) && app->nminers < MAX_MINERS) {
            MinerAccount *m = &app->miners[app->nminers];
            char id[MINER_ID_LEN];
            if (sscanf(line, "MINER|%31[^|]|%lf|%d", id, &m->earned, &m->blocks) == 3) {
                snprintf(m->id, MINER_ID_LEN, "%s", id);
                app->nminers++;
            }
        }
    }
    fclose(f);
}

/* Drops pool entries whose signature does not verify against the signer's
 * registered public key (pool.dat edited outside the program). */
static void check_pool_signatures(App *app) {
    for (size_t i = 0; i < app->pool.count;) {
        const Record *r = &app->pool.items[i].rec;
        const char *registered = registry_pubkey(&app->reg, r->signer_id);
        if (record_verify(r) && registered && strcmp(registered, r->pubkey) == 0) { i++; continue; }
        printf("WARNING: pending request %s in %s has an invalid ECDSA signature: discarded.\n", r->record_id, POOL_PATH);
        pool_remove_seq(&app->pool, app->pool.items[i].seq);
    }
}

int persist_load(App *app, LedgerModel model, int cli_difficulty, int cli_batch) {
    Saved sv;
    read_saved(app, &sv);
    app->difficulty = DEFAULT_DIFFICULTY;
    app->batch_size = DEFAULT_BATCH_SIZE;
    app->block_reward = DEFAULT_BLOCK_REWARD;
    if (sv.have_meta) {
        if (sv.difficulty >= MIN_DIFFICULTY && sv.difficulty <= MAX_DIFFICULTY) app->difficulty = (int)sv.difficulty;
        if (sv.batch >= MIN_BATCH_SIZE && sv.batch <= MAX_BATCH_SIZE) app->batch_size = (int)sv.batch;
        if (sv.reward > 0) app->block_reward = (int)sv.reward;
        sim_set_offset(sv.offset);
    }
    if (cli_difficulty > 0) app->difficulty = cli_difficulty;
    if (cli_batch > 0) app->batch_size = cli_batch;

    chain_init(&app->chain);
    pool_init(&app->pool);
    ledger_init(&app->ledger, model);
    app->state = malloc(sizeof(LibState));
    if (!app->state) { free(sv.state_text); free(sv.ledger_text); return 0; }
    state_init(app->state, &app->reg);

    int loaded = chain_load(&app->chain, CHAIN_PATH);
    if (loaded < 0) {
        fprintf(stderr, "FATAL: %s cannot be parsed. Restore it, or delete the data/*.dat files to start a new chain.\n", CHAIN_PATH);
        free(sv.state_text); free(sv.ledger_text);
        return 0;
    }
    if (loaded == 0) {
        printf("No saved chain at %s: starting a new chain.\n", CHAIN_PATH);
        /* Leftovers from an older chain would not match the new one. */
        remove(POOL_PATH);
        remove(STATE_PATH);
        app->nminers = 0;
        sim_set_offset(0);
        if (!chain_create_genesis(&app->chain, app->difficulty)) { free(sv.state_text); free(sv.ledger_text); return 0; }
        persist_save(app);
        free(sv.state_text); free(sv.ledger_text);
        return 1;
    }

    /* The loaded chain must be re-validated before anything else is allowed. */
    printf("Loaded %zu block(s) from %s; re-validating the whole chain...\n", app->chain.length, CHAIN_PATH);
    LibState *rebuilt = malloc(sizeof(LibState));
    if (!rebuilt) { free(sv.state_text); free(sv.ledger_text); return 0; }
    FullCheck v = persist_validate(app, rebuilt);
    if (!v.valid) {
        app->read_only = 1;
        fprintf(stderr, "WARNING: chain validation FAILED at block %d: %s\n", v.bad_index, v.reason);
        fprintf(stderr, "Read-only session: lending, mining and transfers are disabled. Use validate_chain,\n"
                        "chain_view and merkle_view to inspect the damage.\n");
        char err[256];
        for (const Block *b = app->chain.head; b; b = b->next)
            for (int i = 0; i < b->record_count; i++) state_apply(app->state, &app->reg, &b->records[i], err, sizeof(err), NULL);
    } else {
        printf("Chain valid: links, timestamps, Merkle roots, ECDSA signatures, proof of work and every\n"
               "state transition check out.\n");
        memcpy(app->state, rebuilt, sizeof(LibState));
        char *derived = render(state_fn, app->state);
        if (sv.state_text && sv.state_text[0] && derived) {
            if (strcmp(derived, sv.state_text) == 0)
                printf("Saved inventory, reservation queues, nonces and fines match the chain.\n");
            else
                printf("WARNING: %s does not match the chain (edited outside the program?). Using the state "
                       "rebuilt from the chain.\n", STATE_PATH);
        }
        free(derived);
    }
    free(rebuilt);

    ledger_rebuild(&app->ledger, &app->chain);
    if (!app->read_only && sv.ledger_text && sv.ledger_text[0] && strcmp(sv.model, model == MODEL_UTXO ? "utxo" : "account") == 0) {
        char *derived = render(ledger_fn, &app->ledger);
        if (derived && strcmp(derived, sv.ledger_text) == 0)
            printf("Saved %s balances match the chain.\n", ledger_model_name(model));
        else
            printf("WARNING: saved token balances do not match the chain; using balances rebuilt from the chain.\n");
        free(derived);
    }
    free(sv.state_text);
    free(sv.ledger_text);

    if (pool_load(&app->pool, POOL_PATH) < 0) {
        printf("WARNING: %s is corrupt; starting with an empty pending pool.\n", POOL_PATH);
        pool_init(&app->pool);
    }
    check_pool_signatures(app);
    if (!app->read_only) lending_prune_pool(app);
    if (app->pool.count) printf("Pending pool restored: %zu request(s).\n", app->pool.count);
    return 1;
}
