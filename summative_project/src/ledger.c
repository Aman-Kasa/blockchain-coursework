#include "ledger.h"

#include <stdio.h>
#include <string.h>

void ledger_init(Ledger *led, LedgerModel model) {
    led->model = model;
    utxo_init(&led->utxo);
    account_init(&led->acc);
}

void ledger_free(Ledger *led) {
    utxo_free(&led->utxo);
    account_free(&led->acc);
}

const char *ledger_model_name(LedgerModel m) { return m == MODEL_UTXO ? "UTXO" : "Account-based"; }

long ledger_apply(Ledger *led, const Record *r, int verbose, char *err, size_t errcap) {
    if (r->action == ACT_RETURNED && r->token_reward > 0) {
        char txid[HASH_HEX_LEN];
        record_reward_txid(r, txid);
        long fee = led->model == MODEL_UTXO
                       ? utxo_credit_reward(&led->utxo, r->member_id, r->token_reward, txid)
                       : account_credit_reward(&led->acc, r->member_id, r->token_reward, r->timestamp);
        if (verbose)
            printf("      Reward transaction %.12s... confirmed: %s credited %d - fee %ld = %ld coins\n",
                   txid, r->member_id, r->token_reward, fee, r->token_reward - fee);
        if (led->model == MODEL_ACCOUNT) account_note_nonce(&led->acc, r->member_id, r->nonce);
        return fee;
    }
    if (r->action == ACT_TRANSFER) {
        long fee = 0;
        int ok;
        if (led->model == MODEL_UTXO) {
            char seed[64], txid[HASH_HEX_LEN];
            snprintf(seed, sizeof(seed), "TRANSFER|%s", r->record_id);
            sha256_str(seed, txid);
            ok = utxo_transfer(&led->utxo, r->member_id, r->counterparty, r->amount, txid, &fee, err, errcap, verbose);
        } else {
            ok = account_transfer(&led->acc, r->member_id, r->counterparty, r->amount, r->nonce, r->timestamp,
                                  &fee, err, errcap, verbose);
        }
        return ok ? fee : -1;
    }
    /* Any other request by a member still uses up one of their nonces. */
    if (led->model == MODEL_ACCOUNT && strcmp(r->signer_id, r->member_id) == 0)
        account_note_nonce(&led->acc, r->member_id, r->nonce);
    return 0;
}

void ledger_rebuild(Ledger *led, const Chain *c) {
    LedgerModel m = led->model;
    ledger_free(led);
    ledger_init(led, m);
    char err[160];
    for (const Block *b = c->head; b; b = b->next)
        for (int i = 0; i < b->record_count; i++) ledger_apply(led, &b->records[i], 0, err, sizeof(err));
}

long ledger_balance(const Ledger *led, const char *member_id) {
    if (led->model == MODEL_UTXO) return utxo_balance(&led->utxo, member_id);
    const Account *a = account_find(&led->acc, member_id);
    return a ? a->balance : 0;
}
