/*
 * wallet.c - Simple CLI "Create / Load Wallet" program
 *
 * Features:
 *   1. Create a new wallet   (owner name, generated wallet ID, starting balance)
 *   2. Load an existing wallet from a file
 *   3. Save the current wallet to a file
 *   4. View the current wallet
 *   5. Deposit / 6. Withdraw  (so there is something worth saving)
 *   0. Exit (warns if there are unsaved changes)
 *
 * Money is stored as whole cents (long long) to avoid floating-point
 * rounding errors.
 *
 * Build:  gcc -Wall -Wextra -std=c99 -o wallet wallet.c
 * Run:    ./wallet
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <time.h>

#define NAME_LEN     64
#define ID_LEN       17      /* 16 hex chars + '\0' */
#define FILENAME_LEN 256
#define LINE_LEN     512
#define MAX_BALANCE  100000000000000LL   /* 1 trillion.00 in cents */

typedef struct {
    char      owner[NAME_LEN];
    char      id[ID_LEN];
    long long balance_cents;
} Wallet;

/* ---------- input helpers ---------- */

/* Reads one line from stdin into buf, strips the newline.
 * Returns 1 on success, 0 on EOF/error. Discards extra chars if too long. */
static int read_line(const char *prompt, char *buf, size_t size)
{
    if (prompt) {
        printf("%s", prompt);
        fflush(stdout);
    }
    if (fgets(buf, (int)size, stdin) == NULL)
        return 0;

    char *nl = strchr(buf, '\n');
    if (nl) {
        *nl = '\0';
    } else {
        int c;                      /* line too long: flush the rest */
        while ((c = getchar()) != '\n' && c != EOF)
            ;
    }
    return 1;
}

/* Trims leading and trailing whitespace in place. */
static void trim(char *s)
{
    char *start = s;
    while (isspace((unsigned char)*start))
        start++;
    if (start != s)
        memmove(s, start, strlen(start) + 1);

    size_t len = strlen(s);
    while (len > 0 && isspace((unsigned char)s[len - 1]))
        s[--len] = '\0';
}

/* Parses an amount like "12", "12.5" or "12.50" into cents.
 * Returns 1 on success, 0 if invalid or negative. */
static int parse_amount(const char *text, long long *cents_out)
{
    const char *p = text;
    long long whole = 0, frac = 0;
    int digits = 0, frac_digits = 0;

    if (*p == '\0')
        return 0;

    while (isdigit((unsigned char)*p)) {
        whole = whole * 10 + (*p - '0');
        if (whole > 1000000000000LL)       /* sanity limit */
            return 0;
        p++;
        digits++;
    }
    if (*p == '.') {
        p++;
        while (isdigit((unsigned char)*p)) {
            if (frac_digits == 2)
                return 0;                   /* more than 2 decimals */
            frac = frac * 10 + (*p - '0');
            p++;
            frac_digits++;
        }
    }
    if (*p != '\0' || (digits == 0 && frac_digits == 0))
        return 0;
    if (frac_digits == 1)
        frac *= 10;

    if (whole * 100 + frac > MAX_BALANCE)
        return 0;
    *cents_out = whole * 100 + frac;
    return 1;
}

static void print_money(long long cents)
{
    printf("%lld.%02lld", cents / 100, cents % 100);
}

/* ---------- wallet operations ---------- */

/* Generates a 16-character hex wallet ID.
 * Uses the OS random source (/dev/urandom) when available so that two
 * wallets created in the same second never get the same ID; falls back
 * to rand() on systems without it (e.g. Windows). */
static void generate_id(char *id)
{
    const char hex[] = "0123456789ABCDEF";
    unsigned char bytes[ID_LEN - 1];
    int ok = 0;

    FILE *rng = fopen("/dev/urandom", "rb");
    if (rng) {
        ok = (fread(bytes, 1, sizeof bytes, rng) == sizeof bytes);
        fclose(rng);
    }
    for (int i = 0; i < ID_LEN - 1; i++)
        id[i] = hex[(ok ? bytes[i] : (unsigned)rand()) % 16];
    id[ID_LEN - 1] = '\0';
}

static void view_wallet(const Wallet *w)
{
    printf("\n----- Wallet -----\n");
    printf("Owner   : %s\n", w->owner);
    printf("ID      : %s\n", w->id);
    printf("Balance : ");
    print_money(w->balance_cents);
    printf("\n------------------\n");
}

static int create_wallet(Wallet *w)
{
    Wallet tmp;
    char line[LINE_LEN];

    memset(&tmp, 0, sizeof tmp);

    for (;;) {
        if (!read_line("Enter owner name: ", line, sizeof line))
            return 0;
        trim(line);
        if (line[0] == '\0') {
            printf("Name cannot be empty.\n");
        } else if (strlen(line) >= NAME_LEN) {
            printf("Name too long (max %d characters).\n", NAME_LEN - 1);
        } else {
            break;
        }
    }
    strcpy(tmp.owner, line);

    for (;;) {
        if (!read_line("Enter starting balance (e.g. 100.00): ", line, sizeof line))
            return 0;
        trim(line);
        if (parse_amount(line, &tmp.balance_cents))
            break;
        printf("Invalid amount. Use a non-negative number with up to 2 decimals.\n");
    }

    generate_id(tmp.id);
    *w = tmp;
    printf("Wallet created successfully.\n");
    view_wallet(w);
    return 1;
}

/* File format (plain text, one field per line):
 *   WALLET_V1
 *   owner=<name>
 *   id=<16 hex chars>
 *   balance_cents=<integer>
 */
static int save_wallet(const Wallet *w, const char *filename)
{
    FILE *fp = fopen(filename, "w");
    if (!fp) {
        perror("Error opening file for writing");
        return 0;
    }
    fprintf(fp, "WALLET_V1\n");
    fprintf(fp, "owner=%s\n", w->owner);
    fprintf(fp, "id=%s\n", w->id);
    fprintf(fp, "balance_cents=%lld\n", w->balance_cents);

    if (ferror(fp) || fclose(fp) != 0) {
        fprintf(stderr, "Error writing wallet file.\n");
        return 0;
    }
    printf("Wallet saved to '%s'.\n", filename);
    return 1;
}

static int load_wallet(Wallet *w, const char *filename)
{
    FILE *fp = fopen(filename, "r");
    char line[LINE_LEN];
    Wallet tmp;
    int got_owner = 0, got_id = 0, got_balance = 0, bad = 0;

    if (!fp) {
        perror("Error opening file for reading");
        return 0;
    }
    memset(&tmp, 0, sizeof tmp);

    if (!fgets(line, sizeof line, fp) || strncmp(line, "WALLET_V1", 9) != 0) {
        fprintf(stderr, "Error: '%s' is not a valid wallet file.\n", filename);
        fclose(fp);
        return 0;
    }

    while (fgets(line, sizeof line, fp)) {
        line[strcspn(line, "\r\n")] = '\0';

        if (strncmp(line, "owner=", 6) == 0) {
            const char *v = line + 6;
            if (*v == '\0' || strlen(v) >= NAME_LEN) { bad = 1; break; }
            strcpy(tmp.owner, v);
            got_owner = 1;
        } else if (strncmp(line, "id=", 3) == 0) {
            const char *v = line + 3;
            if (strlen(v) != ID_LEN - 1) { bad = 1; break; }
            strcpy(tmp.id, v);
            got_id = 1;
        } else if (strncmp(line, "balance_cents=", 14) == 0) {
            char *end;
            long long v = strtoll(line + 14, &end, 10);
            if (end == line + 14 || *end != '\0' || v < 0 || v > MAX_BALANCE) {
                bad = 1;
                break;
            }
            tmp.balance_cents = v;
            got_balance = 1;
        }
    }
    fclose(fp);

    if (bad || !got_owner || !got_id || !got_balance) {
        fprintf(stderr, "Error: wallet file '%s' is missing or has invalid fields.\n",
                filename);
        return 0;
    }

    *w = tmp;
    printf("Wallet loaded from '%s'.\n", filename);
    view_wallet(w);
    return 1;
}

static int ask_filename(char *filename)
{
    if (!read_line("Enter file name (e.g. wallet.txt): ", filename, FILENAME_LEN))
        return 0;
    trim(filename);
    if (filename[0] == '\0') {
        printf("File name cannot be empty.\n");
        return 0;
    }
    return 1;
}

static int ask_amount(long long *cents)
{
    char line[LINE_LEN];
    if (!read_line("Enter amount: ", line, sizeof line))
        return 0;
    trim(line);
    if (!parse_amount(line, cents) || *cents == 0) {
        printf("Invalid amount. Use a positive number with up to 2 decimals.\n");
        return 0;
    }
    return 1;
}

/* ---------- main menu ---------- */

static void print_menu(void)
{
    printf("\n===== Wallet Menu =====\n");
    printf("1. Create new wallet\n");
    printf("2. Load wallet from file\n");
    printf("3. Save wallet to file\n");
    printf("4. View wallet\n");
    printf("5. Deposit\n");
    printf("6. Withdraw\n");
    printf("0. Exit\n");
}

int main(void)
{
    Wallet wallet;
    int has_wallet = 0;
    int unsaved = 0;
    char choice[LINE_LEN];
    char filename[FILENAME_LEN];

    srand((unsigned)time(NULL) ^ (unsigned)clock());
    memset(&wallet, 0, sizeof wallet);

    for (;;) {
        print_menu();
        if (!read_line("Choose an option: ", choice, sizeof choice)) {
            printf("\nInput closed. Exiting.\n");
            break;
        }
        trim(choice);

        if (strcmp(choice, "1") == 0) {
            if (has_wallet && unsaved)
                printf("Warning: the current wallet has unsaved changes and will be replaced.\n");
            if (create_wallet(&wallet)) {
                has_wallet = 1;
                unsaved = 1;
            }
        } else if (strcmp(choice, "2") == 0) {
            if (has_wallet && unsaved)
                printf("Warning: the current wallet has unsaved changes and will be replaced.\n");
            if (ask_filename(filename) && load_wallet(&wallet, filename)) {
                has_wallet = 1;
                unsaved = 0;
            }
        } else if (strcmp(choice, "3") == 0) {
            if (!has_wallet)
                printf("No wallet to save. Create or load one first.\n");
            else if (ask_filename(filename) && save_wallet(&wallet, filename))
                unsaved = 0;
        } else if (strcmp(choice, "4") == 0) {
            if (!has_wallet)
                printf("No wallet loaded. Create or load one first.\n");
            else
                view_wallet(&wallet);
        } else if (strcmp(choice, "5") == 0 || strcmp(choice, "6") == 0) {
            long long amount;
            if (!has_wallet) {
                printf("No wallet loaded. Create or load one first.\n");
            } else if (ask_amount(&amount)) {
                if (choice[0] == '5' && amount > MAX_BALANCE - wallet.balance_cents) {
                    printf("Deposit rejected: balance limit exceeded. ");
                } else if (choice[0] == '5') {
                    wallet.balance_cents += amount;
                    unsaved = 1;
                    printf("Deposited. ");
                } else if (amount > wallet.balance_cents) {
                    printf("Insufficient funds. ");
                } else {
                    wallet.balance_cents -= amount;
                    unsaved = 1;
                    printf("Withdrawn. ");
                }
                printf("Balance: ");
                print_money(wallet.balance_cents);
                printf("\n");
            }
        } else if (strcmp(choice, "0") == 0) {
            if (has_wallet && unsaved) {
                char ans[LINE_LEN];
                if (read_line("You have unsaved changes. Save before exiting? (y/n): ",
                              ans, sizeof ans)) {
                    trim(ans);
                    if ((ans[0] == 'y' || ans[0] == 'Y') && ask_filename(filename))
                        save_wallet(&wallet, filename);
                }
            }
            printf("Goodbye!\n");
            break;
        } else {
            printf("Invalid option. Please enter a number from the menu.\n");
        }
    }
    return 0;
}
