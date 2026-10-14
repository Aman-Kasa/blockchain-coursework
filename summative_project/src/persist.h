#ifndef PERSIST_H
#define PERSIST_H

#include "app.h"

/* Disk persistence (plain-text structured .dat files).
 *
 *   data/chain.dat  every block header ("B|...") followed by its records ("R|...")
 *   data/state.dat  settings and chain metadata (META), copy inventory (COPY),
 *                   reservation queues (QUEUE), nonces and fines (SIGNER), return
 *                   history (RETURN), the UTXO set (UTXO) or account balances
 *                   (ACCOUNT), and mining payouts (MINER)
 *   data/pool.dat   the pending pool, one signed request per line ("P|...")
 *
 * Files are written to a temporary name and renamed, so a crash never leaves
 * a half-written file. On startup the chain is validated and replayed from
 * the genesis block; the saved inventory, queues, nonces and balances must
 * match what the chain implies, otherwise state.dat is reported as tampered
 * and the state rebuilt from the chain is used instead. */

typedef struct {
    int  valid;
    int  bad_index;
    char reason[400];
} FullCheck;

/* Loads everything, choosing `model` for the token ledger. Creates a new
 * chain with a genesis block if no chain file exists. Validates the loaded
 * chain before returning; if it fails, app->read_only is set. Returns 0 only
 * on a fatal error. `cli_difficulty` / `cli_batch` override saved settings
 * when > 0. */
int persist_load(App *app, LedgerModel model, int cli_difficulty, int cli_batch);

/* Writes chain.dat, state.dat and pool.dat. Returns 1 on success. Refuses
 * (returns 0) while the session is read-only, so a tampered chain is never
 * written back as if it were valid. */
int persist_save(const App *app);

/* Full validation: chain_validate (hashes, links, Merkle roots, signatures,
 * proof of work, timestamps) plus replaying every record through the
 * library state machine from the genesis block. On success `rebuilt`
 * (optional) receives the replayed state. */
FullCheck persist_validate(const App *app, LibState *rebuilt);

#endif
