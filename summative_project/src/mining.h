#ifndef MINING_H
#define MINING_H

#include "app.h"

/* Mining is the only way a request reaches the chain.
 *
 * Block construction: the pool is sorted by priority and the top requests are
 * taken, up to the batch size, skipping SUSPICIOUS ones. Each candidate is
 * re-verified (ECDSA signature against the registered public key), checked
 * against the ledger for token transfers, and applied to a working copy of
 * the confirmed state; that also enforces nonce order, so a member's requests
 * are always mined in the order they were made. Proof of work then runs on
 * the block header; the block is appended, its records are applied to the
 * confirmed state and the ledger (paying reward transactions), and the
 * confirmed requests are removed from the pool.
 *
 * All three methods work on the same pending pool. */

/* One miner seals one block and receives the full block reward plus fees. */
int mine_solo(App *app);

/* n_miners miners with random hash rates seal one block together; the reward
 * (minus a 2% pool fee) is shared by each miner's hash attempts. */
int mine_pool(App *app, int n_miners);

/* A rented rig mines for `rounds` rounds (1-5), sealing as many blocks as it
 * can; each round costs a rental fee and rewards lose a maintenance fee. */
int mine_cloud(App *app, int rounds);

/* Prints the member balances / UTXO set (after every confirmed block). */
void mining_print_ledger(const App *app);

#endif
