#include "record.h"
#include "config.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

static const char *ACTION_NAMES[ACT_COUNT] = {
    "BORROWED", "RETURNED", "RESERVED", "RENEWED", "OVERDUE", "LOST", "FINE_PAID", "TRANSFER"
};

const char *action_name(Action a) {
    return (a >= 0 && a < ACT_COUNT) ? ACTION_NAMES[a] : "?";
}

int action_parse(const char *s, Action *out) {
    for (int i = 0; i < ACT_COUNT; i++)
        if (strcasecmp(s, ACTION_NAMES[i]) == 0) { *out = (Action)i; return 1; }
    return 0;
}

#define CONTENT_FIELDS 17

void record_content(const Record *r, char *out, size_t cap) {
    snprintf(out, cap, "v1|%ld|%s|%s|%s|%s|%s|%s|%ld|%ld|%d|%s|%s|%s|%ld|%s|%ld|%s",
             (long)r->timestamp, action_name(r->action),
             r->book_id, r->copy_id, r->book_title, r->member_id, r->member_name,
             (long)r->due_date, r->fine_amount, r->token_reward,
             r->condition, r->reason, r->counterparty, r->amount,
             r->signer_id, r->nonce, r->pubkey);
}

static void compute_id(const Record *r, char out[RECORD_ID_LEN]) {
    char content[RECORD_LINE_LEN], h[HASH_HEX_LEN];
    record_content(r, content, sizeof(content));
    sha256_str(content, h);
    snprintf(out, RECORD_ID_LEN, "%.16s", h);
}

int record_sign(Record *r, EVP_PKEY *priv) {
    compute_id(r, r->record_id);
    char content[RECORD_LINE_LEN];
    record_content(r, content, sizeof(content));
    return crypto_sign(priv, content, strlen(content), r->signature, &r->sig_len);
}

int record_verify(const Record *r) {
    char id[RECORD_ID_LEN];
    compute_id(r, id);
    if (strcmp(id, r->record_id) != 0) return 0;          /* content was changed */
    char content[RECORD_LINE_LEN];
    record_content(r, content, sizeof(content));
    return crypto_verify_hex(r->pubkey, content, strlen(content), r->signature, r->sig_len);
}

void record_hash(const Record *r, char out[HASH_HEX_LEN]) {
    char content[RECORD_LINE_LEN], sig[MAX_SIG_LEN * 2 + 1], buf[RECORD_LINE_LEN + 200];
    record_content(r, content, sizeof(content));
    bytes_to_hex(r->signature, r->sig_len, sig, sizeof(sig));
    snprintf(buf, sizeof(buf), "%s|%s|%s", r->record_id, content, sig);
    sha256_str(buf, out);
}

void record_reward_txid(const Record *r, char out[HASH_HEX_LEN]) {
    if (r->action != ACT_RETURNED || r->token_reward <= 0) { out[0] = '\0'; return; }
    char buf[256];
    snprintf(buf, sizeof(buf), "REWARD|SYSTEM|%s|%s|%s|%d|%d",
             r->member_id, r->copy_id, r->record_id, r->token_reward, TX_FEE);
    sha256_str(buf, out);
}

void record_to_line(const Record *r, char *out, size_t cap) {
    char content[RECORD_LINE_LEN], sig[MAX_SIG_LEN * 2 + 1];
    record_content(r, content, sizeof(content));
    bytes_to_hex(r->signature, r->sig_len, sig, sizeof(sig));
    snprintf(out, cap, "%s|%s|%s", r->record_id, content, sig);
}

/* Field parsing helpers: copy with a length check, parse numbers strictly. */
static int cp(char *dst, size_t cap, const char *src) {
    if (strlen(src) >= cap) return 0;
    snprintf(dst, cap, "%s", src);
    return 1;
}

static int num(const char *s, long *out) { return parse_long(s, out); }

int record_from_line(char *line, Record *r) {
    char *f[CONTENT_FIELDS + 3];
    int n = 0;
    char *p = line;
    f[n++] = p;
    for (char *bar = strchr(p, '|'); bar; bar = strchr(p, '|')) {
        if (n == CONTENT_FIELDS + 3) return 0;
        *bar = '\0';
        p = bar + 1;
        f[n++] = p;
    }
    /* record_id | v1 | 17 content fields | signature = 20 fields */
    if (n != CONTENT_FIELDS + 3 || strcmp(f[1], "v1") != 0) return 0;

    memset(r, 0, sizeof(*r));
    long ts, due, fine, reward, amount, nonce;
    if (!cp(r->record_id, RECORD_ID_LEN, f[0]) || !num(f[2], &ts) || !action_parse(f[3], &r->action) ||
        !cp(r->book_id, ID_LEN, f[4]) || !cp(r->copy_id, COPY_ID_LEN, f[5]) ||
        !cp(r->book_title, TITLE_LEN, f[6]) || !cp(r->member_id, ID_LEN, f[7]) ||
        !cp(r->member_name, NAME_LEN, f[8]) || !num(f[9], &due) || !num(f[10], &fine) ||
        !num(f[11], &reward) || !cp(r->condition, COND_LEN, f[12]) || !cp(r->reason, REASON_LEN, f[13]) ||
        !cp(r->counterparty, ID_LEN, f[14]) || !num(f[15], &amount) || !cp(r->signer_id, ID_LEN, f[16]) ||
        !num(f[17], &nonce) || !cp(r->pubkey, PUBKEY_HEX_LEN, f[18])) {
        return 0;
    }
    r->timestamp = (time_t)ts;
    r->due_date = (time_t)due;
    r->fine_amount = fine;
    r->token_reward = (int)reward;
    r->amount = amount;
    r->nonce = nonce;
    r->sig_len = hex_to_bytes(f[19], r->signature, sizeof(r->signature));
    if (r->sig_len == 0 && f[19][0] != '\0') return 0;
    return 1;
}
