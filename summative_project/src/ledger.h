#ifndef LEDGER_H
#define LEDGER_H

#include "account.h"
#include "chain.h"
#include "utxo.h"

/* Member token balances under the model chosen for the session. Both models
 * are driven only by confirmed records on the chain (RETURNED rewards and
 * TRANSFER records), so balances can always be rebuilt from the chain. */

typedef enum { MODEL_UTXO = 0, MODEL_ACCOUNT } LedgerModel;

typedef struct {
    LedgerModel   model;
    UtxoSet       utxo;
    AccountLedger acc;
} Ledger;

void ledger_init(Ledger *led, LedgerModel model);
void ledger_free(Ledger *led);
const char *ledger_model_name(LedgerModel m);

/* Applies one confirmed record. Returns the transaction fee it collected (for
 * the miner), 0 if it carried no token transaction, or -1 if the ledger
 * rejected it (err says why). */
long ledger_apply(Ledger *led, const Record *r, int verbose, char *err, size_t errcap);

/* Clears the ledger and replays every confirmed record on the chain. */
void ledger_rebuild(Ledger *led, const Chain *c);

long ledger_balance(const Ledger *led, const char *member_id);

#endif
