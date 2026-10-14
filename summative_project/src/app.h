#ifndef APP_H
#define APP_H

#include "chain.h"
#include "ledger.h"
#include "pool.h"
#include "registry.h"
#include "state.h"

#define MAX_MINERS 24
#define MINER_ID_LEN 32

/* Mining payouts. Miners are not library members, so their earnings are kept
 * apart from the member ledgers. */
typedef struct {
    char   id[MINER_ID_LEN];
    double earned;
    int    blocks;
} MinerAccount;

/* Everything the CLI works on. */
typedef struct {
    Registry     reg;
    Chain        chain;
    LibState    *state;        /* confirmed state: the chain replayed (heap, it is large) */
    Pool         pool;
    Ledger       ledger;       /* member token balances (UTXO or account model)           */
    MinerAccount miners[MAX_MINERS];
    int          nminers;
    int          difficulty;   /* proof-of-work difficulty for new blocks (1-4)           */
    int          batch_size;   /* records per block                                       */
    int          block_reward;
    int          read_only;    /* set when the loaded chain fails validation               */
} App;

#endif
