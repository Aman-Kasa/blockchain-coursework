#ifndef CHAIN_H
#define CHAIN_H

#include <stddef.h>
#include <time.h>

#include "record.h"
#include "registry.h"

#define SEALED_BY_LEN 32

/* A block holds a batch of lending records. The header fields are the ones
 * the brief requires, plus difficulty and nonce for proof of work. */
typedef struct Block {
    int    index;
    time_t timestamp;                     /* strictly greater than the previous block's */
    int    record_count;
    char   previous_hash[HASH_HEX_LEN];
    char   merkle_root[HASH_HEX_LEN];
    char   hash[HASH_HEX_LEN];
    char   sealed_by[SEALED_BY_LEN];      /* miner / node that mined the block       */
    int    token_reward;                  /* sum of member rewards in this block     */
    char   transaction_id[HASH_HEX_LEN];  /* SHA-256 over the block's reward tx IDs  */
    int    difficulty;
    unsigned long nonce;
    Record *records;                      /* record_count records, heap-allocated    */
    struct Block *next;
} Block;

typedef struct {
    Block *head;
    Block *tail;
    size_t length;
} Chain;

typedef struct {
    int valid;
    int bad_index;         /* first failing block, -1 if valid           */
    char reason[160];
} ChainCheck;

void chain_init(Chain *c);
void chain_free(Chain *c);
void block_free(Block *b);

/* Allocates a new block that will follow the current tail, holding a copy
 * of `n` records. Computes record_count, merkle_root, token_reward and
 * transaction_id. Nonce starts at 0; the hash is empty until mined. */
Block *block_prepare(const Chain *c, const Record *recs, int n, int difficulty,
                     const char *sealed_by, time_t now);

/* Header hash: SHA-256 of every header field (index, timestamp, record_count,
 * previous_hash, merkle_root, sealed_by, token_reward, transaction_id,
 * difficulty, nonce). */
void block_header_hash(const Block *b, char out[HASH_HEX_LEN]);
void block_merkle(const Block *b, char out[HASH_HEX_LEN]);
void block_reward_txid(const Block *b, int *reward, char out[HASH_HEX_LEN]);

int hash_meets_difficulty(const char *hash, int difficulty);

/* Tries nonces from b->nonce, at most max_attempts times. On success b->hash
 * meets the difficulty and 1 is returned; *attempts counts hashes computed.
 * On failure the nonce is left at the next untried value. */
int block_pow(Block *b, unsigned long max_attempts, unsigned long *attempts);

void chain_append(Chain *c, Block *b);

/* Mines the genesis block (index 0, previous_hash = 64 zeros, no records). */
int chain_create_genesis(Chain *c, int difficulty);

/* Full structural and cryptographic check of every block:
 *   index sequence, previous_hash linkage, strictly increasing timestamps,
 *   record_count, every record's ID and ECDSA signature, that every signer's
 *   public key matches the registry, Merkle root recomputation, token_reward
 *   and transaction_id, header hash recomputation and proof of work.
 * Stops at the first problem. (Whether each record is a legal state change
 * is checked separately by replaying the chain into a fresh LibState.) */
ChainCheck chain_validate(const Chain *c, const Registry *reg);

/* chain.dat: one "B|..." header line per block followed by its "R|..." lines. */
int chain_save(const Chain *c, const char *path);
/* Returns 1 loaded, 0 file missing, -1 corrupt. `c` must be empty. */
int chain_load(Chain *c, const char *path);

const Block *chain_block(const Chain *c, int index);

#endif
