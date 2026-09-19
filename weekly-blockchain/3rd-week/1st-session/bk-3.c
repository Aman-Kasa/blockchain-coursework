#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define MAX_SENDER 50
#define MAX_RECIPIENT 50
#define HASH_LEN 65

// Transaction structure
typedef struct {
    char sender[MAX_SENDER];
    char recipient[MAX_RECIPIENT];
    double amount;
} Transaction;

// Block structure
typedef struct Block {
    int index;
    long timestamp;
    Transaction transaction;
    int nonce;
    char prev_hash[HASH_LEN];
    char hash[HASH_LEN];
    struct Block *next;
} Block;

// Simple custom djb2-based hash function for simulation purposes
void compute_hash(int index, long timestamp, Transaction tx, int nonce, const char *prev_hash, char *output_hash) {
    char input[512];
    snprintf(input, sizeof(input), "%d%ld%s%s%.2f%d%s",
             index, timestamp, tx.sender, tx.recipient, tx.amount, nonce, prev_hash);

    unsigned long hash = 5381;
    for (int i = 0; input[i] != '\0'; i++) {
        hash = ((hash << 5) + hash) + input[i]; // hash * 33 + c
    }

    snprintf(output_hash, HASH_LEN, "%016lx%016lx", hash, hash ^ 0xDEADBEEF);
}

// Function to mine a block (Proof of Work simulation)
void mine_block(Block *b, int difficulty) {
    char target[HASH_LEN] = {0};
    memset(target, '0', difficulty); // e.g., difficulty = 2 requires hash starting with "00"

    b->nonce = 0;
    do {
        b->nonce++;
        compute_hash(b->index, b->timestamp, b->transaction, b->nonce, b->prev_hash, b->hash);
    } while (strncmp(b->hash, target, difficulty) != 0);

    printf("Block %d mined! Nonce: %d | Hash: %s\n", b->index, b->nonce, b->hash);
}

// Function to create a new block
Block* create_block(int index, Transaction tx, const char *prev_hash) {
    Block *new_block = (Block*)malloc(sizeof(Block));
    if (!new_block) {
        perror("Memory allocation failed");
        exit(EXIT_FAILURE);
    }

    new_block->index = index;
    new_block->timestamp = time(NULL);
    new_block->transaction = tx;
    strncpy(new_block->prev_hash, prev_hash, HASH_LEN);
    new_block->next = NULL;

    // Mine the block with difficulty level 2
    mine_block(new_block, 2);

    return new_block;
}

// Function to display the entire blockchain
void display_chain(Block *head) {
    Block *current = head;
    printf("\n=================== BLOCKCHAIN LEDGER ===================\n");
    while (current != NULL) {
        printf("---------------------------------------------------------\n");
        printf("Block Index   : %d\n", current->index);
        printf("Timestamp     : %ld\n", current->timestamp);
        printf("Sender        : %s\n", current->transaction.sender);
        printf("Recipient     : %s\n", current->transaction.recipient);
        printf("Amount        : %.2f BTC\n", current->transaction.amount);
        printf("Nonce         : %d\n", current->nonce);
        printf("Previous Hash : %s\n", current->prev_hash);
        printf("Current Hash  : %s\n", current->hash);
        current = current->next;
    }
    printf("=========================================================\n");
}

// Function to validate the blockchain integrity
int is_chain_valid(Block *head) {
    Block *current = head;
    Block *prev = NULL;

    while (current != NULL) {
        char temp_hash[HASH_LEN];
        compute_hash(current->index, current->timestamp, current->transaction, current->nonce, current->prev_hash, temp_hash);

        // Check hash validity
        if (strcmp(current->hash, temp_hash) != 0) {
            return 0;
        }

        // Check linkage to previous block
        if (prev != NULL && strcmp(current->prev_hash, prev->hash) != 0) {
            return 0;
        }

        prev = current;
        current = current->next;
    }
    return 1;
}

// Memory cleanup
void free_chain(Block *head) {
    Block *temp;
    while (head != NULL) {
        temp = head;
        head = head->next;
        free(temp);
    }
}

int main(void) {
    // 1. Create Genesis Block
    Transaction genesis_tx = {"Network", "Alice", 50.0};
