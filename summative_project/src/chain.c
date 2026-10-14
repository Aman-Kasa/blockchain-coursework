#include "chain.h"
#include "merkle.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void chain_init(Chain *c) {
    c->head = c->tail = NULL;
    c->length = 0;
}

void block_free(Block *b) {
    if (!b) return;
    free(b->records);
    free(b);
}

void chain_free(Chain *c) {
    Block *b = c->head;
    while (b) {
        Block *next = b->next;
        block_free(b);
        b = next;
    }
    chain_init(c);
}

static void zero_hash(char out[HASH_HEX_LEN]) {
    memset(out, '0', HASH_HEX_LEN - 1);
    out[HASH_HEX_LEN - 1] = '\0';
}

void block_merkle(const Block *b, char out[HASH_HEX_LEN]) {
    if (b->record_count == 0) { merkle_root(NULL, 0, out); return; }
    char (*leaves)[HASH_HEX_LEN] = malloc((size_t)b->record_count * HASH_HEX_LEN);
    if (!leaves) { out[0] = '\0'; return; }
    for (int i = 0; i < b->record_count; i++) record_hash(&b->records[i], leaves[i]);
    merkle_root(leaves, (size_t)b->record_count, out);
    free(leaves);
}

void block_reward_txid(const Block *b, int *reward, char out[HASH_HEX_LEN]) {
    /* The block's transaction_id is SHA-256 over the concatenated IDs of every
     * reward transaction in the block (empty when the block pays no reward). */
    char buf[64 * MAX_BATCH_SIZE + 1];
    size_t len = 0;
    *reward = 0;
    buf[0] = '\0';
    for (int i = 0; i < b->record_count; i++) {
        char tx[HASH_HEX_LEN];
        record_reward_txid(&b->records[i], tx);
        if (!tx[0]) continue;
        *reward += b->records[i].token_reward;
        if (len + 64 < sizeof(buf)) { memcpy(buf + len, tx, 64); len += 64; buf[len] = '\0'; }
    }
    if (len == 0) out[0] = '\0';
    else sha256_hex(buf, len, out);
}

void block_header_hash(const Block *b, char out[HASH_HEX_LEN]) {
    char buf[512];
    snprintf(buf, sizeof(buf), "%d|%ld|%d|%s|%s|%s|%d|%s|%d|%lu",
             b->index, (long)b->timestamp, b->record_count, b->previous_hash, b->merkle_root,
             b->sealed_by, b->token_reward, b->transaction_id, b->difficulty, b->nonce);
    sha256_str(buf, out);
}

int hash_meets_difficulty(const char *hash, int difficulty) {
    if (difficulty < MIN_DIFFICULTY || difficulty > MAX_DIFFICULTY) return 0;
    for (int i = 0; i < difficulty; i++)
        if (hash[i] != '0') return 0;
    return 1;
}

Block *block_prepare(const Chain *c, const Record *recs, int n, int difficulty,
                     const char *sealed_by, time_t now) {
    Block *b = calloc(1, sizeof(Block));
    if (!b) return NULL;
    if (n > 0) {
        b->records = malloc((size_t)n * sizeof(Record));
        if (!b->records) { free(b); return NULL; }
        memcpy(b->records, recs, (size_t)n * sizeof(Record));
    }
    b->record_count = n;
    b->index = (int)c->length;
    /* Timestamps must strictly increase along the chain. */
    b->timestamp = (c->tail && now <= c->tail->timestamp) ? c->tail->timestamp + 1 : now;
    if (c->tail) snprintf(b->previous_hash, HASH_HEX_LEN, "%s", c->tail->hash);
    else zero_hash(b->previous_hash);
    snprintf(b->sealed_by, SEALED_BY_LEN, "%s", sealed_by);
    b->difficulty = difficulty;
    block_merkle(b, b->merkle_root);
    block_reward_txid(b, &b->token_reward, b->transaction_id);
    return b;
}

int block_pow(Block *b, unsigned long max_attempts, unsigned long *attempts) {
    *attempts = 0;
    while (*attempts < max_attempts) {
        block_header_hash(b, b->hash);
        (*attempts)++;
        if (hash_meets_difficulty(b->hash, b->difficulty)) return 1;
        b->nonce++;
    }
    b->hash[0] = '\0';
    return 0;
}

void chain_append(Chain *c, Block *b) {
    b->next = NULL;
    if (c->tail) c->tail->next = b; else c->head = b;
    c->tail = b;
    c->length++;
}

int chain_create_genesis(Chain *c, int difficulty) {
    Block *b = block_prepare(c, NULL, 0, difficulty, "GENESIS", sim_now());
    if (!b) return 0;
    unsigned long attempts;
    if (!block_pow(b, 100000000UL, &attempts)) { block_free(b); return 0; }
    chain_append(c, b);
    printf("Genesis block mined at difficulty %d after %lu hash attempts.\n", difficulty, attempts);
    return 1;
}

const Block *chain_block(const Chain *c, int index) {
    for (const Block *b = c->head; b; b = b->next)
        if (b->index == index) return b;
    return NULL;
}

static ChainCheck fail(int index, const char *fmt, const char *detail) {
    ChainCheck r = { .valid = 0, .bad_index = index };
    snprintf(r.reason, sizeof(r.reason), fmt, detail ? detail : "");
    return r;
}

ChainCheck chain_validate(const Chain *c, const Registry *reg) {
    if (!c->head) return fail(-1, "chain is empty (no genesis block)%s", NULL);

    char expected_prev[HASH_HEX_LEN];
    zero_hash(expected_prev);
    time_t prev_ts = 0;
    int expected_index = 0;

    for (const Block *b = c->head; b; b = b->next) {
        if (b->index != expected_index) return fail(b->index, "block index out of sequence%s", NULL);
        if (strcmp(b->previous_hash, expected_prev) != 0)
            return fail(b->index, "previous_hash does not match the prior block's hash%s", NULL);
        if (b->index > 0 && b->timestamp <= prev_ts)
            return fail(b->index, "timestamp is not later than the previous block's%s", NULL);
        if (b->record_count < 0 || b->record_count > MAX_BATCH_SIZE || (b->record_count > 0 && !b->records))
            return fail(b->index, "record_count is invalid%s", NULL);

        for (int i = 0; i < b->record_count; i++) {
            const Record *r = &b->records[i];
            if (!record_verify(r))
                return fail(b->index, "record %s: ID or ECDSA signature is invalid", r->record_id);
            const char *registered = registry_pubkey(reg, r->signer_id);
            if (!registered || strcmp(registered, r->pubkey) != 0)
                return fail(b->index, "record %s: signer's public key does not match the registry", r->record_id);
        }

        char h[HASH_HEX_LEN];
        block_merkle(b, h);
        if (strcmp(h, b->merkle_root) != 0)
            return fail(b->index, "Merkle root recomputed from the records does not match (records were modified)%s", NULL);

        int reward;
        block_reward_txid(b, &reward, h);
        if (reward != b->token_reward || strcmp(h, b->transaction_id) != 0)
            return fail(b->index, "token_reward or transaction_id does not match the block's records%s", NULL);

        block_header_hash(b, h);
        if (strcmp(h, b->hash) != 0)
            return fail(b->index, "stored hash does not match the recomputed header hash (block was modified)%s", NULL);
        if (!hash_meets_difficulty(b->hash, b->difficulty))
            return fail(b->index, "hash does not meet the proof-of-work difficulty%s", NULL);

        snprintf(expected_prev, HASH_HEX_LEN, "%s", b->hash);
        prev_ts = b->timestamp;
        expected_index++;
    }
    ChainCheck ok = { .valid = 1, .bad_index = -1, .reason = "" };
    return ok;
}

/* ---- Persistence ---------------------------------------------------------- */

int chain_save(const Chain *c, const char *path) {
    char tmp[256];
    snprintf(tmp, sizeof(tmp), "%s.tmp", path);
    FILE *f = fopen(tmp, "w");
    if (!f) { fprintf(stderr, "ERROR: cannot write '%s'.\n", tmp); return 0; }
    fprintf(f, "# library chain v1: B|index|timestamp|record_count|previous_hash|merkle_root|"
               "hash|sealed_by|token_reward|transaction_id|difficulty|nonce, then R| lines\n");
    char line[RECORD_LINE_LEN];
    for (const Block *b = c->head; b; b = b->next) {
        fprintf(f, "B|%d|%ld|%d|%s|%s|%s|%s|%d|%s|%d|%lu\n", b->index, (long)b->timestamp,
                b->record_count, b->previous_hash, b->merkle_root, b->hash, b->sealed_by,
                b->token_reward, b->transaction_id, b->difficulty, b->nonce);
        for (int i = 0; i < b->record_count; i++) {
            record_to_line(&b->records[i], line, sizeof(line));
            fprintf(f, "R|%s\n", line);
        }
    }
    int ok = (fflush(f) == 0);
    fclose(f);
    /* Write to a temporary file, then rename: a crash mid-save never leaves a
     * half-written chain file behind. */
    if (!ok || rename(tmp, path) != 0) { fprintf(stderr, "ERROR: cannot save '%s'.\n", path); return 0; }
    return 1;
}

static int split(char *line, char *f[], int max) {
    int n = 0;
    char *p = line;
    f[n++] = p;
    for (char *bar = strchr(p, '|'); bar; bar = strchr(p, '|')) {
        if (n == max) return max + 1;
        *bar = '\0';
        p = bar + 1;
        f[n++] = p;
    }
    return n;
}

static int parse_header(char *line, Block *b) {
    char *f[13];
    if (split(line, f, 12) != 12) return 0;
    long idx, ts, cnt, reward, diff, nonce;
    if (!parse_long(f[1], &idx) || !parse_long(f[2], &ts) || !parse_long(f[3], &cnt) ||
        !parse_long(f[8], &reward) || !parse_long(f[10], &diff) || !parse_long(f[11], &nonce) ||
        cnt < 0 || cnt > MAX_BATCH_SIZE || nonce < 0 ||
        strlen(f[4]) >= HASH_HEX_LEN || strlen(f[5]) >= HASH_HEX_LEN || strlen(f[6]) >= HASH_HEX_LEN ||
        strlen(f[7]) >= SEALED_BY_LEN || strlen(f[9]) >= HASH_HEX_LEN) return 0;
    b->index = (int)idx;
    b->timestamp = (time_t)ts;
    b->record_count = (int)cnt;
    snprintf(b->previous_hash, HASH_HEX_LEN, "%s", f[4]);
    snprintf(b->merkle_root, HASH_HEX_LEN, "%s", f[5]);
    snprintf(b->hash, HASH_HEX_LEN, "%s", f[6]);
    snprintf(b->sealed_by, SEALED_BY_LEN, "%s", f[7]);
    b->token_reward = (int)reward;
    snprintf(b->transaction_id, HASH_HEX_LEN, "%s", f[9]);
    b->difficulty = (int)diff;
    b->nonce = (unsigned long)nonce;
    return 1;
}

int chain_load(Chain *c, const char *path) {
    FILE *f = fopen(path, "r");
    if (!f) return 0;
    char line[RECORD_LINE_LEN + 8];
    Block *cur = NULL;
    int filled = 0, ok = 1;
    while (ok && fgets(line, sizeof(line), f)) {
        line[strcspn(line, "\r\n")] = '\0';
        if (line[0] == '\0' || line[0] == '#') continue;
        if (strncmp(line, "B|", 2) == 0) {
            if (cur && filled != cur->record_count) { ok = 0; break; }
            cur = calloc(1, sizeof(Block));
            if (!cur || !parse_header(line, cur)) { free(cur); ok = 0; break; }
            if (cur->record_count > 0) {
                cur->records = calloc((size_t)cur->record_count, sizeof(Record));
                if (!cur->records) { free(cur); ok = 0; break; }
            }
            chain_append(c, cur);
            filled = 0;
        } else if (strncmp(line, "R|", 2) == 0) {
            if (!cur || filled >= cur->record_count || !record_from_line(line + 2, &cur->records[filled])) {
                ok = 0;
                break;
            }
            filled++;
        } else {
            ok = 0;
        }
    }
    fclose(f);
    if (ok && cur && filled != cur->record_count) ok = 0;
    if (!ok || c->length == 0) {
        fprintf(stderr, "ERROR: chain file '%s' is corrupt or empty.\n", path);
        chain_free(c);
        return -1;
    }
    return 1;
}
