#include "merkle.h"
#include "crypto.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* parent = SHA-256(bytes(left) || bytes(right)) */
static void hash_pair(const char *left, const char *right, char out[HASH_HEX_LEN]) {
    unsigned char buf[64];
    hex_to_bytes(left, buf, 32);
    hex_to_bytes(right, buf + 32, 32);
    sha256_hex(buf, sizeof(buf), out);
}

/* Replaces level[0..n) with the next level up; returns the new count. */
static size_t next_level(char (*level)[HASH_HEX_LEN], size_t n) {
    size_t m = 0;
    for (size_t i = 0; i < n; i += 2) {
        const char *left = level[i];
        const char *right = (i + 1 < n) ? level[i + 1] : level[i];   /* odd: duplicate last */
        char parent[HASH_HEX_LEN];
        hash_pair(left, right, parent);
        memcpy(level[m++], parent, HASH_HEX_LEN);
    }
    return m;
}

void merkle_root(char leaves[][HASH_HEX_LEN], size_t n, char out[HASH_HEX_LEN]) {
    if (n == 0) {
        memset(out, '0', HASH_HEX_LEN - 1);
        out[HASH_HEX_LEN - 1] = '\0';
        return;
    }
    char (*level)[HASH_HEX_LEN] = malloc(n * HASH_HEX_LEN);
    if (!level) { out[0] = '\0'; return; }
    memcpy(level, leaves, n * HASH_HEX_LEN);
    while (n > 1) n = next_level(level, n);
    memcpy(out, level[0], HASH_HEX_LEN);
    free(level);
}

void merkle_print(char leaves[][HASH_HEX_LEN], size_t n) {
    if (n == 0) { printf("  (no records: root is 64 zeros)\n"); return; }
    char (*level)[HASH_HEX_LEN] = malloc(n * HASH_HEX_LEN);
    if (!level) return;
    memcpy(level, leaves, n * HASH_HEX_LEN);
    int depth = 0;
    for (;;) {
        printf("  Level %d (%s, %zu node%s):\n", depth, depth == 0 ? "leaves = record hashes" :
               (n == 1 ? "root" : "parents"), n, n == 1 ? "" : "s");
        for (size_t i = 0; i < n; i++) printf("    [%zu] %.16s...\n", i, level[i]);
        if (n == 1) break;
        if (n % 2) printf("    (odd count: [%zu] is paired with itself)\n", n - 1);
        n = next_level(level, n);
        depth++;
    }
    free(level);
}
