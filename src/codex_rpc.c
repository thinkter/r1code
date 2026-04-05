#define _POSIX_C_SOURCE 200809L

#include "codex_rpc.h"

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

typedef enum RequestKind {
    REQUEST_KIND_UNKNOWN = 0,
    REQUEST_KIND_INITIALIZE,
    REQUEST_KIND_THREAD_START,
    REQUEST_KIND_TURN_START,
    REQUEST_KIND_MODEL_LIST,
} RequestKind;

static void CodexRpcClient_SetStatus(CodexRpcClient *client, const char *format, ...)
{
    va_list args;
    va_start(args, format);
    vsnprintf(client->status, sizeof(client->status), format, args);
    va_end(args);
}

static void CodexRpcClient_SetError(CodexRpcClient *client, const char *format, ...)
{
    va_list args;
    va_start(args, format);
    vsnprintf(client->last_error, sizeof(client->last_error), format, args);
    va_end(args);
}

static void CodexRpcClient_LogLine(bool is_stderr, const char *line)
{
    if (line == NULL || line[0] == '\0') {
        return;
    }

    fprintf(stderr, "[codex-rpc %s] %s\n", is_stderr ? "stderr" : "stdout", line);
    fflush(stderr);
}

static bool CodexRpcClient_SetNonBlocking(int fd)
{
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0) {
        return false;
    }

    return fcntl(fd, F_SETFL, flags | O_NONBLOCK) == 0;
}

static bool JsonEscape(const char *input, char *output, size_t output_size)
{
    size_t out = 0;

    if (output_size == 0) {
        return false;
    }

    for (size_t i = 0; input[i] != '\0'; ++i) {
        unsigned char ch = (unsigned char)input[i];
        size_t needed = 0;

        switch (ch) {
            case '\\':
            case '"':
            case '\n':
            case '\r':
            case '\t':
                needed = 2;
                break;
            default:
                needed = (ch >= 32) ? 1 : 0;
                break;
        }

        if (out + needed >= output_size) {
            output[out] = '\0';
            return false;
        }

        switch (ch) {
            case '\\':
            case '"':
                output[out++] = '\\';
                output[out++] = (char)ch;
                break;
            case '\n':
                output[out++] = '\\';
                output[out++] = 'n';
                break;
            case '\r':
                output[out++] = '\\';
                output[out++] = 'r';
                break;
            case '\t':
                output[out++] = '\\';
                output[out++] = 't';
                break;
            default:
                if (ch >= 32) {
                    output[out++] = (char)ch;
                }
                break;
        }
    }

    output[out] = '\0';
    return true;
}

static bool JsonExtractString(const char *json, const char *key, char *output, size_t output_size)
{
    char pattern[64];
    const char *cursor = NULL;
    size_t out = 0;

    if (output_size == 0) {
        return false;
    }

    snprintf(pattern, sizeof(pattern), "\"%s\":\"", key);
    cursor = strstr(json, pattern);
    if (cursor == NULL) {
        output[0] = '\0';
        return false;
    }

    cursor += strlen(pattern);

    while (*cursor != '\0' && out + 1 < output_size) {
        if (*cursor == '\\') {
            ++cursor;
            if (*cursor == '\0') {
                break;
            }

            switch (*cursor) {
                case 'n':
                    output[out++] = '\n';
                    break;
                case 'r':
                    output[out++] = '\r';
                    break;
                case 't':
                    output[out++] = '\t';
                    break;
                case '\\':
                case '"':
                case '/':
                    output[out++] = *cursor;
                    break;
                default:
                    output[out++] = *cursor;
                    break;
            }

            ++cursor;
            continue;
        }

        if (*cursor == '"') {
            output[out] = '\0';
            return true;
        }

        output[out++] = *cursor++;
    }

    output[out] = '\0';
    return out > 0;
}

static bool JsonExtractInt(const char *json, const char *key, int *value)
{
    char pattern[64];
    const char *cursor = NULL;

    snprintf(pattern, sizeof(pattern), "\"%s\":", key);
    cursor = strstr(json, pattern);
    if (cursor == NULL) {
        return false;
    }

    cursor += strlen(pattern);
    *value = (int)strtol(cursor, NULL, 10);
    return true;
}

static void ResetModelListState(CodexRpcClient *client, bool preserve_selected_model)
{
    if (!preserve_selected_model) {
        client->selected_model[0] = '\0';
    }

    memset(client->models, 0, sizeof(client->models));
    client->model_count = 0;
    client->selected_model_index = -1;
    client->models_loading = false;
    client->models_loaded = false;
    client->model_list_request_id = 0;
}

static void SyncSelectedModelIndex(CodexRpcClient *client)
{
    client->selected_model_index = -1;

    if (client->model_count <= 0) {
        return;
    }

    if (client->selected_model[0] != '\0') {
        for (int i = 0; i < client->model_count; ++i) {
            if (strcmp(client->selected_model, client->models[i].id) == 0) {
                client->selected_model_index = i;
                return;
            }
        }
    }

    for (int i = 0; i < client->model_count; ++i) {
        if (client->models[i].is_default) {
            client->selected_model_index = i;
            snprintf(client->selected_model, sizeof(client->selected_model), "%s", client->models[i].id);
            return;
        }
    }

    client->selected_model_index = 0;
    snprintf(client->selected_model, sizeof(client->selected_model), "%s", client->models[0].id);
}

static bool ParseModelListResponse(CodexRpcClient *client, const char *json)
{
    const char *data = strstr(json, "\"data\":[");
    const char *cursor = NULL;
    int parsed = 0;

    if (data == NULL) {
        return false;
    }

    cursor = strchr(data, '[');
    if (cursor == NULL) {
        return false;
    }
    cursor += 1;

    while (*cursor != '\0' && *cursor != ']' && parsed < CODEX_RPC_MAX_MODELS) {
        int depth = 0;
        bool in_string = false;
        bool escaping = false;
        const char *object_start = NULL;
        const char *object_end = NULL;
        char object_json[2048];
        size_t object_length = 0;
        char model_id[CODEX_RPC_MAX_MODEL_ID];
        char display_name[CODEX_RPC_MAX_MODEL_LABEL];

        while (*cursor != '\0' && *cursor != '{' && *cursor != ']') {
            ++cursor;
        }

        if (*cursor != '{') {
            break;
        }

        object_start = cursor;
        for (; *cursor != '\0'; ++cursor) {
            char ch = *cursor;

            if (escaping) {
                escaping = false;
                continue;
            }

            if (ch == '\\' && in_string) {
                escaping = true;
                continue;
            }

            if (ch == '"') {
                in_string = !in_string;
                continue;
            }

            if (in_string) {
                continue;
            }

            if (ch == '{') {
                depth += 1;
            } else if (ch == '}') {
                depth -= 1;
                if (depth == 0) {
                    object_end = cursor + 1;
                    break;
                }
            }
        }

        if (object_end == NULL) {
            break;
        }

        object_length = (size_t)(object_end - object_start);
        if (object_length >= sizeof(object_json)) {
            object_length = sizeof(object_json) - 1;
        }

        memcpy(object_json, object_start, object_length);
        object_json[object_length] = '\0';

        if (!JsonExtractString(object_json, "model", model_id, sizeof(model_id))) {
            cursor = object_end;
            continue;
        }

        if (!JsonExtractString(object_json, "displayName", display_name, sizeof(display_name))) {
            snprintf(display_name, sizeof(display_name), "%s", model_id);
        }

        snprintf(client->models[parsed].id, sizeof(client->models[parsed].id), "%s", model_id);
        if (strcmp(display_name, model_id) == 0) {
            snprintf(client->models[parsed].label, sizeof(client->models[parsed].label), "%s", model_id);
        } else {
            snprintf(client->models[parsed].label, sizeof(client->models[parsed].label), "%s | %s", display_name, model_id);
        }
        client->models[parsed].is_default = strstr(object_json, "\"isDefault\":true") != NULL;
        parsed += 1;
        cursor = object_end;
    }

    client->model_count = parsed;
    client->models_loaded = parsed > 0;
    client->models_loading = false;
    SyncSelectedModelIndex(client);
    return parsed > 0;
}

static void TranscriptAppend(CodexRpcClient *client, const char *text)
{
    size_t current_len = strlen(client->transcript);
    size_t available = sizeof(client->transcript) - current_len - 1;

    if (available == 0 || text[0] == '\0') {
        return;
    }

    strncat(client->transcript, text, available);
}

static void TranscriptAppendTurnEnvelope(CodexRpcClient *client, const char *prompt)
{
    if (client->transcript[0] != '\0') {
        TranscriptAppend(client, "\n\n");
    }

    TranscriptAppend(client, "You: ");
    TranscriptAppend(client, prompt);
    TranscriptAppend(client, "\nCodex: ");
}

static void CodexRpcClient_CloseFds(CodexRpcClient *client)
{
    if (client->stdin_fd >= 0) {
        close(client->stdin_fd);
        client->stdin_fd = -1;
    }

    if (client->stdout_fd >= 0) {
        close(client->stdout_fd);
        client->stdout_fd = -1;
    }

    if (client->stderr_fd >= 0) {
        close(client->stderr_fd);
        client->stderr_fd = -1;
    }
}

static void CodexRpcClient_ResetRuntimeState(CodexRpcClient *client)
{
    client->running = false;
    client->initialized = false;
    client->thread_ready = false;
    client->turn_in_flight = false;
    client->pid = -1;
}

static void ClosePipePair(int pipefd[2])
{
    if (pipefd[0] >= 0) {
        close(pipefd[0]);
        pipefd[0] = -1;
    }

    if (pipefd[1] >= 0) {
        close(pipefd[1]);
        pipefd[1] = -1;
    }
}

static void CodexRpcClient_MarkOversizeLineDiscard(
    CodexRpcClient *client,
    bool is_stderr,
    size_t *buffer_len,
    bool *discarding_oversize_line
)
{
    (void)client;
    CodexRpcClient_LogLine(is_stderr, "dropping oversized line until newline");
    *discarding_oversize_line = true;
    *buffer_len = 0;
}

static bool CodexRpcClient_SendRaw(CodexRpcClient *client, const char *line)
{
    size_t length = strlen(line);
    ssize_t written = 0;

    if (!client->running || client->stdin_fd < 0) {
        CodexRpcClient_SetError(client, "Server stdin is unavailable");
        return false;
    }

    while ((size_t)written < length) {
        ssize_t chunk = write(client->stdin_fd, line + written, length - (size_t)written);
        if (chunk < 0) {
            if (errno == EINTR) {
                continue;
            }

            CodexRpcClient_SetError(client, "write failed: %s", strerror(errno));
            return false;
        }

        written += chunk;
    }

    if (write(client->stdin_fd, "\n", 1) < 0) {
        CodexRpcClient_SetError(client, "newline write failed: %s", strerror(errno));
        return false;
    }

    return true;
}

static RequestKind CodexRpcClient_GetRequestKind(const CodexRpcClient *client, int request_id)
{
    if (request_id == client->initialize_request_id) {
        return REQUEST_KIND_INITIALIZE;
    }

    if (request_id == client->thread_start_request_id) {
        return REQUEST_KIND_THREAD_START;
    }

    if (request_id == client->turn_start_request_id) {
        return REQUEST_KIND_TURN_START;
    }

    if (request_id == client->model_list_request_id) {
        return REQUEST_KIND_MODEL_LIST;
    }

    return REQUEST_KIND_UNKNOWN;
}

static bool CodexRpcClient_SendInitialize(CodexRpcClient *client)
{
    char payload[1024];
    int request_id = client->next_request_id++;

    client->initialize_request_id = request_id;

    snprintf(
        payload,
        sizeof(payload),
        "{\"id\":%d,\"method\":\"initialize\",\"params\":{\"clientInfo\":{\"name\":\"raylib-wrapper\",\"version\":\"0.1.0\"},\"capabilities\":{\"experimentalApi\":false}}}",
        request_id
    );

    CodexRpcClient_SetStatus(client, "Starting Codex app server...");
    return CodexRpcClient_SendRaw(client, payload);
}

static bool CodexRpcClient_SendInitialized(CodexRpcClient *client)
{
    return CodexRpcClient_SendRaw(client, "{\"method\":\"initialized\"}");
}

static bool CodexRpcClient_SendThreadStart(CodexRpcClient *client)
{
    char escaped_cwd[1024];
    char escaped_model[CODEX_RPC_MAX_MODEL_ID * 2];
    char payload[2304];
    char model_fragment[320] = "";
    int request_id = client->next_request_id++;

    if (!JsonEscape(client->cwd, escaped_cwd, sizeof(escaped_cwd))) {
        CodexRpcClient_SetError(client, "cwd is too long to encode");
        return false;
    }

    if (client->selected_model[0] != '\0') {
        if (!JsonEscape(client->selected_model, escaped_model, sizeof(escaped_model))) {
            CodexRpcClient_SetError(client, "model is too long to encode");
            return false;
        }
        snprintf(model_fragment, sizeof(model_fragment), ",\"model\":\"%s\"", escaped_model);
    }

    client->thread_start_request_id = request_id;

    snprintf(
        payload,
        sizeof(payload),
        "{\"id\":%d,\"method\":\"thread/start\",\"params\":{\"cwd\":\"%s\",\"approvalPolicy\":\"never\",\"sandbox\":\"workspace-write\",\"personality\":\"pragmatic\"%s}}",
        request_id,
        escaped_cwd,
        model_fragment
    );

    CodexRpcClient_SetStatus(client, "Creating thread...");
    return CodexRpcClient_SendRaw(client, payload);
}

bool CodexRpcClient_RequestModelList(CodexRpcClient *client)
{
    char payload[256];
    int request_id = 0;

    if (!client->running || !client->initialized || client->models_loading) {
        return false;
    }

    request_id = client->next_request_id++;
    client->model_list_request_id = request_id;
    client->models_loading = true;

    snprintf(
        payload,
        sizeof(payload),
        "{\"id\":%d,\"method\":\"model/list\",\"params\":{\"limit\":%d}}",
        request_id,
        CODEX_RPC_MAX_MODELS
    );

    if (!CodexRpcClient_SendRaw(client, payload)) {
        client->models_loading = false;
        return false;
    }

    CodexRpcClient_SetStatus(client, "Loading models...");
    return true;
}

bool CodexRpcClient_SendPrompt(CodexRpcClient *client, const char *prompt)
{
    char escaped_prompt[(CODEX_RPC_MAX_PROMPT * 6)];
    char escaped_cwd[1024];
    char escaped_model[CODEX_RPC_MAX_MODEL_ID * 2];
    char payload[4352];
    char model_fragment[320] = "";
    int request_id = 0;

    if (!client->running || !client->initialized || !client->thread_ready || client->turn_in_flight) {
        return false;
    }

    if (!JsonEscape(prompt, escaped_prompt, sizeof(escaped_prompt))) {
        CodexRpcClient_SetError(client, "prompt is too long to encode");
        return false;
    }
    if (!JsonEscape(client->cwd, escaped_cwd, sizeof(escaped_cwd))) {
        CodexRpcClient_SetError(client, "cwd is too long to encode");
        return false;
    }
    if (client->selected_model[0] != '\0') {
        if (!JsonEscape(client->selected_model, escaped_model, sizeof(escaped_model))) {
            CodexRpcClient_SetError(client, "model is too long to encode");
            return false;
        }
        snprintf(model_fragment, sizeof(model_fragment), ",\"model\":\"%s\"", escaped_model);
    }

    request_id = client->next_request_id++;
    client->turn_start_request_id = request_id;
    client->turn_in_flight = true;

    snprintf(
        payload,
        sizeof(payload),
        "{\"id\":%d,\"method\":\"turn/start\",\"params\":{\"threadId\":\"%s\",\"cwd\":\"%s\",\"input\":[{\"type\":\"text\",\"text\":\"%s\"}]%s}}",
        request_id,
        client->thread_id,
        escaped_cwd,
        escaped_prompt,
        model_fragment
    );

    CodexRpcClient_SetStatus(client, "Running turn...");
    if (!CodexRpcClient_SendRaw(client, payload)) {
        client->turn_in_flight = false;
        return false;
    }

    TranscriptAppendTurnEnvelope(client, prompt);

    return true;
}

void CodexRpcClient_SelectModel(CodexRpcClient *client, int index)
{
    if (index < 0 || index >= client->model_count) {
        return;
    }

    client->selected_model_index = index;
    snprintf(client->selected_model, sizeof(client->selected_model), "%s", client->models[index].id);
    CodexRpcClient_SetStatus(client, "Selected model: %s", client->models[index].label);
}

static void CodexRpcClient_HandleResponse(CodexRpcClient *client, const char *line, int request_id)
{
    RequestKind kind = CodexRpcClient_GetRequestKind(client, request_id);

    if (strstr(line, "\"error\":") != NULL) {
        char message[CODEX_RPC_MAX_STATUS];

        if (JsonExtractString(line, "message", message, sizeof(message))) {
            CodexRpcClient_SetError(client, "%s", message);
            CodexRpcClient_SetStatus(client, "Server returned an error");
        } else {
            CodexRpcClient_SetError(client, "Server returned an error");
            CodexRpcClient_SetStatus(client, "Server returned an error");
        }

        if (kind == REQUEST_KIND_TURN_START) {
            client->turn_in_flight = false;
        }

        return;
    }

    switch (kind) {
        case REQUEST_KIND_INITIALIZE:
            client->initialized = true;
            CodexRpcClient_SetStatus(client, "Initialized. Opening thread...");
            CodexRpcClient_SendInitialized(client);
            CodexRpcClient_SendThreadStart(client);
            break;

        case REQUEST_KIND_THREAD_START:
            if (JsonExtractString(line, "id", client->thread_id, sizeof(client->thread_id))) {
                client->thread_ready = true;
                CodexRpcClient_SetStatus(client, "Thread ready");
            } else {
                CodexRpcClient_SetStatus(client, "Thread created");
            }
            CodexRpcClient_RequestModelList(client);
            break;

        case REQUEST_KIND_TURN_START:
            CodexRpcClient_SetStatus(client, "Turn started");
            break;

        case REQUEST_KIND_MODEL_LIST:
            if (!ParseModelListResponse(client, line)) {
                client->models_loading = false;
                CodexRpcClient_SetError(client, "Failed to parse model list response");
                CodexRpcClient_SetStatus(client, "Model list unavailable");
            } else if (client->selected_model_index >= 0) {
                CodexRpcClient_SetStatus(client, "Models loaded (%d). Active: %s", client->model_count, client->models[client->selected_model_index].label);
            } else {
                CodexRpcClient_SetStatus(client, "Models loaded (%d)", client->model_count);
            }
            break;

        case REQUEST_KIND_UNKNOWN:
        default:
            break;
    }
}

static void CodexRpcClient_HandleNotification(CodexRpcClient *client, const char *line)
{
    char method[128];

    if (!JsonExtractString(line, "method", method, sizeof(method))) {
        return;
    }

    if (strcmp(method, "thread/started") == 0) {
        if (JsonExtractString(line, "id", client->thread_id, sizeof(client->thread_id))) {
            client->thread_ready = true;
        }
        CodexRpcClient_SetStatus(client, "Thread ready");
        CodexRpcClient_RequestModelList(client);
        return;
    }

    if (strcmp(method, "turn/started") == 0) {
        client->turn_in_flight = true;
        CodexRpcClient_SetStatus(client, "Turn streaming...");
        return;
    }

    if (strcmp(method, "turn/completed") == 0) {
        client->turn_in_flight = false;
        CodexRpcClient_SetStatus(client, "Turn completed");
        return;
    }

    if (strcmp(method, "item/agentMessage/delta") == 0) {
        char delta[1024];

        if (JsonExtractString(line, "delta", delta, sizeof(delta))) {
            TranscriptAppend(client, delta);
        }
        return;
    }

    if (strcmp(method, "error") == 0) {
        char message[CODEX_RPC_MAX_STATUS];

        if (JsonExtractString(line, "message", message, sizeof(message))) {
            CodexRpcClient_SetError(client, "%s", message);
            CodexRpcClient_SetStatus(client, "Server error notification");
        }
    }
}

static void CodexRpcClient_HandleLine(CodexRpcClient *client, const char *line, bool is_stderr)
{
    int request_id = 0;

    if (is_stderr) {
        CodexRpcClient_LogLine(true, line);
        return;
    }

    CodexRpcClient_LogLine(false, line);

    if (JsonExtractInt(line, "id", &request_id) && (strstr(line, "\"result\":") != NULL || strstr(line, "\"error\":") != NULL)) {
        CodexRpcClient_HandleResponse(client, line, request_id);
        return;
    }

    if (strstr(line, "\"method\":") != NULL) {
        CodexRpcClient_HandleNotification(client, line);
    }
}

static void CodexRpcClient_ProcessBuffer(
    CodexRpcClient *client,
    char *buffer,
    size_t *buffer_len,
    size_t buffer_size,
    ssize_t bytes_read,
    bool is_stderr,
    bool *discarding_oversize_line
)
{
    size_t start = 0;

    if ((size_t)bytes_read + *buffer_len >= buffer_size) {
        CodexRpcClient_MarkOversizeLineDiscard(client, is_stderr, buffer_len, discarding_oversize_line);
        return;
    }

    *buffer_len += (size_t)bytes_read;
    buffer[*buffer_len] = '\0';

    if (*discarding_oversize_line) {
        char *newline = memchr(buffer, '\n', *buffer_len);

        if (newline == NULL) {
            if (*buffer_len >= buffer_size - 1) {
                *buffer_len = 0;
            }
            return;
        }

        start = (size_t)((newline - buffer) + 1);
        *discarding_oversize_line = false;
    }

    for (size_t i = 0; i < *buffer_len; ++i) {
        if (buffer[i] != '\n') {
            continue;
        }

        buffer[i] = '\0';
        if (i > start && buffer[i - 1] == '\r') {
            buffer[i - 1] = '\0';
        }

            CodexRpcClient_HandleLine(client, buffer + start, is_stderr);
        start = i + 1;
    }

    if (start > 0) {
        memmove(buffer, buffer + start, *buffer_len - start);
        *buffer_len -= start;
        buffer[*buffer_len] = '\0';
    }
}

static void CodexRpcClient_DrainFd(
    CodexRpcClient *client,
    int fd,
    char *buffer,
    size_t *buffer_len,
    size_t buffer_size,
    bool is_stderr,
    bool *discarding_oversize_line
)
{
    while (true) {
        if (*buffer_len >= buffer_size - 1) {
            CodexRpcClient_MarkOversizeLineDiscard(client, is_stderr, buffer_len, discarding_oversize_line);
        }

        ssize_t bytes_read = read(fd, buffer + *buffer_len, buffer_size - *buffer_len - 1);
        if (bytes_read < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) {
                return;
            }

            CodexRpcClient_SetError(client, "read failed: %s", strerror(errno));
            return;
        }

        if (bytes_read == 0) {
            return;
        }

        CodexRpcClient_ProcessBuffer(client, buffer, buffer_len, buffer_size, bytes_read, is_stderr, discarding_oversize_line);
    }
}

void CodexRpcClient_Init(CodexRpcClient *client, const char *cwd)
{
    memset(client, 0, sizeof(*client));

    client->pid = -1;
    client->stdin_fd = -1;
    client->stdout_fd = -1;
    client->stderr_fd = -1;
    client->next_request_id = 1;

    snprintf(client->cwd, sizeof(client->cwd), "%s", cwd != NULL ? cwd : ".");
    ResetModelListState(client, false);
    CodexRpcClient_SetStatus(client, "Press R to launch Codex app server");
}

bool CodexRpcClient_Start(CodexRpcClient *client)
{
    int stdin_pipe[2] = {-1, -1};
    int stdout_pipe[2] = {-1, -1};
    int stderr_pipe[2] = {-1, -1};
    pid_t child_pid = 0;

    if (client->running) {
        return true;
    }

    CodexRpcClient_CloseFds(client);
    CodexRpcClient_ResetRuntimeState(client);

    if (pipe(stdin_pipe) != 0 || pipe(stdout_pipe) != 0 || pipe(stderr_pipe) != 0) {
        ClosePipePair(stdin_pipe);
        ClosePipePair(stdout_pipe);
        ClosePipePair(stderr_pipe);
        CodexRpcClient_SetError(client, "pipe failed: %s", strerror(errno));
        return false;
    }

    child_pid = fork();
    if (child_pid < 0) {
        ClosePipePair(stdin_pipe);
        ClosePipePair(stdout_pipe);
        ClosePipePair(stderr_pipe);
        CodexRpcClient_SetError(client, "fork failed: %s", strerror(errno));
        return false;
    }

    if (child_pid == 0) {
        dup2(stdin_pipe[0], STDIN_FILENO);
        dup2(stdout_pipe[1], STDOUT_FILENO);
        dup2(stderr_pipe[1], STDERR_FILENO);

        close(stdin_pipe[0]);
        close(stdin_pipe[1]);
        close(stdout_pipe[0]);
        close(stdout_pipe[1]);
        close(stderr_pipe[0]);
        close(stderr_pipe[1]);

        execlp("codex", "codex", "app-server", (char *)NULL);
        _exit(127);
    }

    close(stdin_pipe[0]);
    close(stdout_pipe[1]);
    close(stderr_pipe[1]);

    client->pid = (int)child_pid;
    client->stdin_fd = stdin_pipe[1];
    client->stdout_fd = stdout_pipe[0];
    client->stderr_fd = stderr_pipe[0];
    client->running = true;
    client->initialized = false;
    client->thread_ready = false;
    client->turn_in_flight = false;
    client->thread_id[0] = '\0';
    client->last_error[0] = '\0';
    client->transcript[0] = '\0';
    client->next_request_id = 1;
    client->initialize_request_id = 0;
    client->thread_start_request_id = 0;
    client->turn_start_request_id = 0;
    client->stdout_buffer_len = 0;
    client->stdout_discarding_oversize_line = false;
    client->stderr_buffer_len = 0;
    client->stderr_discarding_oversize_line = false;
    ResetModelListState(client, true);

    if (!CodexRpcClient_SetNonBlocking(client->stdout_fd) || !CodexRpcClient_SetNonBlocking(client->stderr_fd)) {
        CodexRpcClient_SetError(client, "failed to set non-blocking pipes");
    }

    if (!CodexRpcClient_SendInitialize(client)) {
        CodexRpcClient_Stop(client);
        return false;
    }

    return true;
}

void CodexRpcClient_Update(CodexRpcClient *client)
{
    int status = 0;
    pid_t result = 0;

    if (!client->running) {
        return;
    }

    CodexRpcClient_DrainFd(
        client,
        client->stdout_fd,
        client->stdout_buffer,
        &client->stdout_buffer_len,
        sizeof(client->stdout_buffer),
        false,
        &client->stdout_discarding_oversize_line
    );

    CodexRpcClient_DrainFd(
        client,
        client->stderr_fd,
        client->stderr_buffer,
        &client->stderr_buffer_len,
        sizeof(client->stderr_buffer),
        true,
        &client->stderr_discarding_oversize_line
    );

    result = waitpid((pid_t)client->pid, &status, WNOHANG);
    if (result == (pid_t)client->pid) {
        CodexRpcClient_CloseFds(client);
        CodexRpcClient_ResetRuntimeState(client);
        CodexRpcClient_SetStatus(client, "Codex app server exited");
    }
}

void CodexRpcClient_Stop(CodexRpcClient *client)
{
    CodexRpcClient_CloseFds(client);

    if (client->running && client->pid > 0) {
        const int wait_steps = 20;
        bool exited = false;

        kill((pid_t)client->pid, SIGTERM);
        for (int i = 0; i < wait_steps; ++i) {
            pid_t result = waitpid((pid_t)client->pid, NULL, WNOHANG);
            if (result == (pid_t)client->pid) {
                exited = true;
                break;
            }
            {
                struct timespec wait_time = {0, 100 * 1000 * 1000};
                nanosleep(&wait_time, NULL);
            }
        }

        if (!exited) {
            kill((pid_t)client->pid, SIGKILL);
            waitpid((pid_t)client->pid, NULL, 0);
            CodexRpcClient_SetError(client, "server did not exit on SIGTERM; forced SIGKILL");
        }
    }

    CodexRpcClient_ResetRuntimeState(client);
}
