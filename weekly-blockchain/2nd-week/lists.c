#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Structure representing a single block in the linked list
typedef struct Block {
    int index;
    char data[256];
    char hash[65];
    char prev_hash[65];
    struct Block *next;
} Block;

// Helper function to generate a mock hash (in production, use SHA-256)
void generate_hash(int index, const char *data, const char *prev_hash, char *output_hash) {
    // Basic hash formatting simulation
    snprintf(output_hash, 65, "HASH_IDX%d_%s", index, data);
}

// Function to initialize and return the Genesis Block
Block *create_genesis_block(void) {
    Block *genesis = (Block *)malloc(sizeof(Block));
    if (!genesis) {
        perror("Allocation failed");
        return NULL;
    }

    genesis->index = 0;
    strncpy(genesis->data, "Genesis Block", sizeof(genesis->data) - 1);
    
    // Parent hash set to zeroes for the genesis block
    strncpy(genesis->prev_hash, "00000000", sizeof(genesis->prev_hash) - 1);
    
    generate_hash(genesis->index, genesis->data, genesis->prev_hash, genesis->hash);
    genesis->next = NULL;

    return genesis;
}

// Function to create and link a new block from the previous block
Block *add_block(Block *prev_block, const char *data) {
    if (!prev_block) return NULL;

    Block *new_block = (Block *)malloc(sizeof(Block));
    if (!new_block) {
        perror("Allocation failed");
        return NULL;
    }

    new_block->index = prev_block->index + 1;
    strncpy(new_block->data, data, sizeof(new_block->data) - 1);
    
    // Set current block's prev_hash to the parent block's hash
    strncpy(new_block->prev_hash, prev_block->hash, sizeof(new_block->prev_hash) - 1);
    
    generate_hash(new_block->index, new_block->data, new_block->prev_hash, new_block->hash);
    new_block->next = NULL;

    // Link the previous block to the new block
    prev_block->next = new_block;

    return new_block;
}

// Utility function to print the blockchain
void print_blockchain(const Block *head) {
    const Block *current = head;
    while (current != NULL) {
        printf("Index: %d\n", current->index);
        printf("Data: %s\n", current->data);
        printf("Hash: %s\n", current->hash);
        printf("Parent Hash: %s\n", current->prev_hash);
        printf("----------------------------------------\n");
        current = current->next;
    }
}

// Memory cleanup utility
void free_blockchain(Block *head) {
    Block *temp;
    while (head != NULL) {
        temp = head;
        head = head->next;
        free(temp);
    }
}

int main(void) {
    // 1. Create the Genesis Block
    Block *genesis = create_genesis_block();

    // 2. Add subsequent blocks linked from the genesis block
    Block *block1 = add_block(genesis, "Transaction 1: Alice -> Bob");
    Block *block2 = add_block(block1, "Transaction 2: Bob -> Charlie");

    // 3. Display the chain state
    print_blockchain(genesis);

    // 4. Free allocated memory
    free_blockchain(genesis);

    return 0;
}
