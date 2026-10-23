#define _POSIX_C_SOURCE 200809L
#include "aimt/expert_protocol.h"

#include <fcntl.h>
#include <stdio.h>
#include <sys/stat.h>
#include <unistd.h>
#include <string.h>

static aimt_expert_message *read_message(FILE *input)
{
    char json[AIMT_EXPERT_MESSAGE_MAX + 1], error[256];
    size_t used = fread(json, 1, sizeof(json), input);
    aimt_expert_message *message = NULL;
    if (ferror(input) || !aimt_expert_message_parse(json, used, &message, error, sizeof(error))) {
        fprintf(stderr, "%s\n", ferror(input) ? "Cannot read the message." : error);
    }
    return message;
}

int main(int argc, char **argv)
{
    aimt_expert_message *request = NULL;
    if (argc != 1 && !(argc == 3 && !strcmp(argv[1], "--reply-to"))) {
        fprintf(stderr, "Usage: ai-multitool-agents-manager-expert-check [--reply-to REQUEST_FILE] < message.json\n");
        return 2;
    }
    if (argc == 3) {
        int fd = open(argv[2], O_RDONLY | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK);
        struct stat st;
        if (fd < 0 || fstat(fd, &st) || !S_ISREG(st.st_mode) || st.st_size <= 0 || st.st_size > AIMT_EXPERT_MESSAGE_MAX) {
            if (fd >= 0) close(fd);
            fprintf(stderr, "Cannot read the bounded request file.\n");
            return 1;
        }
        FILE *input = fdopen(fd, "rb");
        if (!input) { close(fd); return 1; }
        request = read_message(input);
        fclose(input);
        if (!request) return 1;
        if (aimt_expert_message_kind(request) != AIMT_EXPERT_REQUEST) {
            fprintf(stderr, "The reference file must contain a request.\n");
            aimt_expert_message_destroy(request);
            return 1;
        }
    }
    aimt_expert_message *message = read_message(stdin);
    bool ok = message && (!request || aimt_expert_reply_matches(request, message));
    if (message && !ok) fprintf(stderr, "The reply does not match the request or source revisions.\n");
    if (ok) puts("Valid expert contract v1.");
    aimt_expert_message_destroy(message);
    aimt_expert_message_destroy(request);
    return ok ? 0 : 1;
}
