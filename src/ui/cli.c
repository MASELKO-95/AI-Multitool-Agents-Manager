#include "aimt/cli.h"

#include <string.h>

aimt_input_result aimt_read_line(FILE *input, char *buffer, size_t capacity)
{
    size_t length = 0;
    int ch;
    bool too_long = false;
    if (input == NULL || buffer == NULL || capacity < 2u) return AIMT_INPUT_INVALID;
    buffer[0] = '\0';
    while ((ch = fgetc(input)) != EOF && ch != '\n') {
        if (length < capacity - 1u) buffer[length++] = (char)(unsigned char)ch;
        else too_long = true;
    }
    if (ferror(input)) { buffer[0] = '\0'; return AIMT_INPUT_IO_ERROR; }
    if (ch == EOF && length == 0 && !too_long) return AIMT_INPUT_EOF;
    if (too_long) { buffer[0] = '\0'; return AIMT_INPUT_TOO_LONG; }
    if (length > 0 && buffer[length - 1u] == '\r') length--;
    buffer[length] = '\0';
    if (!aimt_text_valid(buffer, length)) {
        buffer[0] = '\0';
        return AIMT_INPUT_INVALID;
    }
    return AIMT_INPUT_OK;
}

static void help(FILE *out)
{
    fputs("/help - commands\n/status - session status and budget\n"
          "/modules - modules\n/module NAME - select module\n"
          "/history - session messages\n/clear - clear history from RAM\n"
          "/safe - Safe Mode status\n/quit - exit\n", out);
}

int aimt_cli_run(aimt_app *app, FILE *input, FILE *out)
{
    char line[AIMT_MESSAGE_MAX_BYTES + 1u];
    if (app == NULL || input == NULL || out == NULL) return 1;
    fputs("AI Multitool Agents Manager - main module\n"
          "Session in RAM only. No AI model connected to this CLI yet.\n"
          "Files, commands and network unavailable. Safe Mode not implemented.\n"
          "Enter /help to see commands.\n", out);
    for (;;) {
        aimt_input_result input_result;
        fputs("> ", out);
        if (fflush(out) == EOF) return 1;
        input_result = aimt_read_line(input, line, sizeof(line));
        if (input_result == AIMT_INPUT_EOF) { fputc('\n', out); break; }
        if (input_result == AIMT_INPUT_IO_ERROR) return 1;
        if (input_result != AIMT_INPUT_OK) {
            fputs("Entire line rejected: 2048-byte limit or invalid text.\n", out);
            continue;
        }
        if (line[0] == '\0') continue;
        if (strcmp(line, "/quit") == 0) break;
        if (strcmp(line, "/help") == 0) help(out);
        else if (strcmp(line, "/status") == 0) {
            aimt_status status;
            if (aimt_app_status(app, &status) != AIMT_OK) return 1;
            fprintf(out, "Module: %s; messages: %zu/%zu; text: %zu bytes\n"
                    "Session core allocation: %zu/%zu bytes (not the whole process).\n",
                    status.active_module, status.message_count, status.message_capacity,
                    status.text_bytes, status.allocated_bytes, status.budget_bytes);
        } else if (strcmp(line, "/modules") == 0) {
            for (size_t i = 0; i < aimt_module_count(); ++i) {
                const aimt_module_info *module = aimt_module_at(i);
                fprintf(out, "%s [%s] — %s\n", module->id,
                        module->available ? "available" : "not implemented", module->description);
            }
        } else if (strncmp(line, "/module ", 8u) == 0) {
            aimt_result result = aimt_app_select_module(app, line + 8u);
            if (result == AIMT_OK) fputs("Module selected.\n", out);
            else if (result == AIMT_UNAVAILABLE) fputs("This module is not implemented yet.\n", out);
            else fputs("Unknown module. Use /modules.\n", out);
        } else if (strcmp(line, "/history") == 0) {
            aimt_status status;
            if (aimt_app_status(app, &status) != AIMT_OK) return 1;
            for (size_t i = 0; i < status.message_count; ++i)
                fprintf(out, "%zu: %s\n", i + 1u, aimt_app_message(app, i));
            if (status.message_count == 0) fputs("History is empty.\n", out);
        } else if (strcmp(line, "/clear") == 0) {
            aimt_app_clear(app);
            fputs("History removed from the session buffer.\n", out);
        } else if (strncmp(line, "/safe", 5u) == 0 &&
                   (line[5] == '\0' || line[5] == ' ')) {
            fputs("Safe Mode unavailable: encryption and isolation are missing. It was not enabled.\n", out);
        } else if (line[0] == '/') {
            fputs("Unknown command. Use /help.\n", out);
        } else {
            aimt_result result = aimt_app_submit(app, line, strlen(line));
            if (result == AIMT_OK)
                fputs("Message added to the session. An AI reply requires connecting a model.\n", out);
            else if (result == AIMT_FULL)
                fputs("Session full. Earlier messages were retained. Use /clear or exit.\n", out);
            else fputs("Message rejected.\n", out);
        }
        if (ferror(out)) return 1;
    }
    return ferror(out) ? 1 : 0;
}
