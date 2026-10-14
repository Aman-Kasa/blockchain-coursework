#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>

#include "app.h"
#include "cli.h"
#include "persist.h"

static void usage(const char *prog) {
    fprintf(stderr,
            "Usage: %s [--model utxo|account] [--difficulty 1-4] [--batch 1-16] [--seed N]\n"
            "  --model       token ledger model for this session (asked at startup if omitted)\n"
            "  --difficulty  proof-of-work difficulty: leading zero hex characters (default %d)\n"
            "  --batch       lending records per block (default %d)\n"
            "  --seed        random seed for pool-mining hash rates (repeatable demos)\n",
            prog, DEFAULT_DIFFICULTY, DEFAULT_BATCH_SIZE);
}

static int parse_int(const char *s, int lo, int hi, int *out) {
    long v;
    if (!parse_long(s, &v) || v < lo || v > hi) return 0;
    *out = (int)v;
    return 1;
}

static int parse_model(const char *s, LedgerModel *m) {
    if (!strcasecmp(s, "utxo") || !strcmp(s, "1")) { *m = MODEL_UTXO; return 1; }
    if (!strcasecmp(s, "account") || !strcmp(s, "2")) { *m = MODEL_ACCOUNT; return 1; }
    return 0;
}

static LedgerModel ask_model(void) {
    char line[64];
    for (;;) {
        printf("Choose the token ledger model for this session:\n  1) UTXO model\n  2) Account-based model\nChoice [1/2]: ");
        fflush(stdout);
        if (!fgets(line, sizeof(line), stdin)) { printf("\nNo choice: using UTXO.\n"); return MODEL_UTXO; }
        str_trim(line);
        LedgerModel m;
        if (parse_model(line, &m)) return m;
        printf("Please enter 1 or 2.\n");
    }
}

int main(int argc, char **argv) {
    LedgerModel model = MODEL_UTXO;
    int have_model = 0, difficulty = 0, batch = 0, seed = (int)time(NULL);
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--model") && i + 1 < argc) {
            if (!parse_model(argv[++i], &model)) { usage(argv[0]); return 1; }
            have_model = 1;
        } else if (!strcmp(argv[i], "--difficulty") && i + 1 < argc) {
            if (!parse_int(argv[++i], MIN_DIFFICULTY, MAX_DIFFICULTY, &difficulty)) {
                fprintf(stderr, "ERROR: difficulty must be %d-%d.\n", MIN_DIFFICULTY, MAX_DIFFICULTY);
                return 1;
            }
        } else if (!strcmp(argv[i], "--batch") && i + 1 < argc) {
            if (!parse_int(argv[++i], MIN_BATCH_SIZE, MAX_BATCH_SIZE, &batch)) {
                fprintf(stderr, "ERROR: batch size must be %d-%d.\n", MIN_BATCH_SIZE, MAX_BATCH_SIZE);
                return 1;
            }
        } else if (!strcmp(argv[i], "--seed") && i + 1 < argc) {
            if (!parse_int(argv[++i], 0, 2000000000, &seed)) { usage(argv[0]); return 1; }
        } else {
            usage(argv[0]);
            return 1;
        }
    }
    srand((unsigned)seed);

    static App app;   /* large fixed arrays: keep off the stack */
    if (!registry_load(&app.reg)) {
        fprintf(stderr, "FATAL: could not load the registries (books.txt, copies.txt, members.txt). Exiting.\n");
        registry_free(&app.reg);
        return 1;
    }
    printf("Registries loaded: %zu book(s), %zu copies, %zu member(s); every member has an ECDSA key pair.\n",
           app.reg.book_count, app.reg.copy_count, app.reg.member_count);
    if (!have_model) model = ask_model();

    int rc = 1;
    if (persist_load(&app, model, difficulty, batch)) {
        printf("Token ledger: %s model. Difficulty %d, %d records per block.\n",
               ledger_model_name(app.ledger.model), app.difficulty, app.batch_size);
        rc = cli_run(&app);
        if (!app.read_only && !persist_save(&app)) fprintf(stderr, "WARNING: could not save state on exit.\n");
    }
    chain_free(&app.chain);
    pool_free(&app.pool);
    ledger_free(&app.ledger);
    free(app.state);
    registry_free(&app.reg);
    return rc;
}
