#include "aimt/version.h"
#include "aimt/app.h"
#include "aimt/cli.h"

#include <stdio.h>
#include <string.h>

static bool parse_budget(const char *text, size_t *bytes)
{
    size_t kib = 0;
    if (text == NULL || text[0] == '\0') return false;
    for (size_t i = 0; text[i] != '\0'; ++i) {
        unsigned char ch = (unsigned char)text[i];
        if (ch < '0' || ch > '9' || kib > 409u) return false;
        kib = kib * 10u + (size_t)(ch - '0');
        if (kib > 4096u) return false;
    }
    if (kib < 8u) return false;
    *bytes = kib * 1024u;
    return true;
}

int main(int argc, char **argv)
{
    if (argc == 2 && strcmp(argv[1], "--version") == 0) {
        puts(aimt_version());
        return 0;
    }

    if (argc == 2 && strcmp(argv[1], "--help") == 0) {
        puts("Usage: ai-multitool-agents-manager [--memory-kib 8..4096] [--safe]");
        puts("       ai-multitool-agents-manager --help | --version");
        puts("No arguments: start the main module terminal session.");
        puts("--memory-kib limits session allocation, not process or model memory.");
        puts("--safe refuses startup until full Safe Mode is implemented.");
        return 0;
    }

    aimt_config config = aimt_config_default();
    aimt_app *app = NULL;
    bool budget_seen = false;
    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--safe") == 0 && !config.require_safe_mode) {
            config.require_safe_mode = true;
        } else if (strcmp(argv[i], "--memory-kib") == 0 && !budget_seen && i + 1 < argc) {
            if (!parse_budget(argv[++i], &config.session_budget_bytes)) {
                fputs("Invalid session budget: expected 8..4096 KiB.\n", stderr);
                return 2;
            }
            budget_seen = true;
        } else {
            fputs("Unsupported or duplicate arguments. Use --help.\n", stderr);
            return 2;
        }
    }
    aimt_result result = aimt_app_create(&config, &app);
    if (result != AIMT_OK) {
        fputs(result == AIMT_UNAVAILABLE ?
              "Safe Mode unavailable; startup refused without downgrading security.\n" :
              "Cannot initialize main module.\n", stderr);
        return 1;
    }
    int exit_code = aimt_cli_run(app, stdin, stdout);
    aimt_app_destroy(app);
    return exit_code;
}
