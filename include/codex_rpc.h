#pragma once

#include <stdbool.h>
#include <stddef.h>

#define CODEX_RPC_MAX_STATUS 256
#define CODEX_RPC_MAX_THREAD_ID 128
#define CODEX_RPC_MAX_PROMPT 1024
#define CODEX_RPC_MAX_TRANSCRIPT 16384
#define CODEX_RPC_MAX_MODELS 24
#define CODEX_RPC_MAX_MODEL_ID 128
#define CODEX_RPC_MAX_MODEL_LABEL 160

typedef struct CodexRpcModelInfo {
    char id[CODEX_RPC_MAX_MODEL_ID];
    char label[CODEX_RPC_MAX_MODEL_LABEL];
    bool is_default;
} CodexRpcModelInfo;

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
    int model_list_request_id;

    char cwd[512];
    char status[CODEX_RPC_MAX_STATUS];
    char thread_id[CODEX_RPC_MAX_THREAD_ID];
    char last_error[CODEX_RPC_MAX_STATUS];
    char transcript[CODEX_RPC_MAX_TRANSCRIPT];
    char selected_model[CODEX_RPC_MAX_MODEL_ID];

    char stdout_buffer[8192];
    size_t stdout_buffer_len;
    bool stdout_discarding_oversize_line;
    char stderr_buffer[4096];
    size_t stderr_buffer_len;
    bool stderr_discarding_oversize_line;

    CodexRpcModelInfo models[CODEX_RPC_MAX_MODELS];
    int model_count;
    int selected_model_index;
    bool models_loading;
    bool models_loaded;
} CodexRpcClient;

void CodexRpcClient_Init(CodexRpcClient *client, const char *cwd);
bool CodexRpcClient_Start(CodexRpcClient *client);
void CodexRpcClient_Update(CodexRpcClient *client);
void CodexRpcClient_Stop(CodexRpcClient *client);
bool CodexRpcClient_SendPrompt(CodexRpcClient *client, const char *prompt);
bool CodexRpcClient_RequestModelList(CodexRpcClient *client);
void CodexRpcClient_SelectModel(CodexRpcClient *client, int index);
