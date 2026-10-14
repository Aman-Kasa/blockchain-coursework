#include "util.h"
#include "config.h"

#include <ctype.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>

void str_trim(char *s) {
    size_t len = strlen(s);
    while (len > 0 && isspace((unsigned char)s[len - 1])) s[--len] = '\0';
    size_t start = 0;
    while (s[start] && isspace((unsigned char)s[start])) start++;
    if (start > 0) memmove(s, s + start, len - start + 1);
}

int str_tokenize(char *line, char *tok[], int max) {
    int n = 0;
    char *p = line;
    while (*p && n < max) {
        while (*p && isspace((unsigned char)*p)) p++;
        if (!*p) break;
        if (*p == '"') {
            p++;
            tok[n++] = p;
            while (*p && *p != '"') p++;
        } else {
            tok[n++] = p;
            while (*p && !isspace((unsigned char)*p)) p++;
        }
        if (*p) *p++ = '\0';
    }
    return n;
}

int parse_long(const char *s, long *out) {
    if (!s || *s == '\0') return 0;
    char *end;
    errno = 0;
    long v = strtol(s, &end, 10);
    if (errno != 0 || *end != '\0') return 0;
    *out = v;
    return 1;
}

int valid_id(const char *s) {
    size_t len = strlen(s);
    if (len == 0 || len >= ID_LEN) return 0;
    for (size_t i = 0; i < len; i++) {
        unsigned char c = (unsigned char)s[i];
        if (!isalnum(c) && c != '-' && c != '_') return 0;
    }
    return 1;
}

int valid_text(const char *s, size_t cap) {
    size_t len = strlen(s);
    if (len == 0 || len >= cap) return 0;
    return strpbrk(s, ",|\n\r") == NULL;
}

static long clock_offset = 0;

time_t sim_now(void) { return time(NULL) + (time_t)clock_offset; }
long sim_get_offset(void) { return clock_offset; }
void sim_set_offset(long seconds) { clock_offset = seconds; }

void format_time(time_t t, char *out, size_t cap) {
    struct tm tm_info;
    localtime_r(&t, &tm_info);
    strftime(out, cap, "%Y-%m-%d %H:%M:%S", &tm_info);
}

void format_date(time_t t, char *out, size_t cap) {
    struct tm tm_info;
    localtime_r(&t, &tm_info);
    strftime(out, cap, "%Y-%m-%d", &tm_info);
}

long days_late(time_t earlier, time_t later) {
    /* Whole days completed. A copy is late (and fined) only once a full day
     * has passed since its due date, so the result does not depend on how
     * many seconds elapsed between two commands. */
    if (later <= earlier) return 0;
    return (long)(later - earlier) / SECONDS_PER_DAY;
}
