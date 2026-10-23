#ifndef AIMT_CLI_H
#define AIMT_CLI_H

#include <stddef.h>
#include <stdio.h>

#include "aimt/app.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    AIMT_INPUT_OK, AIMT_INPUT_EOF, AIMT_INPUT_TOO_LONG,
    AIMT_INPUT_INVALID, AIMT_INPUT_IO_ERROR
} aimt_input_result;

/* Invalid/oversized records are consumed completely, never executed partially. */
aimt_input_result aimt_read_line(FILE *input, char *buffer, size_t capacity);
int aimt_cli_run(aimt_app *app, FILE *input, FILE *output);

#ifdef __cplusplus
}
#endif

#endif
