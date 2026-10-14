#include "mining.h"
#include "lending.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define POW_CAP 200000000UL   /* far above the 65,536 attempts expected at difficulty 4 */

static double ms_since(clock_t start) { return 1000.0 * (double)(clock() - start) / CLOCKS_PER_SEC; }

/* ===================================================================== */
/* Miner payouts                                                          */
/* ===================================================================== */

static void pay_miner(App *app, const char *id, double amount, int block) {
    MinerAccount *m = NULL;
    for (int i = 0; i < app->nminers; i++)
        if (strcmp(app->miners[i].id, id) == 0) m = &app->miners[i];
    if (!m && app->nminers < MAX_MINERS) {
        m = &app->miners[app->nminers++];
        memset(m, 0, sizeof(*m));
        snprintf(m->id, MINER_ID_LEN, "%s", id);
    }
    if (!m) return;
    m->earned += amount;
    m->blocks += block;
}

void mining_print_ledger(const App *app) {
    if (app->ledger.model == MODEL_UTXO) {
        utxo_print(&app->ledger.utxo);
    }
    printf("    Member balances (%s model):", ledger_model_name(app->ledger.model));
    for (size_t i = 0; i < app->reg.member_count; i++)
        printf(" %s=%ld", app->reg.members[i].member_id, ledger_balance(&app->ledger, app->reg.members[i].member_id));
    printf("\n");
}

/* ===================================================================== */
/* Block construction and confirmation (shared by all three methods)      */
/* ===================================================================== */

/* Chooses up to batch_size records and prepares (but does not mine) the
 * block. Returns NULL if nothing can be confirmed. */
static Block *build_block(App *app, const char *sealed_by, unsigned long *seqs) {
    Pool *p = &app->pool;
    if (p->count == 0) return NULL;
    size_t *order = malloc(p->count * sizeof(size_t));
    char *taken = calloc(p->count, 1);
    LibState *work = malloc(sizeof(LibState));
    Record *recs = malloc((size_t)app->batch_size * sizeof(Record));
    long *spent = calloc(app->reg.member_count + 1, sizeof(long));   /* transfers taken per sender */
    Block *b = NULL;
    if (!order || !taken || !work || !recs || !spent) goto done;
    memcpy(work, app->state, sizeof(LibState));
    pool_priority_order(p, order);

    int n = 0, progress = 1;
    char err[256];
    /* Several passes: a request whose earlier-nonce sibling sits lower in the
     * priority order becomes valid once that sibling has been taken. */
    while (n < app->batch_size && progress) {
        progress = 0;
        for (size_t k = 0; k < p->count && n < app->batch_size; k++) {
            size_t i = order[k];
            PoolEntry *e = &p->items[i];
            if (taken[i] || e->status != PS_PENDING) continue;
            const char *registered = registry_pubkey(&app->reg, e->rec.signer_id);
            if (!record_verify(&e->rec) || !registered || strcmp(registered, e->rec.pubkey) != 0) {
                printf("  Rejected %s: invalid or missing ECDSA signature; removed from the pool.\n", e->rec.record_id);
                taken[i] = 2;
                continue;
            }
            if (e->rec.action == ACT_TRANSFER) {
                size_t m = 0;
                while (m < app->reg.member_count && strcmp(app->reg.members[m].member_id, e->rec.member_id) != 0) m++;
                long need = e->rec.amount + TX_FEE;
                if (ledger_balance(&app->ledger, e->rec.member_id) - spent[m] < need) continue;
                if (!state_apply(work, &app->reg, &e->rec, err, sizeof(err), NULL)) continue;
                spent[m] += need;
            } else if (!state_apply(work, &app->reg, &e->rec, err, sizeof(err), NULL)) {
                continue;
            }
            taken[i] = 1;
            recs[n] = e->rec;
            seqs[n] = e->seq;
            n++;
            progress = 1;
        }
    }
    for (size_t i = p->count; i-- > 0;)
        if (taken[i] == 2) pool_remove_seq(p, p->items[i].seq);
    if (n > 0) b = block_prepare(&app->chain, recs, n, app->difficulty, sealed_by, sim_now());
done:
    free(order);
    free(taken);
    free(work);
    free(recs);
    free(spent);
    return b;
}

/* Appends a mined block, applies its records to the state and ledger, and
 * removes them from the pool. Returns the transaction fees collected. */
static long confirm_block(App *app, Block *b, const unsigned long *seqs) {
    chain_append(&app->chain, b);
    char err[256], offer[ID_LEN];
    long fees = 0;
    printf("  Block %d confirmed (%d record(s), Merkle root %.16s..., hash %.16s...):\n",
           b->index, b->record_count, b->merkle_root, b->hash);
    for (int i = 0; i < b->record_count; i++) {
        const Record *r = &b->records[i];
        if (!state_apply(app->state, &app->reg, r, err, sizeof(err), offer))
            printf("    INTERNAL ERROR applying %s: %s\n", r->record_id, err);   /* cannot happen */
        char subject[64];
        const char *item = r->copy_id[0] ? r->copy_id : r->book_id;
        snprintf(subject, sizeof(subject), "%s%s%s", r->member_id, (r->member_id[0] && item[0]) ? " / " : "", item);
        printf("    + %-9s %-16s %-22s signer %-9s nonce %ld\n", action_name(r->action), r->record_id,
               subject, r->signer_id, r->nonce);
        long fee = ledger_apply(&app->ledger, r, 1, err, sizeof(err));
        if (fee < 0) printf("      LEDGER ERROR: %s\n", err);
        else fees += fee;
        if (r->action == ACT_RETURNED && r->fine_amount)
            printf("      Late by %ld day(s): fine of %ld coins added to %s's account.\n",
                   r->fine_amount / FINE_PER_DAY, r->fine_amount, r->member_id);
        if (offer[0])
            printf("      Copy %s is now RESERVED for %s, the next member in the reservation queue.\n",
                   r->copy_id, offer);
    }
    for (int i = 0; i < b->record_count; i++) pool_remove_seq(&app->pool, seqs[i]);
    mining_print_ledger(app);
    return fees;
}

static int nothing_to_mine(const App *app, Block *b) {
    if (b) return 0;
    if (app->pool.count == 0)
        printf("Nothing to mine: the pending pool is empty.\n");
    else
        printf("Nothing to mine: no PENDING request can be confirmed right now "
               "(SUSPICIOUS requests need approve_suspicious first).\n");
    return 1;
}

/* ===================================================================== */
/* Solo mining                                                            */
/* ===================================================================== */

int mine_solo(App *app) {
    if (app->read_only) { printf("ERROR: the chain failed validation; mining is disabled.\n"); return 0; }
    unsigned long *seqs = malloc((size_t)app->batch_size * sizeof(unsigned long));
    if (!seqs) return 0;
    const char *miner = "solo:LIB-NODE-1";
    Block *b = build_block(app, miner, seqs);
    if (nothing_to_mine(app, b)) { free(seqs); return 0; }

    printf("=== Solo mining: block %d, %d record(s) (top of the pool, batch size %d), difficulty %d ===\n",
           b->index, b->record_count, app->batch_size, app->difficulty);
    printf("  Incrementing the nonce until SHA-256(header) starts with %d zero%s...\n",
           app->difficulty, app->difficulty == 1 ? "" : "s");
    unsigned long attempts;
    clock_t t0 = clock();
    if (!block_pow(b, POW_CAP, &attempts)) { printf("ERROR: proof of work failed.\n"); block_free(b); free(seqs); return 0; }
    printf("  Valid hash after %lu hash attempts (nonce %lu, %.1f ms; expected about %lu at difficulty %d)\n",
           attempts, b->nonce, ms_since(t0), 1UL << (4 * app->difficulty), app->difficulty);
    long fees = confirm_block(app, b, seqs);
    double reward = app->block_reward + (double)fees;
    pay_miner(app, miner, reward, 1);
    printf("  Miner reward: %d block reward + %ld fee(s) = %.2f coins, all to %s.\n",
           app->block_reward, fees, reward, miner);
    lending_prune_pool(app);
    free(seqs);
    return 1;
}

/* ===================================================================== */
/* Pool mining                                                            */
/* ===================================================================== */

typedef struct {
    int hash_rate;
    unsigned long attempts;
} PoolMiner;

int mine_pool(App *app, int n_miners) {
    if (app->read_only) { printf("ERROR: the chain failed validation; mining is disabled.\n"); return 0; }
    if (n_miners < POOL_MIN_MINERS || n_miners > POOL_MAX_MINERS) {
        printf("ERROR: pool size must be between %d and %d miners.\n", POOL_MIN_MINERS, POOL_MAX_MINERS);
        return 0;
    }
    unsigned long *seqs = malloc((size_t)app->batch_size * sizeof(unsigned long));
    if (!seqs) return 0;
    Block *b = build_block(app, "pool:ALU-POOL", seqs);
    if (nothing_to_mine(app, b)) { free(seqs); return 0; }

    PoolMiner miners[POOL_MAX_MINERS];
    printf("=== Pool mining: block %d, %d record(s), %d miners, difficulty %d ===\n",
           b->index, b->record_count, n_miners, app->difficulty);
    for (int i = 0; i < n_miners; i++) {
        miners[i].hash_rate = POOL_MIN_HASHRATE + rand() % (POOL_MAX_HASHRATE - POOL_MIN_HASHRATE + 1);
        miners[i].attempts = 0;
        printf("  Miner M%d hash rate: %d attempts/round\n", i + 1, miners[i].hash_rate);
    }

    /* All miners hash at the same time. In each round miner i searches its
     * own slice of the nonce space (hash_rate nonces, starting where miner
     * i-1's slice ends), so no work is repeated. A miner that finds a valid
     * hash after k of its r attempts found it at time k/r into the round; the
     * earliest finder wins, and every miner is credited only with the attempts
     * it had made by that moment. */
    int winner = -1, rounds = 0;
    double win_time = 0.0;
    unsigned long round_start = 0;
    Block trial;
    while (winner < 0) {
        rounds++;
        unsigned long slice = round_start, found_at[POOL_MAX_MINERS], win_nonce = 0;
        char win_hash[HASH_HEX_LEN] = "";
        for (int i = 0; i < n_miners; i++) {
            trial = *b;
            trial.nonce = slice;
            unsigned long a = 0;
            found_at[i] = block_pow(&trial, (unsigned long)miners[i].hash_rate, &a) ? a : 0;
            if (found_at[i]) {
                double t = (double)found_at[i] / miners[i].hash_rate;
                if (winner < 0 || t < win_time) {
                    winner = i;
                    win_time = t;
                    win_nonce = trial.nonce;
                    memcpy(win_hash, trial.hash, HASH_HEX_LEN);
                }
            }
            slice += (unsigned long)miners[i].hash_rate;
        }
        for (int i = 0; i < n_miners; i++) {
            unsigned long rate = (unsigned long)miners[i].hash_rate, done = rate;
            if (winner == i) done = found_at[i];
            else if (winner >= 0) {
                done = (unsigned long)(win_time * rate + 0.999999);
                if (done > rate) done = rate;
            }
            miners[i].attempts += done;
        }
        if (winner >= 0) {
            b->nonce = win_nonce;
            memcpy(b->hash, win_hash, HASH_HEX_LEN);
        }
        round_start = slice;
    }
    printf("  Valid hash found by M%d in round %d, %.0f%% of the way through it (nonce %lu)\n",
           winner + 1, rounds, win_time * 100.0, b->nonce);

    long fees = confirm_block(app, b, seqs);
    unsigned long total_attempts = 0;
    for (int i = 0; i < n_miners; i++) total_attempts += miners[i].attempts;
    double gross = app->block_reward + (double)fees;
    double pool_fee = gross * POOL_FEE_PERCENT / 100.0;
    double share_total = gross - pool_fee;

    printf("\n  --- Pool reward distribution ---\n");
    printf("  Total reward %d block reward + %ld fee(s) = %.2f; pool fee %.0f%% = %.2f; distributed %.2f\n",
           app->block_reward, fees, gross, POOL_FEE_PERCENT, pool_fee, share_total);
    printf("  Share = miner_attempts / total_attempts; reward = share x (block reward after the pool fee)\n");
    printf("  +----------+-----------+----------+---------+------------+\n");
    printf("  | Miner ID | Hash rate | Attempts | Share %% | Reward     |\n");
    printf("  +----------+-----------+----------+---------+------------+\n");
    for (int i = 0; i < n_miners; i++) {
        double share = total_attempts ? (double)miners[i].attempts / (double)total_attempts : 0.0;
        char id[MINER_ID_LEN];
        snprintf(id, sizeof(id), "pool:M%d", i + 1);
        pay_miner(app, id, share * share_total, i == winner);
        printf("  | M%-7d | %9d | %8lu | %6.2f%% | %10.2f |%s\n", i + 1, miners[i].hash_rate, miners[i].attempts,
               share * 100.0, share * share_total, i == winner ? " <- found the block" : "");
    }
    printf("  +----------+-----------+----------+---------+------------+\n");
    printf("  | TOTAL    |           | %8lu | 100.00%% | %10.2f |\n", total_attempts, share_total);
    printf("  +----------+-----------+----------+---------+------------+\n");
    pay_miner(app, "pool:operator-fee", pool_fee, 0);
    lending_prune_pool(app);
    free(seqs);
    return 1;
}

/* ===================================================================== */
/* Cloud mining                                                           */
/* ===================================================================== */

int mine_cloud(App *app, int rounds) {
    if (app->read_only) { printf("ERROR: the chain failed validation; mining is disabled.\n"); return 0; }
    if (rounds < CLOUD_MIN_ROUNDS || rounds > CLOUD_MAX_ROUNDS) {
        printf("ERROR: rental duration must be between %d and %d rounds.\n", CLOUD_MIN_ROUNDS, CLOUD_MAX_ROUNDS);
        return 0;
    }
    unsigned long *seqs = malloc((size_t)app->batch_size * sizeof(unsigned long));
    if (!seqs) return 0;
    const char *rig = "cloud:RIG-1";
    printf("=== Cloud mining: renting %d round(s), difficulty %d ===\n", rounds, app->difficulty);
    printf("  Rented hash power %d attempts/round; rental fee %.2f per round; maintenance %.0f%% of rewards\n",
           CLOUD_HASHRATE, CLOUD_RENTAL_FEE, CLOUD_MAINTENANCE_PERCENT);

    Block *b = NULL;                 /* block being mined; its proof of work carries across rounds */
    double cum_reward = 0, cum_fees = 0;
    int total_blocks = 0, warned = 0;
    for (int r = 1; r <= rounds; r++) {
        unsigned long budget = CLOUD_HASHRATE, used = 0;
        double reward = 0;
        int blocks = 0;
        printf("\n  -- Round %d --\n", r);
        while (budget > 0) {
            if (!b) {
                b = build_block(app, rig, seqs);
                if (!b) break;   /* nothing (more) to mine */
            }
            unsigned long a = 0;
            int found = block_pow(b, budget, &a);
            budget -= a;
            used += a;
            if (!found) break;   /* out of hash power this round */
            printf("  Block %d sealed by %s (nonce %lu)\n", b->index, rig, b->nonce);
            long fees = confirm_block(app, b, seqs);
            b = NULL;
            reward += app->block_reward + (double)fees;
            blocks++;
        }
        double maintenance = reward * CLOUD_MAINTENANCE_PERCENT / 100.0;
        double round_fees = CLOUD_RENTAL_FEE + maintenance;
        cum_reward += reward;
        cum_fees += round_fees;
        total_blocks += blocks;
        printf("  Round %d: %lu attempts used, %d block(s), reward %.2f, rental %.2f, maintenance %.2f\n",
               r, used, blocks, reward, CLOUD_RENTAL_FEE, maintenance);
        printf("           cumulative rewards %.2f, cumulative fees %.2f, running net %.2f\n",
               cum_reward, cum_fees, cum_reward - cum_fees);
        if (cum_fees > cum_reward) {
            warned = 1;
            printf("           WARNING: rental is unprofitable - cumulative fees (%.2f) exceed cumulative "
                   "rewards (%.2f)\n", cum_fees, cum_reward);
        }
    }
    if (b) {
        printf("\n  The block being mined when the rental ended was not sealed; its requests stay in the pool.\n");
        block_free(b);
    }
    pay_miner(app, "cloud:renter", cum_reward - cum_fees, total_blocks);

    printf("\n  --- Cloud mining summary (%d round%s) ---\n", rounds, rounds == 1 ? "" : "s");
    printf("  Blocks confirmed : %d\n", total_blocks);
    printf("  Gross earnings   : %.2f coins\n", cum_reward);
    printf("  Total fees paid  : %.2f coins (rental %.2f + maintenance %.2f)\n", cum_fees,
           CLOUD_RENTAL_FEE * rounds, cum_fees - CLOUD_RENTAL_FEE * rounds);
    printf("  Net profit       : %.2f coins\n", cum_reward - cum_fees);
    if (cum_reward - cum_fees < 0) printf("  WARNING: this rental was UNPROFITABLE overall.\n");
    else if (warned) printf("  Note: unprofitable during at least one round, but ended in profit.\n");
    lending_prune_pool(app);
    free(seqs);
    return 1;
}
