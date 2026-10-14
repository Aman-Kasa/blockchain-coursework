#ifndef LENDING_H
#define LENDING_H

#include "app.h"

/* Turns library operations into signed requests in the pending pool.
 *
 * Every operation follows the same path (lending_submit):
 *   1. build the record from the registries and the projected state
 *      (confirmed state + everything already pending),
 *   2. give it the signer's expected next nonce and public key,
 *   3. check it is a legal state change on the projected state,
 *   4. sign it with the signer's private key and verify the signature,
 *   5. run the misuse heuristics (SUSPICIOUS requests stay in the pool but
 *      are never mined until a librarian approves them),
 *   6. compute its priority and add it to the pool.
 * Nothing on the chain, in the inventory or in any balance changes until the
 * request is mined. Each function prints what happened and returns 1 if a
 * request entered the pool. */

int lending_borrow(App *app, const char *member_id, const char *book_id, const char *copy_id);
int lending_return(App *app, const char *copy_id, const char *condition, const char *reason);
int lending_renew(App *app, const char *copy_id);
int lending_reserve(App *app, const char *member_id, const char *book_id);
int lending_settle_fine(App *app, const char *member_id, long amount);
int lending_mark_lost(App *app, const char *copy_id, const char *reason);
int lending_transfer(App *app, const char *from, const char *to, long amount, long nonce_or_minus1);

/* Overdue detection: flags every confirmed BORROWED copy past its due date
 * with a librarian-signed OVERDUE request (once per loan). Runs before every
 * CLI command. Returns the number of requests created; prints them unless
 * quiet. */
int lending_scan_overdue(App *app, int quiet);

/* Submits an already-signed record unchanged (attacker replay simulation for
 * the duplicate-request heuristic). */
int lending_replay(App *app, const char *request_id);

/* Drops pool entries that can no longer be applied (for example after the
 * request they depended on was rejected). Prints what was dropped. */
void lending_prune_pool(App *app);

/* The signer's expected next nonce: confirmed nonce + its pending requests. */
long lending_expected_nonce(const App *app, const char *signer_id);

/* Approve / reject a SUSPICIOUS request (fraud review workflow). */
int lending_approve(App *app, const char *request_id);
int lending_reject(App *app, const char *request_id);

#endif
