#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

// Data structure representing a single transaction
typedef struct {
    char sender[64];
    char recipient[64];
    double amount;
    double fee;
    time_t timestamp;
    char signature[128];
    char tx_hash[65];
} Transaction;

// Data structure representing a Block in the Blockchain
typedef struct Block {
    int index;
    time_t timestamp;
    Transaction tx;
    char prev_hash[65];
    char block_hash[65];
    int nonce;
    struct Block* next;
} Block;

// Helper function to generate a simple hash string for demonstration
void compute_hash(const char* data, char* out_hash) {
    unsigned long hash = 5381;
    int c;
    while ((c = *data++)) {
        hash = ((hash << 5) + hash) + c;
    }
    snprintf(out_hash, 65, "0x%016lx", hash);
}

// 1. Initiation
Transaction initiate_transaction(const char* sender, const char* recipient, double amount, double fee) {
    Transaction tx;
    strncpy(tx.sender, sender, sizeof(tx.sender) - 1);
    strncpy(tx.recipient, recipient, sizeof(tx.recipient) - 1);
    tx.amount = amount;
    tx.fee = fee;
    tx.timestamp = time(NULL);

    // Simulate cryptographic signing using sender's private key
    snprintf(tx.signature, sizeof(tx.signature), "SIG_PRIVKEY_%s_%ld", sender, tx.timestamp);

    // Compute hash of transaction payload
    char payload[256];
    snprintf(payload, sizeof(payload), "%s:%s:%.2f:%.2f:%ld", 
             tx.sender, tx.recipient, tx.amount, tx.fee, tx.timestamp);
    compute_hash(payload, tx.tx_hash);

    printf("[1. Initiation] Transaction created and signed by sender.\n");
    printf("    Sender: %s | Recipient: %s | Amount: %.2f | Fee: %.4f\n", 
           tx.sender, tx.recipient, tx.amount, tx.fee);
    printf("    Tx Hash: %s\n", tx.tx_hash);
    return tx;
}

// 2. Propagation
void propagate_transaction(const Transaction* tx) {
    printf("[2. Propagation] Broadcasting transaction %s to peer-to-peer network nodes...\n", tx->tx_hash);
    printf("    Transaction replicated across 4 peer nodes.\n");
}

// 3. Verification
int verify_transaction(const Transaction* tx) {
    printf("[3. Verification] Nodes checking digital signature and parameter validity...\n");
    
    if (tx->amount <= 0) {
        printf("    Validation Failed: Amount must be greater than 0.\n");
        return 0;
    }
    if (strncmp(tx->signature, "SIG_PRIVKEY_", 12) != 0) {
        printf("    Validation Failed: Signature verification failed.\n");
        return 0;
    }
    
    printf("    Signature verified with Public Key. Transaction is valid.\n");
    return 1;
}

// 4. Consensus Mechanisms
int run_consensus(const Transaction* tx) {
    printf("[4. Consensus Mechanisms] Reaching consensus across validator nodes...\n");
    
    int votes = 0;
    int total_nodes = 5;
    
    for (int i = 0; i < total_nodes; i++) {
        votes++; // Simulated node approval
    }

    if (votes >= 3) {
        printf("    Consensus reached (%d/%d nodes approved block candidate).\n", votes, total_nodes);
        return 1;
    }
    return 0;
}

// 5. Block Addition
Block* add_block_to_chain(Block* head, Transaction tx) {
    printf("[5. Block Addition] Appending verified block to the blockchain...\n");
    
    Block* new_block = (Block*)malloc(sizeof(Block));
    if (!new_block) {
        perror("Memory allocation failed");
        exit(EXIT_FAILURE);
    }

    int new_index = 0;
    char prev_hash[65] = "0x0000000000000000";

    if (head != NULL) {
        Block* current = head;
        while (current->next != NULL) {
            current = current->next;
        }
        new_index = current->index + 1;
        strncpy(prev_hash, current->block_hash, sizeof(prev_hash));
        current->next = new_block;
    }

    new_block->index = new_index;
    new_block->timestamp = time(NULL);
    new_block->tx = tx;
    strncpy(new_block->prev_hash, prev_hash, sizeof(new_block->prev_hash));
    new_block->nonce = 1001;
    new_block->next = NULL;

    // Generate block hash linking previous block
    char block_data[512];
    snprintf(block_data, sizeof(block_data), "%d:%ld:%s:%s:%d", 
             new_block->index, new_block->timestamp, tx.tx_hash, new_block->prev_hash, new_block->nonce);
    compute_hash(block_data, new_block->block_hash);

    printf("    Block #%d linked to chain. Block Hash: %s\n", new_block->index, new_block->block_hash);
    return head == NULL ? new_block : head;
}

// 6. Confirmation
void confirm_transaction(const Block* block) {
    printf("[6. Confirmation] Transaction state finalized and ledger updated.\n");
    printf("    Confirmed Tx Hash : %s\n", block->tx.tx_hash);
    printf("    Transfer          : %.2f tokens from %s to %s\n", 
           block->tx.amount, block->tx.sender, block->tx.recipient);
    printf("    Status            : SUCCESS / CONFIRMED\n");
}

int main(void) {
    Block* blockchain = NULL;

    printf("=== BLOCKCHAIN TRANSACTION SIMULATOR ===\n\n");

    // Execution Stage 1: Initiation
    Transaction tx = initiate_transaction("0xAliceAddress789", "0xBobAddress456", 250.75, 0.0015);
    printf("\n");

    // Execution Stage 2: Propagation
    propagate_transaction(&tx);
    printf("\n");

    // Execution Stage 3: Verification
    if (!verify_transaction(&tx)) {
        printf("Transaction aborted during verification.\n");
        return 1;
    }
    printf("\n");

    // Execution Stage 4: Consensus
    if (!run_consensus(&tx)) {
        printf("Transaction aborted: Consensus not reached.\n");
        return 1;
    }
    printf("\n");

    // Execution Stage 5: Block Addition
    blockchain = add_block_to_chain(blockchain, tx);
    printf("\n");

    // Execution Stage 6: Confirmation
    Block* latest = blockchain;
    while (latest->next != NULL) {
        latest = latest->next;
    }
    confirm_transaction(latest);

    // Free memory
    while (blockchain != NULL) {
        Block* temp = blockchain;
        blockchain = blockchain->next;
        free(temp);
    }

    return 0;
}
