#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

#define MAX_UTXOS 100
#define ADDR_LEN 32

typedef struct {
    int tx_id;
    char address[ADDR_LEN];
    double amount;
    bool is_spent;
} UTXO;

UTXO utxo_pool[MAX_UTXOS];
int utxo_count = 0;
int next_tx_id = 1;

void add_utxo(const char *address, double amount) {
    if (utxo_count >= MAX_UTXOS) return;
    utxo_pool[utxo_count].tx_id = next_tx_id++;
    strncpy(utxo_pool[utxo_count].address, address, ADDR_LEN - 1);
    utxo_pool[utxo_count].address[ADDR_LEN - 1] = '\0';
    utxo_pool[utxo_count].amount = amount;
    utxo_pool[utxo_count].is_spent = false;
    utxo_count++;
}

void print_utxos(void) {
    printf("\n%-6s | %-12s | %-8s | %-8s\n", "TX ID", "Receiver", "Amount", "Status");
    printf("-----------------------------------------\n");
    for (int i = 0; i < utxo_count; i++) {
        printf("%-6d | %-12s | %-8.2f | %-8s\n",
               utxo_pool[i].tx_id,
               utxo_pool[i].address,
               utxo_pool[i].amount,
               utxo_pool[i].is_spent ? "SPENT" : "UNSPENT");
    }
}

bool process_transaction(const char *sender, const char *receiver, double amount) {
    double collected = 0.0;
    int selected_indices[MAX_UTXOS];
    int selected_count = 0;

    // 1. Select unspent UTXOs belonging to sender until amount is covered
    for (int i = 0; i < utxo_count; i++) {
        if (!utxo_pool[i].is_spent && strcmp(utxo_pool[i].address, sender) == 0) {
            collected += utxo_pool[i].amount;
            selected_indices[selected_count++] = i;
            if (collected >= amount) break;
        }
    }

    if (collected < amount) {
        printf("\n[ERROR] Transaction failed: %s has insufficient funds (Required: %.2f, Available: %.2f)\n", 
               sender, amount, collected);
        return false;
    }

    // 2. Mark selected UTXOs as spent
    for (int i = 0; i < selected_count; i++) {
        utxo_pool[selected_indices[i]].is_spent = true;
    }

    // 3. Generate receiver UTXO
    add_utxo(receiver, amount);

    // 4. Generate change UTXO for sender (if any)
    double change = collected - amount;
    if (change > 0.0) {
        add_utxo(sender, change);
    }

    printf("\n[SUCCESS] %s sent %.2f to %s (Change returned: %.2f)\n", 
           sender, amount, receiver, change);
    return true;
}

int main(void) {
    // Populate initial state
    add_utxo("Alice", 10.0);
    add_utxo("Alice", 5.0);
    add_utxo("Bob", 8.0);

    print_utxos();

    // Alice sends 12.0 to Bob (consumes 10.0 + 5.0 UTXOs; generates 12.0 for Bob and 3.0 change for Alice)
    process_transaction("Alice", "Bob", 12.0);
    print_utxos();

    // Bob sends 4.0 to Charlie (consumes 8.0 UTXO; generates 4.0 for Charlie and 4.0 change for Bob)
    process_transaction("Bob", "Charlie", 4.0);
    print_utxos();

    return 0;
}
