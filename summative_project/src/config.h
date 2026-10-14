#ifndef CONFIG_H
#define CONFIG_H

/* Every tunable number in the system lives here, so the report and README can
 * point at a single file. Amounts of coins and fines are whole numbers. */

/* ---- Lending rules ------------------------------------------------------- */
#define LOAN_DAYS               14   /* due date = borrow time + this          */
#define RENEW_DAYS               7   /* a renewal extends the due date by this */
#define MAX_RENEWALS             2   /* renewals allowed per loan              */
#define FINE_PER_DAY             2   /* coins of fine per full day late        */
#define SECONDS_PER_DAY      86400L

/* ---- Misuse / fraud heuristics ------------------------------------------- */
#define MAX_CONCURRENT_BORROWS   3   /* copies one member may hold at once     */
#define REBORROW_WINDOW_SECONDS (24L * 3600L) /* same title re-borrowed sooner = suspicious */

/* ---- Token rewards (from Formative 2) ------------------------------------ */
#define REWARD_ON_TIME          10
#define REWARD_LATE              5
#define TX_FEE                   1   /* fixed fee on every token transaction   */

/* ---- Blocks and proof of work -------------------------------------------- */
#define DEFAULT_DIFFICULTY       2
#define MIN_DIFFICULTY           1
#define MAX_DIFFICULTY           4
#define DEFAULT_BATCH_SIZE       4   /* records per block (fixed-size batching) */
#define MIN_BATCH_SIZE           1
#define MAX_BATCH_SIZE          16
#define DEFAULT_BLOCK_REWARD    50   /* coins paid to whoever seals a block    */

/* ---- Mining economics ----------------------------------------------------- */
#define POOL_FEE_PERCENT       2.0
#define POOL_DEFAULT_MINERS      4
#define POOL_MIN_MINERS          2
#define POOL_MAX_MINERS          8
#define POOL_MIN_HASHRATE       50   /* attempts per round, chosen at random   */
#define POOL_MAX_HASHRATE      400
#define CLOUD_MIN_ROUNDS         1
#define CLOUD_MAX_ROUNDS         5
#define CLOUD_RENTAL_FEE      20.0   /* per round                              */
#define CLOUD_MAINTENANCE_PERCENT 10.0
#define CLOUD_HASHRATE        2000   /* attempts per rented round              */

/* ---- Capacity limits (fixed arrays keep state easy to copy and save) ----- */
#define MAX_BOOKS              128
#define MAX_COPIES             512
#define MAX_MEMBERS            128
#define MAX_QUEUE               16   /* reservations waiting per title         */
#define MAX_RETURN_EVENTS     1024

/* ---- File locations ------------------------------------------------------- */
#define BOOKS_PATH    "data/books.txt"
#define MEMBERS_PATH  "data/members.txt"
#define COPIES_PATH   "data/copies.txt"
#define CHAIN_PATH    "data/chain.dat"
#define STATE_PATH    "data/state.dat"
#define POOL_PATH     "data/pool.dat"
#define KEYS_DIR      "keys"

/* The librarian node signs system-generated records (OVERDUE, LOST). */
#define LIBRARIAN_ID  "LIBRARIAN"

#endif
