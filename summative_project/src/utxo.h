#ifndef UTXO_H
#define UTXO_H

#include <stddef.h>

#include "util.h"

/* UTXO model. A member's coins are the unspent transaction outputs that name
 * them as owner; the balance is their sum. Every transaction consumes whole
 * outputs (inputs) and creates new ones (outputs). */

typedef struct Utxo {
    int  id;                       /* short id shown by utxo_view (#1, #2 ...) */
    char txid[HASH_HEX_LEN];       /* transaction that created it              */
    int  vout;                     /* output position within that transaction  */
    char owner[ID_LEN];
    long amount;
    int  spent;
    char spent_by[HASH_HEX_LEN];   /* txid of the transaction that spent it    */
    struct Utxo *next;
} Utxo;

typedef struct {
    Utxo *head;
    Utxo *tail;
    int   next_id;
} UtxoSet;

void utxo_init(UtxoSet *set);
void utxo_free(UtxoSet *set);

/* Reward: a transaction with no inputs (like a coinbase) paying gross - fee
 * to the member. Returns the fee collected, or 0 if nothing was created. */
long utxo_credit_reward(UtxoSet *set, const char *member, long gross, const char *txid);

/* Transfer: selects the sender's oldest unspent outputs until they cover
 * amount + fee. Rejected (0, reason in err) if they cannot. On success the
 * inputs are marked spent (so they can never be spent again), an output pays
 * the recipient, and any excess returns to the sender as a change output.
 * txid identifies the transaction (derived from the signed record). */
int utxo_transfer(UtxoSet *set, const char *from, const char *to, long amount,
                  const char *txid, long *fee_out, char *err, size_t errcap, int verbose);

long utxo_balance(const UtxoSet *set, const char *member);

/* Prints every unspent output and its owner. */
void utxo_print(const UtxoSet *set);

#endif
