/* Unit tests for the cryptographic core: ECDSA record signing, the Merkle
 * tree, multi-record blocks, proof of work, chain validation and chain.dat
 * round-trips. Run from a scratch directory that has data/ and keys/
 * (tests/run_tests.sh does this). */
#include <stdio.h>
#include <string.h>

#include "../src/chain.h"
#include "../src/merkle.h"
#include "../src/record.h"
#include "../src/registry.h"

static int pass = 0, failures = 0;
#define CHECK(cond, name) do { if (cond) pass++; else { failures++; printf("FAIL: %s\n", name); } } while (0)

static Record make_record(const Registry *reg, const char *member, const char *copy, long nonce) {
    Record r;
    memset(&r, 0, sizeof(r));
    r.timestamp = 1790000000 + nonce;
    r.action = ACT_BORROWED;
    snprintf(r.copy_id, COPY_ID_LEN, "%s", copy);
    const CopyInfo *ci = registry_copy(reg, copy);
    snprintf(r.book_id, ID_LEN, "%s", ci->book_id);
    snprintf(r.book_title, TITLE_LEN, "%s", registry_book(reg, ci->book_id)->title);
    const Member *m = registry_member(reg, member);
    snprintf(r.member_id, ID_LEN, "%s", member);
    snprintf(r.member_name, NAME_LEN, "%s", m->full_name);
    r.due_date = r.timestamp + 14 * 86400;
    snprintf(r.signer_id, ID_LEN, "%s", member);
    r.nonce = nonce;
    snprintf(r.pubkey, PUBKEY_HEX_LEN, "%s", m->pubkey_hex);
    record_sign(&r, m->key);
    return r;
}

static void test_merkle(void) {
    char leaves[4][HASH_HEX_LEN], root[HASH_HEX_LEN], manual[HASH_HEX_LEN];
    sha256_str("a", leaves[0]);
    sha256_str("b", leaves[1]);
    sha256_str("c", leaves[2]);

    merkle_root(leaves, 0, root);
    CHECK(strcmp(root, "0000000000000000000000000000000000000000000000000000000000000000") == 0,
          "empty tree root is 64 zeros");
    merkle_root(leaves, 1, root);
    CHECK(strcmp(root, leaves[0]) == 0, "single-leaf root is the leaf itself");

    /* Two leaves: root = SHA-256(bytes(a) || bytes(b)) */
    unsigned char buf[64];
    hex_to_bytes(leaves[0], buf, 32);
    hex_to_bytes(leaves[1], buf + 32, 32);
    sha256_hex(buf, 64, manual);
    merkle_root(leaves, 2, root);
    CHECK(strcmp(root, manual) == 0, "two-leaf root = SHA-256(left || right)");

    /* Three leaves: the odd leaf is paired with itself. */
    char ab[HASH_HEX_LEN], cc[HASH_HEX_LEN], expect[HASH_HEX_LEN];
    memcpy(ab, manual, HASH_HEX_LEN);
    hex_to_bytes(leaves[2], buf, 32);
    hex_to_bytes(leaves[2], buf + 32, 32);
    sha256_hex(buf, 64, cc);
    hex_to_bytes(ab, buf, 32);
    hex_to_bytes(cc, buf + 32, 32);
    sha256_hex(buf, 64, expect);
    merkle_root(leaves, 3, root);
    CHECK(strcmp(root, expect) == 0, "three-leaf root duplicates the odd leaf");

    char before[HASH_HEX_LEN];
    memcpy(before, root, HASH_HEX_LEN);
    leaves[1][0] = leaves[1][0] == 'f' ? 'e' : 'f';
    merkle_root(leaves, 3, root);
    CHECK(strcmp(root, before) != 0, "changing one leaf changes the root");
}

static void test_records(const Registry *reg) {
    Record r = make_record(reg, "ALU001", "BK001-C1", 0);
    CHECK(strlen(r.record_id) == 16, "record_id is 16 hex characters");
    CHECK(r.sig_len >= 64 && r.sig_len <= MAX_SIG_LEN, "ECDSA signature has a DER length");
    CHECK(record_verify(&r), "valid record verifies");

    Record t = r;
    t.fine_amount = 99;
    CHECK(!record_verify(&t), "edited field breaks the record ID / signature");

    t = r;
    t.signature[10] ^= 0x01;
    CHECK(!record_verify(&t), "flipped signature byte is rejected");

    t = r;
    t.sig_len = 0;
    CHECK(!record_verify(&t), "missing signature is rejected");

    /* Re-sign the same content with ALU002's key but keep ALU001's pubkey. */
    t = r;
    const Member *m2 = registry_member(reg, "ALU002");
    char content[RECORD_LINE_LEN];
    record_content(&t, content, sizeof(content));
    crypto_sign(m2->key, content, strlen(content), t.signature, &t.sig_len);
    CHECK(!record_verify(&t), "signature by the wrong key is rejected");

    char line[RECORD_LINE_LEN];
    Record back;
    record_to_line(&r, line, sizeof(line));
    CHECK(record_from_line(line, &back) && memcmp(&back, &r, sizeof(r)) == 0 && record_verify(&back),
          "record text round-trip is exact");
}

static void test_chain(const Registry *reg) {
    Chain c;
    chain_init(&c);
    CHECK(chain_create_genesis(&c, 2), "genesis mined");
    Record recs[3] = { make_record(reg, "ALU001", "BK001-C1", 0),
                       make_record(reg, "ALU002", "BK002-C1", 0),
                       make_record(reg, "ALU003", "BK003-C1", 0) };
    Block *b = block_prepare(&c, recs, 3, 2, "test-miner", sim_now());
    unsigned long attempts;
    CHECK(b && block_pow(b, 10000000UL, &attempts) && strncmp(b->hash, "00", 2) == 0,
          "block with 3 records mined at difficulty 2");
    chain_append(&c, b);
    CHECK(b->record_count == 3 && b->timestamp > c.head->timestamp, "record_count and increasing timestamp");

    ChainCheck v = chain_validate(&c, reg);
    CHECK(v.valid, "fresh chain validates");

    CHECK(chain_save(&c, "data/test_chain.dat"), "chain saved");
    Chain d;
    chain_init(&d);
    CHECK(chain_load(&d, "data/test_chain.dat") == 1 && d.length == 2 && chain_validate(&d, reg).valid,
          "chain reloads and still validates");
    chain_free(&d);

    /* Tamper 1: change a record field and re-sign it with the right key.
     * The record is valid on its own, but the Merkle root no longer matches. */
    Record saved = b->records[1];
    b->records[1].fine_amount = 50;
    record_sign(&b->records[1], registry_member(reg, "ALU002")->key);
    v = chain_validate(&c, reg);
    CHECK(!v.valid && v.bad_index == 1 && strstr(v.reason, "Merkle"), "re-signed record caught by Merkle root");
    b->records[1] = saved;

    /* Tamper 2: change a record without re-signing. */
    b->records[0].due_date += 86400;
    v = chain_validate(&c, reg);
    CHECK(!v.valid && strstr(v.reason, "signature"), "edited record caught by its signature");
    b->records[0] = recs[0];

    /* Tamper 3: header field. */
    b->difficulty = 1;
    v = chain_validate(&c, reg);
    CHECK(!v.valid && strstr(v.reason, "header hash"), "edited header caught by block hash");
    b->difficulty = 2;

    /* Tamper 4: a record signed by a key that is not the registered one. */
    saved = b->records[2];
    snprintf(b->records[2].pubkey, PUBKEY_HEX_LEN, "%s", registry_member(reg, "ALU001")->pubkey_hex);
    record_sign(&b->records[2], registry_member(reg, "ALU001")->key);
    v = chain_validate(&c, reg);
    CHECK(!v.valid && strstr(v.reason, "public key does not match"), "impersonation caught by registry key check");
    b->records[2] = saved;

    CHECK(chain_validate(&c, reg).valid, "chain valid again after restoring");
    chain_free(&c);
}

int main(void) {
    static Registry reg;
    if (!registry_load(&reg)) { printf("FAIL: registry_load\n"); return 1; }
    test_merkle();
    test_records(&reg);
    test_chain(&reg);
    registry_free(&reg);
    printf("unit tests: %d passed, %d failed\n", pass, failures);
    return failures ? 1 : 0;
}
