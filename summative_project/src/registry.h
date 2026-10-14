#ifndef REGISTRY_H
#define REGISTRY_H

#include <stddef.h>
#include <openssl/evp.h>

#include "config.h"
#include "crypto.h"
#include "util.h"

/* Book and Member keep the exact fields required by the brief (Formative 1).
 * Members additionally carry their ECDSA identity. */
typedef struct {
    char book_id[ID_LEN];
    char title[TITLE_LEN];
    char author[AUTHOR_LEN];
} Book;

typedef struct {
    char member_id[ID_LEN];
    char full_name[NAME_LEN];
    char course_code[COURSE_LEN];
    char pubkey_hex[PUBKEY_HEX_LEN];  /* on-chain identity (compressed P-256 point) */
    EVP_PKEY *key;                    /* simulated member wallet: private key      */
} Member;

/* Physical copies are registered in copies.txt (copy_id, book_id, condition).
 * Their changing state (who holds them, due date ...) lives in LibState. */
typedef enum { COND_NEW = 0, COND_GOOD, COND_WORN, COND_LOST, COND_COUNT } Condition;

typedef struct {
    char copy_id[COPY_ID_LEN];
    char book_id[ID_LEN];
    Condition condition;              /* condition when registered             */
} CopyInfo;

typedef struct {
    Book books[MAX_BOOKS];
    size_t book_count;
    Member members[MAX_MEMBERS];
    size_t member_count;
    CopyInfo copies[MAX_COPIES];
    size_t copy_count;
    /* The librarian node: signs OVERDUE and LOST records. */
    char librarian_pubkey[PUBKEY_HEX_LEN];
    EVP_PKEY *librarian_key;
} Registry;

/* Loads books.txt, members.txt and copies.txt. Prints a specific error and
 * returns 0 if any file is missing, empty, or has no valid lines. Loads (or
 * creates) every member's key pair and the librarian's. */
int registry_load(Registry *reg);
void registry_free(Registry *reg);

const Book *registry_book(const Registry *reg, const char *book_id);
const Member *registry_member(const Registry *reg, const char *member_id);
const CopyInfo *registry_copy(const Registry *reg, const char *copy_id);

/* Public key / private key for a signer (a member ID or LIBRARIAN_ID). */
const char *registry_pubkey(const Registry *reg, const char *signer_id);
EVP_PKEY *registry_private_key(const Registry *reg, const char *signer_id);

/* Runtime registration: validates the input, appends to the registry file
 * and to memory. Returns 1 on success, 0 with a message in err. */
int registry_add_member(Registry *reg, const char *id, const char *name, const char *course,
                        char *err, size_t errcap);
int registry_add_book(Registry *reg, const char *id, const char *title, const char *author,
                      char *err, size_t errcap);
/* Copy IDs are generated as <book_id>-C<n>. The new ID is written to new_id. */
int registry_add_copy(Registry *reg, const char *book_id, Condition cond,
                      char new_id[COPY_ID_LEN], char *err, size_t errcap);

const char *condition_name(Condition c);
int condition_parse(const char *s, Condition *out);

#endif
