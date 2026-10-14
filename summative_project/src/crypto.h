#ifndef CRYPTO_H
#define CRYPTO_H

#include <stddef.h>
#include <openssl/evp.h>

#include "util.h"

#define PUBKEY_HEX_LEN  67   /* compressed P-256 point: 33 bytes = 66 hex + NUL */
#define MAX_SIG_LEN     72   /* DER-encoded P-256 ECDSA signature, at most 72   */

/* SHA-256 of `len` bytes, written as 64 lowercase hex characters + NUL. */
void sha256_hex(const void *data, size_t len, char out[HASH_HEX_LEN]);
void sha256_str(const char *s, char out[HASH_HEX_LEN]);

/* Loads the ECDSA (NIST P-256) key pair for `owner` from keys/<owner>.pem,
 * creating it if it does not exist. The private key file is written with
 * owner-only permissions. An existing private key is never replaced, because
 * every signature already on the chain depends on it. Returns NULL on error. */
EVP_PKEY *crypto_identity_load_or_create(const char *owner);

/* 1 if keys/<owner>.pem exists. */
int crypto_identity_exists(const char *owner);

/* The public key as a compressed EC point in hex (66 characters). This is the
 * member's on-chain identity and is copied into every record they sign. */
int crypto_pubkey_hex(EVP_PKEY *pkey, char out[PUBKEY_HEX_LEN]);

/* Rebuilds a public-key object from its hex form, for verification. */
EVP_PKEY *crypto_pubkey_from_hex(const char *hex);

/* ECDSA-SHA256 sign / verify of an arbitrary message. Verification never
 * "fails open": any error is reported as an invalid signature. */
int crypto_sign(EVP_PKEY *priv, const void *msg, size_t len,
                unsigned char sig[MAX_SIG_LEN], size_t *sig_len);
int crypto_verify_hex(const char *pubkey_hex, const void *msg, size_t len,
                      const unsigned char *sig, size_t sig_len);

/* Hex helpers for writing signatures to text files. */
void bytes_to_hex(const unsigned char *b, size_t len, char *out, size_t cap);
size_t hex_to_bytes(const char *hex, unsigned char *out, size_t cap);

#endif
