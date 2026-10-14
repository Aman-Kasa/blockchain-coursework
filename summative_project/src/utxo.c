#include "utxo.h"
#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void utxo_init(UtxoSet *set) {
    set->head = set->tail = NULL;
    set->next_id = 1;
}

void utxo_free(UtxoSet *set) {
    Utxo *u = set->head;
    while (u) {
        Utxo *next = u->next;
        free(u);
        u = next;
    }
    utxo_init(set);
}

static int add_output(UtxoSet *set, const char *txid, int vout, const char *owner, long amount) {
    Utxo *u = calloc(1, sizeof(Utxo));
    if (!u) return 0;
    u->id = set->next_id++;
    snprintf(u->txid, HASH_HEX_LEN, "%s", txid);
    u->vout = vout;
    snprintf(u->owner, ID_LEN, "%s", owner);
    u->amount = amount;
    if (set->tail) set->tail->next = u; else set->head = u;
    set->tail = u;
    return 1;
}

long utxo_credit_reward(UtxoSet *set, const char *member, long gross, const char *txid) {
    if (gross <= TX_FEE) return 0;
    return add_output(set, txid, 0, member, gross - TX_FEE) ? TX_FEE : 0;
}

int utxo_transfer(UtxoSet *set, const char *from, const char *to, long amount,
                  const char *txid, long *fee_out, char *err, size_t errcap, int verbose) {
    const long needed = amount + TX_FEE;
    Utxo *inputs[64];
    size_t n = 0;
    long total = 0;
    /* Coin selection: oldest unspent outputs first. Nothing is changed until
     * the inputs are known to cover amount + fee. */
    for (Utxo *u = set->head; u && total < needed && n < 64; u = u->next) {
        if (u->spent || strcmp(u->owner, from) != 0) continue;
        inputs[n++] = u;
        total += u->amount;
    }
    if (amount <= 0 || total < needed) {
        snprintf(err, errcap, "insufficient funds: %s's unspent outputs total %ld, but amount %ld + fee %d = %ld",
                 from, total, amount, TX_FEE, needed);
        return 0;
    }
    long change = total - needed;
    if (!add_output(set, txid, 0, to, amount) || (change > 0 && !add_output(set, txid, 1, from, change))) {
        snprintf(err, errcap, "out of memory");
        return 0;
    }
    for (size_t i = 0; i < n; i++) {
        inputs[i]->spent = 1;
        snprintf(inputs[i]->spent_by, HASH_HEX_LEN, "%s", txid);
    }
    *fee_out = TX_FEE;
    if (verbose) {
        printf("      UTXO tx %.12s...: spent", txid);
        for (size_t i = 0; i < n; i++) printf(" #%d(%ld)", inputs[i]->id, inputs[i]->amount);
        printf(" = %ld -> %ld to %s", total, amount, to);
        if (change > 0) printf(", %ld change back to %s", change, from);
        printf(", fee %d\n", TX_FEE);
    }
    return 1;
}

long utxo_balance(const UtxoSet *set, const char *member) {
    long total = 0;
    for (const Utxo *u = set->head; u; u = u->next)
        if (!u->spent && strcmp(u->owner, member) == 0) total += u->amount;
    return total;
}

void utxo_print(const UtxoSet *set) {
    printf("    UTXO set (unspent outputs):\n      %-5s %-9s %-7s %s\n", "ID", "Owner", "Amount", "Created by (txid:vout)");
    int any = 0;
    for (const Utxo *u = set->head; u; u = u->next) {
        if (u->spent) continue;
        printf("      #%-4d %-9s %-7ld %.16s...:%d\n", u->id, u->owner, u->amount, u->txid, u->vout);
        any = 1;
    }
    if (!any) printf("      (empty)\n");
}
