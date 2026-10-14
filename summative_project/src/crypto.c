#include "crypto.h"
#include "config.h"

#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#include <openssl/core_names.h>
#include <openssl/err.h>
#include <openssl/params.h>
#include <openssl/pem.h>

/* Implementation note: the brief mentions OpenSSL's EC_KEY API. In OpenSSL 3
 * every EC_KEY function is deprecated and produces warnings, which would break
 * the "compile cleanly with -Wall" requirement. The EVP_PKEY interface used
 * here performs exactly the same ECDSA operations on the same curve; it is
 * OpenSSL's supported replacement for EC_KEY. */

#define CURVE_NAME "prime256v1"   /* NIST P-256 */

static void openssl_error(const char *what) {
    fprintf(stderr, "ERROR: %s (OpenSSL: %s)\n", what, ERR_error_string(ERR_get_error(), NULL));
}

void sha256_hex(const void *data, size_t len, char out[HASH_HEX_LEN]) {
    unsigned char digest[EVP_MAX_MD_SIZE];
    unsigned int dlen = 0;
    if (!EVP_Digest(data, len, digest, &dlen, EVP_sha256(), NULL)) {
        openssl_error("SHA-256 failed");
        out[0] = '\0';
        return;
    }
    bytes_to_hex(digest, dlen, out, HASH_HEX_LEN);
}

void sha256_str(const char *s, char out[HASH_HEX_LEN]) {
    sha256_hex(s, strlen(s), out);
}

static void key_path(const char *owner, char *out, size_t cap) {
    snprintf(out, cap, "%s/%s.pem", KEYS_DIR, owner);
}

int crypto_identity_exists(const char *owner) {
    char path[128];
    key_path(owner, path, sizeof(path));
    FILE *f = fopen(path, "r");
    if (!f) return 0;
    fclose(f);
    return 1;
}

EVP_PKEY *crypto_identity_load_or_create(const char *owner) {
    char path[128];
    key_path(owner, path, sizeof(path));

    FILE *f = fopen(path, "r");
    if (f) {
        EVP_PKEY *pkey = PEM_read_PrivateKey(f, NULL, NULL, NULL);
        fclose(f);
        if (!pkey) openssl_error("could not parse private key");
        return pkey;
    }

    EVP_PKEY *pkey = EVP_EC_gen(CURVE_NAME);
    if (!pkey) {
        openssl_error("ECDSA key generation failed");
        return NULL;
    }
    f = fopen(path, "w");
    if (!f) {
        fprintf(stderr, "ERROR: cannot write private key '%s'.\n", path);
        EVP_PKEY_free(pkey);
        return NULL;
    }
    chmod(path, S_IRUSR | S_IWUSR);   /* owner-only before any key material is written */
    int ok = PEM_write_PrivateKey(f, pkey, NULL, NULL, 0, NULL, NULL);
    fclose(f);
    if (!ok) {
        openssl_error("writing private key failed");
        EVP_PKEY_free(pkey);
        return NULL;
    }
    return pkey;
}

int crypto_pubkey_hex(EVP_PKEY *pkey, char out[PUBKEY_HEX_LEN]) {
    /* Export the public point (uncompressed: 0x04 || X || Y, 65 bytes), then
     * compress it ourselves: 0x02 if Y is even, 0x03 if odd, followed by X. */
    unsigned char buf[65];
    size_t len = 0;
    if (!EVP_PKEY_get_octet_string_param(pkey, OSSL_PKEY_PARAM_PUB_KEY, buf, sizeof(buf), &len)) {
        openssl_error("cannot export public key");
        return 0;
    }
    unsigned char comp[33];
    if (len == 65 && buf[0] == 0x04) {
        comp[0] = (buf[64] & 1) ? 0x03 : 0x02;
        memcpy(comp + 1, buf + 1, 32);
    } else if (len == 33 && (buf[0] == 0x02 || buf[0] == 0x03)) {
        memcpy(comp, buf, 33);
    } else {
        fprintf(stderr, "ERROR: unexpected public key encoding (%zu bytes).\n", len);
        return 0;
    }
    bytes_to_hex(comp, sizeof(comp), out, PUBKEY_HEX_LEN);
    return 1;
}

EVP_PKEY *crypto_pubkey_from_hex(const char *hex) {
    unsigned char buf[65];
    size_t len = hex_to_bytes(hex, buf, sizeof(buf));
    if (len != 33) return NULL;

    EVP_PKEY_CTX *ctx = EVP_PKEY_CTX_new_from_name(NULL, "EC", NULL);
    if (!ctx) return NULL;
    char group[] = CURVE_NAME;
    OSSL_PARAM params[] = {
        OSSL_PARAM_utf8_string(OSSL_PKEY_PARAM_GROUP_NAME, group, 0),
        OSSL_PARAM_octet_string(OSSL_PKEY_PARAM_PUB_KEY, buf, len),
        OSSL_PARAM_END
    };
    EVP_PKEY *pkey = NULL;
    if (EVP_PKEY_fromdata_init(ctx) != 1 ||
        EVP_PKEY_fromdata(ctx, &pkey, EVP_PKEY_PUBLIC_KEY, params) != 1) {
        pkey = NULL;
    }
    EVP_PKEY_CTX_free(ctx);
    return pkey;
}

int crypto_sign(EVP_PKEY *priv, const void *msg, size_t len,
                unsigned char sig[MAX_SIG_LEN], size_t *sig_len) {
    int ok = 0;
    EVP_MD_CTX *ctx = EVP_MD_CTX_new();
    if (!ctx) return 0;
    size_t need = 0;
    if (EVP_DigestSignInit(ctx, NULL, EVP_sha256(), NULL, priv) == 1 &&
        EVP_DigestSign(ctx, NULL, &need, msg, len) == 1 && need <= MAX_SIG_LEN) {
        size_t got = MAX_SIG_LEN;
        if (EVP_DigestSign(ctx, sig, &got, msg, len) == 1) {
            *sig_len = got;
            ok = 1;
        }
    }
    if (!ok) openssl_error("ECDSA signing failed");
    EVP_MD_CTX_free(ctx);
    return ok;
}

int crypto_verify_hex(const char *pubkey_hex, const void *msg, size_t len,
                      const unsigned char *sig, size_t sig_len) {
    if (sig_len == 0) return 0;               /* a missing signature is never valid */
    EVP_PKEY *pub = crypto_pubkey_from_hex(pubkey_hex);
    if (!pub) return 0;
    int valid = 0;
    EVP_MD_CTX *ctx = EVP_MD_CTX_new();
    if (ctx && EVP_DigestVerifyInit(ctx, NULL, EVP_sha256(), NULL, pub) == 1) {
        valid = (EVP_DigestVerify(ctx, sig, sig_len, msg, len) == 1);
    }
    EVP_MD_CTX_free(ctx);
    EVP_PKEY_free(pub);
    ERR_clear_error();   /* a bad signature is an expected outcome, not an error */
    return valid;
}

void bytes_to_hex(const unsigned char *b, size_t len, char *out, size_t cap) {
    static const char digits[] = "0123456789abcdef";
    if (cap < len * 2 + 1) {
        if (cap) out[0] = '\0';
        return;
    }
    for (size_t i = 0; i < len; i++) {
        out[2 * i] = digits[b[i] >> 4];
        out[2 * i + 1] = digits[b[i] & 0xF];
    }
    out[len * 2] = '\0';
}

static int hexval(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

size_t hex_to_bytes(const char *hex, unsigned char *out, size_t cap) {
    size_t n = strlen(hex);
    if (n % 2 || n / 2 > cap) return 0;
    for (size_t i = 0; i < n / 2; i++) {
        int hi = hexval(hex[2 * i]), lo = hexval(hex[2 * i + 1]);
        if (hi < 0 || lo < 0) return 0;
        out[i] = (unsigned char)(hi << 4 | lo);
    }
    return n / 2;
}
