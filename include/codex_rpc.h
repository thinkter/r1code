#pragma once

#include <stdbool.h>
#include <stddef.h>

#define CODEX_RPC_MAX_STATUS 256
#define CODEX_RPC_MAX_THREAD_ID 128
#define CODEX_RPC_MAX_PROMPT 1024
#define CODEX_RPC_MAX_TRANSCRIPT 16384
#define CODEX_RPC_MAX_LOG_LINES 14
#define CODEX_RPC_MAX_LOG_LINE 256

typedef struct CodexRpcClient {
    int pid;
    int stdin_fd;
    int stdout_fd;
    int stderr_fd;

    bool running;
    bool initialized;
    bool thread_ready;
    bool turn_in_flight;

    int next_request_id;
    int initialize_request_id;
    int thread_start_request_id;
    int turn_start_request_id;

    char cwd[512];
    char status[CODEX_RPC_MAX_STATUS];
    char thread_id[CODEX_RPC_MAX_THREAD_ID];
    char last_error[CODEX_RPC_MAX_STATUS];
    char transcript[CODEX_RPC_MAX_TRANSCRIPT];

    char stdout_buffer[8192];
    size_t stdout_buffer_len;
    bool stdout_discarding_oversize_line;
    char stderr_buffer[4096];
    size_t stderr_buffer_len;
    bool stderr_discarding_oversize_line;

    char log_lines[CODEX_RPC_MAX_LOG_LINES][CODEX_RPC_MAX_LOG_LINE];
    int log_line_count;
} CodexRpcClient;

void CodexRpcClient_Init(CodexRpcClient *client, const char *cwd);
bool CodexRpcClient_Start(CodexRpcClient *client);
void CodexRpcClient_Update(CodexRpcClient *client);
void CodexRpcClient_Stop(CodexRpcClient *client);
bool CodexRpcClient_SendPrompt(CodexRpcClient *client, const char *prompt);
