#pragma once

#include <stdbool.h>
#include <stddef.h>

#define CODEX_RPC_MAX_STATUS 256
#define CODEX_RPC_MAX_THREAD_ID 128
#define CODEX_RPC_MAX_PROMPT 1024
#define CODEX_RPC_MAX_TRANSCRIPT 1048576
#define CODEX_RPC_MAX_PLAN_TEXT 65536
#define CODEX_RPC_MAX_ACTIVITY_TEXT 262144
#define CODEX_RPC_MAX_DIFF_TEXT 131072
#define CODEX_RPC_MAX_MODELS 24
#define CODEX_RPC_MAX_MODEL_ID 128
#define CODEX_RPC_MAX_MODEL_LABEL 160
#define CODEX_RPC_MAX_REASONING_EFFORTS 6
#define CODEX_RPC_MAX_REASONING_EFFORT_ID 16
#define CODEX_RPC_MAX_REASONING_EFFORT_LABEL 96
#define CODEX_RPC_MAX_ACTIVE_ITEMS 64
#define CODEX_RPC_MAX_ITEM_ID 128

typedef struct CodexRpcReasoningEffortInfo {
    char id[CODEX_RPC_MAX_REASONING_EFFORT_ID];
    char label[CODEX_RPC_MAX_REASONING_EFFORT_LABEL];
    bool is_default;
} CodexRpcReasoningEffortInfo;

typedef struct CodexRpcModelInfo {
    char id[CODEX_RPC_MAX_MODEL_ID];
    char label[CODEX_RPC_MAX_MODEL_LABEL];
    bool is_default;
    CodexRpcReasoningEffortInfo reasoning_efforts[CODEX_RPC_MAX_REASONING_EFFORTS];
    int reasoning_effort_count;
    int default_reasoning_effort_index;
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
    int thread_read_request_id;

    char cwd[512];
    char status[CODEX_RPC_MAX_STATUS];
    char thread_id[CODEX_RPC_MAX_THREAD_ID];
    char last_error[CODEX_RPC_MAX_STATUS];
    char transcript[CODEX_RPC_MAX_TRANSCRIPT];
    char plan_text[CODEX_RPC_MAX_PLAN_TEXT];
    char activity_text[CODEX_RPC_MAX_ACTIVITY_TEXT];
    char diff_text[CODEX_RPC_MAX_DIFF_TEXT];
    unsigned int transcript_version;
    unsigned int plan_version;
    unsigned int activity_version;
    unsigned int diff_version;
    char selected_model[CODEX_RPC_MAX_MODEL_ID];
    char selected_reasoning_effort[CODEX_RPC_MAX_REASONING_EFFORT_ID];

    char stdout_buffer[524288];
    size_t stdout_buffer_len;
    bool stdout_discarding_oversize_line;
    char stderr_buffer[16384];
    size_t stderr_buffer_len;
    bool stderr_discarding_oversize_line;
    char active_item_ids[CODEX_RPC_MAX_ACTIVE_ITEMS][CODEX_RPC_MAX_ITEM_ID];
    int active_item_kinds[CODEX_RPC_MAX_ACTIVE_ITEMS];
    bool active_item_opened[CODEX_RPC_MAX_ACTIVE_ITEMS];
    int active_item_count;

    CodexRpcModelInfo models[CODEX_RPC_MAX_MODELS];
    int model_count;
    int selected_model_index;
    int selected_reasoning_effort_index;
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
void CodexRpcClient_SelectReasoningEffort(CodexRpcClient *client, int index);
