#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <openssl/sha.h>

#define MAX_TX_LEN 256
#define HASH_HEX_LEN 65 // 64 hex characters + null terminator

// Block Structure representing the ledger payload
typedef struct {
    int index;
    time_t timestamp;
    char transaction[MAX_TX_LEN];
    char prev_hash[HASH_HEX_LEN];
    char current_hash[HASH_HEX_LEN];
    unsigned long long nonce;
} Block;

// Function Prototypes
void compute_sha256(const char *input, char output_hex[HASH_HEX_LEN]);
int meets_difficulty(const char *hash, int difficulty);
void mine_block(Block *block, int difficulty);
int validate_block(const Block *block, int difficulty);
void update_ledger(const Block *block);

// Calculates double SHA-256 hash string from input
void compute_sha256(const char *input, char output_hex[HASH_HEX_LEN]) {
    unsigned char hash[SHA256_DIGEST_LENGTH];
    SHA256_CTX sha256;
    
    SHA256_Init(&sha256);
    SHA256_Update(&sha256, input, strlen(input));
    SHA256_Final(hash, &sha256);

    for (int i = 0; i < SHA256_DIGEST_LENGTH; i++) {
        sprintf(output_hex + (i * 2), "%02x", hash[i]);
    }
    output_hex[64] = '\0';
}

// Checks if the hash string has the required number of leading zeros
int meets_difficulty(const char *hash, int difficulty) {
    for (int i = 0; i < difficulty; i++) {
        if (hash[i] != '0') {
            return 0;
        }
    }
    return 1;
}

// Simulates miner brute-forcing the nonce until target condition is met
void mine_block(Block *block, int difficulty) {
    char header_buffer[1024];
    block->nonce = 0;

    printf("\n[Step 1] Miner attempting nonce guesses for Block #%d (Difficulty: %d)...\n", 
           block->index, difficulty);

    while (1) {
        // Construct block header string: Index + Timestamp + Transaction + PrevHash + Nonce
        snprintf(header_buffer, sizeof(header_buffer), "%d%ld%s%s%llu",
                 block->index, block->timestamp, block->transaction, 
                 block->prev_hash, block->nonce);

        compute_sha256(header_buffer, block->current_hash);

        if (meets_difficulty(block->current_hash, difficulty)) {
            printf("[+] Block successfully mined!\n");
            printf("    Nonce required: %llu iterations\n", block->nonce);
            printf("    Mined Hash:     %s\n", block->current_hash);
            break;
        }
        block->nonce++;
    }
}

// Validates block header hash and difficulty target on network nodes
int validate_block(const Block *block, int difficulty) {
    char header_buffer[1024];
    char recomputed_hash[HASH_HEX_LEN];

    snprintf(header_buffer, sizeof(header_buffer), "%d%ld%s%s%llu",
             block->index, block->timestamp, block->transaction, 
             block->prev_hash, block->nonce);

    compute_sha256(header_buffer, recomputed_hash);

    // Verify hash integrity and difficulty threshold
    if (strcmp(recomputed_hash, block->current_hash) == 0 && 
        meets_difficulty(recomputed_hash, difficulty)) {
        return 1;
    }
    return 0;
}

// Updates local ledger state
void update_ledger(const Block *block) {
    printf("[+] Ledger Updated: Block #%d appended to local chain.\n", block->index);
}

int main(void) {
    int difficulty = 4; // Target requires 4 leading zeros

    // Step 2: Group pending transaction into candidate block
    Block new_block;
    new_block.index = 1;
    new_block.timestamp = time(NULL);
    strncpy(new_block.transaction, "Alice pays Bob 1.5 BTC", MAX_TX_LEN);
    strncpy(new_block.prev_hash, "0000000000000000000000000000000000000000000000000000000000000000", HASH_HEX_LEN);

    printf("--- BLOCKCHAIN MINING SIMULATION ---\n");
    printf("Pending Transaction: \"%s\"\n", new_block.transaction);

    // Step 1-3: Mine candidate block
    mine_block(&new_block, difficulty);

    // Step 4: Broadcast block to network nodes
    printf("\n[Step 4] Broadcasting Block #%d to peer nodes...\n", new_block.index);

    // Step 5: Network validation process
    printf("[Step 5] Network nodes validating block...\n");
    if (validate_block(&new_block, difficulty)) {
        printf("    Validation Result: VALID BLOCK\n");

        // Step 6: All validating nodes update local copy of ledger
        printf("\n[Step 6] Propagating block across network...\n");
        update_ledger(&new_block);
    } else {
        printf("    Validation Result: INVALID BLOCK (Rejected)\n");
    }

    return 0;
}
