#ifndef CLI_H
#define CLI_H

#include "app.h"

/* Runs the command loop until `exit` or end of input. Returns 0. */
int cli_run(App *app);

#endif
