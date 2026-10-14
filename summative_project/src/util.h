#ifndef UTIL_H
#define UTIL_H

#include <stddef.h>
#include <time.h>

/* Field sizes shared by every module. */
#define ID_LEN        20   /* member_id, book_id                   */
#define COPY_ID_LEN   24   /* copy_id, e.g. BK001-C1               */
#define TITLE_LEN     80
#define AUTHOR_LEN    50
#define NAME_LEN      50
#define COURSE_LEN    10
#define HASH_HEX_LEN  65   /* 64 hex characters + NUL              */

/* Removes leading/trailing whitespace and line endings in place. */
void str_trim(char *s);

/* Splits a command line into tokens. Double quotes group words, so
 * register_book BK004 "Purple Hibiscus" "Chimamanda Adichie" gives 4 tokens.
 * Returns the token count (at most max). Modifies `line` in place. */
int str_tokenize(char *line, char *tok[], int max);

/* Strict whole-number parsing: no empty string, no trailing junk, no overflow. */
int parse_long(const char *s, long *out);

/* IDs: 1-19 characters of letters, digits, '-' or '_'. */
int valid_id(const char *s);

/* Free text (titles, names): non-empty, fits in `cap`, and contains none of
 * the characters used as separators in the data files (',' '|' newline). */
int valid_text(const char *s, size_t cap);

/* Simulated clock: real time plus an offset in seconds. Every timestamp and
 * due date in the system comes from sim_now(), so late returns and overdue
 * books can be demonstrated without waiting two weeks. The offset is saved
 * with the rest of the state, so it survives restarts. */
time_t sim_now(void);
long sim_get_offset(void);
void sim_set_offset(long seconds);

void format_time(time_t t, char *out, size_t cap);   /* 2026-10-13 14:05:09 */
void format_date(time_t t, char *out, size_t cap);   /* 2026-10-13          */

/* Whole days that `later` is after `earlier` (0 within the first day). A
 * copy counts as late / overdue from 1 full day past its due date. */
long days_late(time_t earlier, time_t later);

#endif
