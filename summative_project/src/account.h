#ifndef ACCOUNT_H
#define ACCOUNT_H

#include <stddef.h>
#include <time.h>

#include "config.h"
#include "util.h"

/* Account model. Each registered member is an account with a balance that is
 * debited and credited directly, a nonce, and a transaction history kept as
 * a linked list in memory. */

typedef struct TxRecord {
    char   kind[10];               /* REWARD or TRANSFER                   */
    char   sender[ID_LEN];         /* "SYSTEM" for rewards                 */
    char   recipient[ID_LEN];
    long   amount;                 /* what the recipient received          */
    long   fee;
    long   nonce;                  /* sender's nonce; -1 for rewards       */
    time_t timestamp;
    struct TxRecord *next;
} TxRecord;

typedef struct {
    char      member_id[ID_LEN];
    long      balance;
    long      nonce;               /* next nonce: last outgoing nonce + 1  */
    int       outgoing;            /* outgoing transactions sent           */
    TxRecord *history_head;        /* oldest first                         */
    TxRecord *history_tail;
} Account;

typedef struct {
    Account accounts[MAX_MEMBERS];
    size_t  count;
} AccountLedger;

void account_init(AccountLedger *led);
void account_free(AccountLedger *led);
/* Returns the account, creating it (balance 0) on first use. */
Account *account_get(AccountLedger *led, const char *member_id);
const Account *account_find(const AccountLedger *led, const char *member_id);

/* Credits gross - fee to the member. Returns the fee collected. */
long account_credit_reward(AccountLedger *led, const char *member, long gross, time_t ts);

/* Debits amount + fee from the sender, credits amount to the recipient,
 * logs it in both histories and advances the sender's nonce. Rejected (0)
 * if the sender's balance cannot cover amount + fee. */
int account_transfer(AccountLedger *led, const char *from, const char *to, long amount, long nonce,
                     time_t ts, long *fee_out, char *err, size_t errcap, int verbose);

/* Records that the member sent a (non-token) request with this nonce, so the
 * account nonce stays equal to the member's request nonce. */
void account_note_nonce(AccountLedger *led, const char *member, long nonce);

void account_print_history(const AccountLedger *led, const char *member_id);

#endif
