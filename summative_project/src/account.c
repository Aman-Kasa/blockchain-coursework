#include "account.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void account_init(AccountLedger *led) { memset(led, 0, sizeof(*led)); }

void account_free(AccountLedger *led) {
    for (size_t i = 0; i < led->count; i++) {
        TxRecord *t = led->accounts[i].history_head;
        while (t) {
            TxRecord *next = t->next;
            free(t);
            t = next;
        }
    }
    account_init(led);
}

const Account *account_find(const AccountLedger *led, const char *member_id) {
    for (size_t i = 0; i < led->count; i++)
        if (strcmp(led->accounts[i].member_id, member_id) == 0) return &led->accounts[i];
    return NULL;
}

Account *account_get(AccountLedger *led, const char *member_id) {
    Account *a = (Account *)account_find(led, member_id);
    if (a || led->count >= MAX_MEMBERS) return a;
    a = &led->accounts[led->count++];
    memset(a, 0, sizeof(*a));
    snprintf(a->member_id, ID_LEN, "%s", member_id);
    return a;
}

static void log_tx(Account *a, const char *kind, const char *sender, const char *recipient,
                   long amount, long fee, long nonce, time_t ts) {
    TxRecord *t = calloc(1, sizeof(TxRecord));
    if (!t) return;
    snprintf(t->kind, sizeof(t->kind), "%s", kind);
    snprintf(t->sender, ID_LEN, "%s", sender);
    snprintf(t->recipient, ID_LEN, "%s", recipient);
    t->amount = amount;
    t->fee = fee;
    t->nonce = nonce;
    t->timestamp = ts;
    if (a->history_tail) a->history_tail->next = t; else a->history_head = t;
    a->history_tail = t;
}

long account_credit_reward(AccountLedger *led, const char *member, long gross, time_t ts) {
    Account *a = account_get(led, member);
    if (!a || gross <= TX_FEE) return 0;
    a->balance += gross - TX_FEE;
    log_tx(a, "REWARD", "SYSTEM", member, gross - TX_FEE, TX_FEE, -1, ts);
    return TX_FEE;
}

void account_note_nonce(AccountLedger *led, const char *member, long nonce) {
    Account *a = account_get(led, member);
    if (a && nonce + 1 > a->nonce) a->nonce = nonce + 1;
}

int account_transfer(AccountLedger *led, const char *from, const char *to, long amount, long nonce,
                     time_t ts, long *fee_out, char *err, size_t errcap, int verbose) {
    Account *s = account_get(led, from), *r = account_get(led, to);
    if (!s || !r || s == r) { snprintf(err, errcap, "invalid sender or recipient"); return 0; }
    if (amount <= 0 || s->balance < amount + TX_FEE) {
        snprintf(err, errcap, "insufficient balance: %s has %ld, needs amount %ld + fee %d = %ld",
                 from, s->balance, amount, TX_FEE, amount + TX_FEE);
        return 0;
    }
    s->balance -= amount + TX_FEE;   /* debit  */
    r->balance += amount;            /* credit */
    s->outgoing++;
    account_note_nonce(led, from, nonce);
    log_tx(s, "TRANSFER", from, to, amount, TX_FEE, nonce, ts);
    log_tx(r, "TRANSFER", from, to, amount, TX_FEE, nonce, ts);
    *fee_out = TX_FEE;
    if (verbose)
        printf("      Account transfer: %s -> %s %ld (fee %d, nonce %ld); %s now has %ld, %s has %ld\n",
               from, to, amount, TX_FEE, nonce, from, s->balance, to, r->balance);
    return 1;
}

void account_print_history(const AccountLedger *led, const char *member_id) {
    const Account *a = account_find(led, member_id);
    printf("Transaction history for %s (balance %ld, next nonce %ld):\n", member_id,
           a ? a->balance : 0, a ? a->nonce : 0);
    if (!a || !a->history_head) { printf("  (no token transactions yet)\n"); return; }
    printf("  %-9s %-19s %-9s %-9s %-7s %-4s %s\n", "Kind", "Time", "Sender", "Recipient", "Amount", "Fee", "Nonce");
    for (const TxRecord *t = a->history_head; t; t = t->next) {
        char ts[32], nonce[24] = "-";
        format_time(t->timestamp, ts, sizeof(ts));
        if (t->nonce >= 0) snprintf(nonce, sizeof(nonce), "%ld", t->nonce);
        printf("  %-9s %-19s %-9s %-9s %-7ld %-4ld %s\n", t->kind, ts, t->sender, t->recipient, t->amount, t->fee, nonce);
    }
}
