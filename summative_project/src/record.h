#ifndef RECORD_H
#define RECORD_H

#include <stddef.h>
#include <time.h>

#include "crypto.h"
#include "registry.h"

#define RECORD_ID_LEN  17   /* 16 hex characters + NUL                      */
#define COND_LEN        8
#define REASON_LEN     24

/* The five actions named in the brief, plus three the system also needs to
 * put on the chain: LOST (librarian override), FINE_PAID (settle_fine) and
 * TRANSFER (token transfers, which must also go through the pending pool). */
typedef enum {
    ACT_BORROWED = 0, ACT_RETURNED, ACT_RESERVED, ACT_RENEWED, ACT_OVERDUE,
    ACT_LOST, ACT_FINE_PAID, ACT_TRANSFER, ACT_COUNT
} Action;

/* One lending record (transaction). Every field except the signature is
 * covered by the signature; every field including the signature is covered
 * by the record hash, which is a leaf of the block's Merkle tree. */
typedef struct {
    char   record_id[RECORD_ID_LEN];   /* first 16 hex of SHA-256(content)      */
    time_t timestamp;                  /* when the request was created          */
    Action action;
    char   book_id[ID_LEN];
    char   copy_id[COPY_ID_LEN];
    char   book_title[TITLE_LEN];      /* copied from the registry              */
    char   member_id[ID_LEN];          /* the member the record is about        */
    char   member_name[NAME_LEN];      /* copied from the registry              */
    time_t due_date;                   /* BORROWED / RENEWED: the (new) due date */
    long   fine_amount;                /* late RETURNED or OVERDUE, else 0      */
    int    token_reward;               /* RETURNED: 10 on time, 5 late          */
    char   condition[COND_LEN];        /* RETURNED / LOST: condition of the copy */
    char   reason[REASON_LEN];         /* librarian override reason code        */
    char   counterparty[ID_LEN];       /* TRANSFER: recipient                    */
    long   amount;                     /* TRANSFER / FINE_PAID: coins           */
    char   signer_id[ID_LEN];          /* member_id, or LIBRARIAN               */
    long   nonce;                      /* signer's nonce (replay protection)    */
    char   pubkey[PUBKEY_HEX_LEN];     /* signer's public key = identity        */
    unsigned char signature[MAX_SIG_LEN];
    size_t sig_len;
} Record;

const char *action_name(Action a);
int action_parse(const char *s, Action *out);

/* The canonical text that is hashed and signed (every field but record_id and
 * signature, in a fixed order, '|'-separated). */
void record_content(const Record *r, char *out, size_t cap);

/* Computes record_id from the content, then signs it with `priv`. The signer
 * fields (signer_id, nonce, pubkey) must already be set. Returns 1 on success. */
int record_sign(Record *r, EVP_PKEY *priv);

/* Checks that record_id matches the content and the signature verifies
 * against the public key in the record. Returns 1 if both hold. */
int record_verify(const Record *r);

/* SHA-256 leaf hash used in the Merkle tree: covers content and signature. */
void record_hash(const Record *r, char out[HASH_HEX_LEN]);

/* SHA-256 identifier of the token-reward transaction a RETURNED record
 * creates. Empty string when the record carries no reward. */
void record_reward_txid(const Record *r, char out[HASH_HEX_LEN]);

/* One-line text form used in chain.dat and pool.dat, and its parser. */
void record_to_line(const Record *r, char *out, size_t cap);
int record_from_line(char *line, Record *r);

#define RECORD_LINE_LEN 1400

#endif
