#include "registry.h"

#include <stdio.h>
#include <string.h>
#include <strings.h>

#define LINE_LEN 256

static const char *COND_NAMES[COND_COUNT] = { "NEW", "GOOD", "WORN", "LOST" };

const char *condition_name(Condition c) {
    return (c >= 0 && c < COND_COUNT) ? COND_NAMES[c] : "?";
}

int condition_parse(const char *s, Condition *out) {
    for (int i = 0; i < COND_COUNT; i++) {
        if (strcasecmp(s, COND_NAMES[i]) == 0) { *out = (Condition)i; return 1; }
    }
    return 0;
}

/* Splits `line` on commas into exactly `want` fields (in place). */
static int split3(char *line, char *f[3]) {
    f[0] = line;
    char *a = strchr(line, ',');
    if (!a) return 0;
    *a = '\0';
    f[1] = a + 1;
    char *b = strchr(f[1], ',');
    if (!b) return 0;
    *b = '\0';
    f[2] = b + 1;
    for (int i = 0; i < 3; i++) str_trim(f[i]);
    return 1;
}

/* Reads a registry file line by line, calling `add` for each 3-field line.
 * Returns the number of records accepted, or -1 if the file is missing. */
static int read_registry(const char *path, const char *label, Registry *reg,
                         int (*add)(Registry *, char *f[3], int line_no)) {
    FILE *fp = fopen(path, "r");
    if (!fp) {
        fprintf(stderr, "ERROR: %s file '%s' is missing.\n", label, path);
        return -1;
    }
    char line[LINE_LEN];
    int count = 0, line_no = 0;
    while (fgets(line, sizeof(line), fp)) {
        line_no++;
        str_trim(line);
        if (line[0] == '\0' || line[0] == '#') continue;
        char *f[3];
        if (!split3(line, f)) {
            fprintf(stderr, "WARNING: %s line %d skipped (expected 3 comma-separated fields).\n",
                    path, line_no);
            continue;
        }
        if (add(reg, f, line_no)) count++;
    }
    fclose(fp);
    if (count == 0) fprintf(stderr, "ERROR: %s file '%s' is empty or has no valid records.\n", label, path);
    return count;
}

static int add_book_line(Registry *reg, char *f[3], int line_no) {
    if (reg->book_count >= MAX_BOOKS || !valid_id(f[0]) ||
        !valid_text(f[1], TITLE_LEN) || !valid_text(f[2], AUTHOR_LEN) || registry_book(reg, f[0])) {
        fprintf(stderr, "WARNING: books.txt line %d skipped (invalid, too long or duplicate).\n", line_no);
        return 0;
    }
    Book *b = &reg->books[reg->book_count++];
    snprintf(b->book_id, ID_LEN, "%s", f[0]);
    snprintf(b->title, TITLE_LEN, "%s", f[1]);
    snprintf(b->author, AUTHOR_LEN, "%s", f[2]);
    return 1;
}

static int add_member_line(Registry *reg, char *f[3], int line_no) {
    if (reg->member_count >= MAX_MEMBERS || !valid_id(f[0]) || strcmp(f[0], LIBRARIAN_ID) == 0 ||
        !valid_text(f[1], NAME_LEN) || !valid_text(f[2], COURSE_LEN) || registry_member(reg, f[0])) {
        fprintf(stderr, "WARNING: members.txt line %d skipped (invalid, too long or duplicate).\n", line_no);
        return 0;
    }
    Member *m = &reg->members[reg->member_count];
    memset(m, 0, sizeof(*m));
    snprintf(m->member_id, ID_LEN, "%s", f[0]);
    snprintf(m->full_name, NAME_LEN, "%s", f[1]);
    snprintf(m->course_code, COURSE_LEN, "%s", f[2]);
    m->key = crypto_identity_load_or_create(m->member_id);
    if (!m->key || !crypto_pubkey_hex(m->key, m->pubkey_hex)) {
        fprintf(stderr, "ERROR: no usable key pair for member %s.\n", m->member_id);
        EVP_PKEY_free(m->key);
        return 0;
    }
    reg->member_count++;
    return 1;
}

static int add_copy_line(Registry *reg, char *f[3], int line_no) {
    Condition c;
    if (reg->copy_count >= MAX_COPIES || !valid_id(f[0]) || strlen(f[0]) >= COPY_ID_LEN ||
        !registry_book(reg, f[1]) || !condition_parse(f[2], &c) || registry_copy(reg, f[0])) {
        fprintf(stderr, "WARNING: copies.txt line %d skipped (bad ID, unknown book, bad condition "
                        "or duplicate).\n", line_no);
        return 0;
    }
    CopyInfo *ci = &reg->copies[reg->copy_count++];
    snprintf(ci->copy_id, COPY_ID_LEN, "%s", f[0]);
    snprintf(ci->book_id, ID_LEN, "%s", f[1]);
    ci->condition = c;
    return 1;
}

int registry_load(Registry *reg) {
    memset(reg, 0, sizeof(*reg));
    /* Books must be loaded before copies, which refer to them. */
    if (read_registry(BOOKS_PATH, "Book registry", reg, add_book_line) <= 0) return 0;
    if (read_registry(MEMBERS_PATH, "Member registry", reg, add_member_line) <= 0) return 0;
    if (read_registry(COPIES_PATH, "Copy registry", reg, add_copy_line) <= 0) return 0;

    reg->librarian_key = crypto_identity_load_or_create(LIBRARIAN_ID);
    if (!reg->librarian_key || !crypto_pubkey_hex(reg->librarian_key, reg->librarian_pubkey)) {
        fprintf(stderr, "ERROR: no usable librarian key pair.\n");
        return 0;
    }
    return 1;
}

void registry_free(Registry *reg) {
    for (size_t i = 0; i < reg->member_count; i++) EVP_PKEY_free(reg->members[i].key);
    EVP_PKEY_free(reg->librarian_key);
    reg->member_count = 0;
    reg->librarian_key = NULL;
}

const Book *registry_book(const Registry *reg, const char *book_id) {
    for (size_t i = 0; i < reg->book_count; i++)
        if (strcmp(reg->books[i].book_id, book_id) == 0) return &reg->books[i];
    return NULL;
}

const Member *registry_member(const Registry *reg, const char *member_id) {
    for (size_t i = 0; i < reg->member_count; i++)
        if (strcmp(reg->members[i].member_id, member_id) == 0) return &reg->members[i];
    return NULL;
}

const CopyInfo *registry_copy(const Registry *reg, const char *copy_id) {
    for (size_t i = 0; i < reg->copy_count; i++)
        if (strcmp(reg->copies[i].copy_id, copy_id) == 0) return &reg->copies[i];
    return NULL;
}

const char *registry_pubkey(const Registry *reg, const char *signer_id) {
    if (strcmp(signer_id, LIBRARIAN_ID) == 0) return reg->librarian_pubkey;
    const Member *m = registry_member(reg, signer_id);
    return m ? m->pubkey_hex : NULL;
}

EVP_PKEY *registry_private_key(const Registry *reg, const char *signer_id) {
    if (strcmp(signer_id, LIBRARIAN_ID) == 0) return reg->librarian_key;
    const Member *m = registry_member(reg, signer_id);
    return m ? m->key : NULL;
}

static int append_line(const char *path, const char *a, const char *b, const char *c) {
    FILE *fp = fopen(path, "a");
    if (!fp) return 0;
    fprintf(fp, "%s,%s,%s\n", a, b, c);
    fclose(fp);
    return 1;
}

int registry_add_member(Registry *reg, const char *id, const char *name, const char *course,
                        char *err, size_t errcap) {
    if (!valid_id(id) || strcmp(id, LIBRARIAN_ID) == 0) {
        snprintf(err, errcap, "ERROR: invalid member ID '%s' (letters, digits, '-' or '_', max %d).", id, ID_LEN - 1);
        return 0;
    }
    if (registry_member(reg, id)) { snprintf(err, errcap, "ERROR: member %s already exists.", id); return 0; }
    if (!valid_text(name, NAME_LEN) || !valid_text(course, COURSE_LEN)) {
        snprintf(err, errcap, "ERROR: name (max %d) and course code (max %d) must be non-empty "
                 "and must not contain ',' or '|'.", NAME_LEN - 1, COURSE_LEN - 1);
        return 0;
    }
    if (reg->member_count >= MAX_MEMBERS) { snprintf(err, errcap, "ERROR: member registry is full."); return 0; }
    char buf[3][NAME_LEN];
    snprintf(buf[0], NAME_LEN, "%s", id);
    snprintf(buf[1], NAME_LEN, "%s", name);
    snprintf(buf[2], NAME_LEN, "%s", course);
    char *f[3] = { buf[0], buf[1], buf[2] };
    if (!add_member_line(reg, f, 0)) { snprintf(err, errcap, "ERROR: could not create the member's key pair."); return 0; }
    if (!append_line(MEMBERS_PATH, id, name, course)) {
        snprintf(err, errcap, "WARNING: member added in memory but members.txt could not be updated.");
        return 0;
    }
    return 1;
}

int registry_add_book(Registry *reg, const char *id, const char *title, const char *author,
                      char *err, size_t errcap) {
    if (!valid_id(id)) { snprintf(err, errcap, "ERROR: invalid book ID '%s'.", id); return 0; }
    if (registry_book(reg, id)) { snprintf(err, errcap, "ERROR: book %s already exists.", id); return 0; }
    if (!valid_text(title, TITLE_LEN) || !valid_text(author, AUTHOR_LEN)) {
        snprintf(err, errcap, "ERROR: title (max %d) and author (max %d) must be non-empty and "
                 "must not contain ',' or '|'.", TITLE_LEN - 1, AUTHOR_LEN - 1);
        return 0;
    }
    if (reg->book_count >= MAX_BOOKS) { snprintf(err, errcap, "ERROR: book registry is full."); return 0; }
    Book *b = &reg->books[reg->book_count++];
    snprintf(b->book_id, ID_LEN, "%s", id);
    snprintf(b->title, TITLE_LEN, "%s", title);
    snprintf(b->author, AUTHOR_LEN, "%s", author);
    if (!append_line(BOOKS_PATH, id, title, author)) {
        snprintf(err, errcap, "WARNING: book added in memory but books.txt could not be updated.");
        return 0;
    }
    return 1;
}

int registry_add_copy(Registry *reg, const char *book_id, Condition cond,
                      char new_id[COPY_ID_LEN], char *err, size_t errcap) {
    if (!registry_book(reg, book_id)) { snprintf(err, errcap, "ERROR: Book or Member not found (book %s).", book_id); return 0; }
    if (cond == COND_LOST) { snprintf(err, errcap, "ERROR: a new copy cannot be registered as LOST."); return 0; }
    if (reg->copy_count >= MAX_COPIES) { snprintf(err, errcap, "ERROR: copy registry is full."); return 0; }
    for (int n = 1; n < 1000; n++) {
        snprintf(new_id, COPY_ID_LEN, "%s-C%d", book_id, n);
        if (!registry_copy(reg, new_id)) break;
    }
    CopyInfo *ci = &reg->copies[reg->copy_count++];
    snprintf(ci->copy_id, COPY_ID_LEN, "%s", new_id);
    snprintf(ci->book_id, ID_LEN, "%s", book_id);
    ci->condition = cond;
    if (!append_line(COPIES_PATH, new_id, book_id, condition_name(cond))) {
        snprintf(err, errcap, "WARNING: copy added in memory but copies.txt could not be updated.");
        return 0;
    }
    return 1;
}
