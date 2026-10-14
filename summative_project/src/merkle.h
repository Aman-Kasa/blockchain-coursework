#ifndef MERKLE_H
#define MERKLE_H

#include <stddef.h>

#include "util.h"

/* Merkle tree built from scratch (no external library).
 *
 * Leaves are the SHA-256 hashes of the block's records. Each parent is
 * SHA-256(left || right) over the raw 32-byte digests. When a level has an
 * odd number of nodes, the last node is paired with itself (as in Bitcoin).
 * A block with no records has a root of 64 zeros. */
void merkle_root(char leaves[][HASH_HEX_LEN], size_t n, char out[HASH_HEX_LEN]);

/* Prints every level of the tree from leaves to root (for merkle_view). */
void merkle_print(char leaves[][HASH_HEX_LEN], size_t n);

#endif
