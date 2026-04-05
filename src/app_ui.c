#include "app_ui.h"

#include "raylib.h"
#include "raygui.h"

#include "codex_rpc.h"
#include "platform_dialogs.h"
#include "text_panel.h"
#include "ui_theme.h"

#include <pthread.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

typedef struct FolderPickerTask {
    pthread_t thread;
    atomic_bool active;
    atomic_bool done;
    bool success;
    char start_dir[512];
    char selected_dir[512];
} FolderPickerTask;

static bool SubmitPrompt(CodexRpcClient *client, char *prompt, bool *prompt_edit_mode)
{
    if (!CodexRpcClient_SendPrompt(client, prompt)) {
        return false;
    }

    prompt[0] = '\0';
    *prompt_edit_mode = false;
    return true;
}

static void *FolderPickerThreadMain(void *userdata)
{
    FolderPickerTask *task = (FolderPickerTask *)userdata;

    task->success = OpenNativeFolderPicker(task->start_dir, task->selected_dir, sizeof(task->selected_dir));
    atomic_store_explicit(&task->done, true, memory_order_release);
    return NULL;
}

static bool StartFolderPickerTask(FolderPickerTask *task, const char *cwd)
{
    if (atomic_load_explicit(&task->active, memory_order_acquire)) {
        return false;
    }

    snprintf(task->start_dir, sizeof(task->start_dir), "%s", cwd);
    task->selected_dir[0] = '\0';
    task->success = false;
    atomic_store_explicit(&task->done, false, memory_order_relaxed);

    if (pthread_create(&task->thread, NULL, FolderPickerThreadMain, task) != 0) {
        return false;
    }

    atomic_store_explicit(&task->active, true, memory_order_release);
    return true;
}

static bool PollFolderPickerTask(FolderPickerTask *task)
{
    if (!atomic_load_explicit(&task->active, memory_order_acquire)) {
        return false;
    }

    if (!atomic_load_explicit(&task->done, memory_order_acquire)) {
        return false;
    }

    pthread_join(task->thread, NULL);
    atomic_store_explicit(&task->active, false, memory_order_release);
    return true;
}

static void JoinFolderPickerTaskIfActive(FolderPickerTask *task)
{
    if (!atomic_load_explicit(&task->active, memory_order_acquire)) {
        return;
    }

    pthread_join(task->thread, NULL);
    atomic_store_explicit(&task->active, false, memory_order_release);
}

static void BuildModelOptions(const CodexRpcClient *client, char *output, size_t output_size)
{
    size_t out = 0;

    if (output_size == 0) {
        return;
    }

    output[0] = '\0';
    for (int i = 0; i < client->model_count && out + 1 < output_size; ++i) {
        size_t label_length = strlen(client->models[i].label);
        size_t writable = output_size - out - 1;

        if (i > 0 && writable > 1) {
            output[out++] = ';';
            writable = output_size - out - 1;
        }

        if (label_length > writable) {
            label_length = writable;
        }

        memcpy(output + out, client->models[i].label, label_length);
        out += label_length;
    }

    output[out] = '\0';
}

int RunApplication(void)
{
    CodexRpcClient client;
    Font ui_font = {0};
    char prompt[CODEX_RPC_MAX_PROMPT] = "Summarize what this workspace contains.";
    bool prompt_edit_mode = false;
    Vector2 transcript_scroll = {0};
    FolderPickerTask folder_picker_task = {0};
    char model_options_cache[(CODEX_RPC_MAX_MODELS * (CODEX_RPC_MAX_MODEL_LABEL + 1)) + 1] = "";
    int model_dropdown_index = -1;
    bool model_dropdown_edit_mode = false;
    int last_model_count = -1;

    SetConfigFlags(FLAG_VSYNC_HINT | FLAG_WINDOW_HIGHDPI | FLAG_WINDOW_RESIZABLE);
    InitWindow(1440, 900, "Codex RPC Wrapper");
    SetTargetFPS(60);

    ui_font = LoadUiFont();
    ConfigureUiTheme(ui_font);
    CodexRpcClient_Init(&client, GetWorkingDirectory());
    CodexRpcClient_Start(&client);

    while (!WindowShouldClose()) {
        int screen_width = GetScreenWidth();
        int screen_height = GetScreenHeight();
        int margin = 20;
        int gap = 20;
        int header_height = 68;
        int footer_height = 24;
        int content_top = margin + header_height + margin;
        int content_bottom = screen_height - margin;
        int available_height = content_bottom - content_top;
        int prompt_height = 96;
        int center_height = available_height - prompt_height - gap;
        int content_width = screen_width - (margin * 2);
        int side_width = 300;
        int left_width = 0;
        int right_x = 0;
        int prompt_y = content_top + center_height + gap;
        Rectangle header_panel = {(float)margin, (float)margin, (float)(screen_width - (margin * 2)), (float)header_height};
        Rectangle header_title = {(float)(margin + 16), (float)(margin + 8), 460.0f, 24.0f};
        Rectangle header_status = {(float)(margin + 16), (float)(margin + 34), (float)(screen_width - (margin * 2) - 260), 20.0f};
        Rectangle folder_button = {(float)(screen_width - margin - 220), (float)(margin + 16), 200.0f, 30.0f};
        Rectangle transcript_panel = {(float)margin, (float)content_top, (float)left_width, (float)center_height};
        Rectangle state_panel = {(float)right_x, (float)content_top, (float)side_width, (float)center_height};
        Rectangle prompt_box = {(float)margin, (float)prompt_y, (float)(screen_width - (margin * 2)), (float)prompt_height};
        Rectangle prompt_input = {(float)(margin + 18), (float)(prompt_y + 44), (float)(screen_width - (margin * 2) - 36), 36.0f};
        Rectangle prompt_hint = {(float)(margin + 18), (float)(prompt_y + 18), (float)(screen_width - (margin * 2) - 160), 18.0f};
        Rectangle send_button = {(float)(screen_width - margin - 118), (float)(prompt_y + 14), 100.0f, 26.0f};

        if (content_width < 900) {
            side_width = 240;
        } else if (content_width > 1400) {
            side_width = 320;
        }

        if (side_width > content_width - 280) {
            side_width = content_width - 280;
        }

        if (side_width < 220) {
            side_width = 220;
        }

        left_width = content_width - gap - side_width;
        right_x = margin + left_width + gap;
        transcript_panel.width = (float)left_width;
        state_panel.x = (float)right_x;
        state_panel.width = (float)side_width;

        if (center_height < 180) {
            center_height = 180;
            prompt_y = content_top + center_height + gap;
            prompt_box.y = (float)prompt_y;
            prompt_input.y = (float)(prompt_y + 44);
            prompt_hint.y = (float)(prompt_y + 18);
            send_button.y = (float)(prompt_y + 14);
        }

        if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
            Vector2 mouse = GetMousePosition();
            bool prompt_hit = CheckCollisionPointRec(mouse, prompt_input);

            if (!prompt_hit) prompt_edit_mode = false;
        }

        {
            bool ctrl_down = IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL);
            if (prompt_edit_mode && IsKeyPressed(KEY_ESCAPE)) prompt_edit_mode = false;
            if (prompt_edit_mode && ctrl_down && IsKeyPressed(KEY_ENTER)) {
                SubmitPrompt(&client, prompt, &prompt_edit_mode);
            }
            if (!prompt_edit_mode && IsKeyPressed(KEY_ENTER)) {
                SubmitPrompt(&client, prompt, &prompt_edit_mode);
            }

            if (IsKeyPressed(KEY_R)) {
                CodexRpcClient_Stop(&client);
                CodexRpcClient_Start(&client);
            }
        }

        CodexRpcClient_Update(&client);

        if (client.model_count != last_model_count) {
            BuildModelOptions(&client, model_options_cache, sizeof(model_options_cache));
            last_model_count = client.model_count;
        }

        if (client.selected_model_index >= 0) {
            model_dropdown_index = client.selected_model_index;
        }

        if (PollFolderPickerTask(&folder_picker_task)) {
            if (folder_picker_task.success) {
                snprintf(client.cwd, sizeof(client.cwd), "%s", folder_picker_task.selected_dir);
                snprintf(client.status, sizeof(client.status), "Working folder updated");
            } else {
                snprintf(client.last_error, sizeof(client.last_error), "%s", "No native folder picker was available or the dialog was cancelled");
            }
        }

        BeginDrawing();
        ClearBackground(GetColor(GuiGetStyle(DEFAULT, BACKGROUND_COLOR)));

        GuiPanel(header_panel, NULL);
        GuiLabel(header_title, "Codex CLI App Server Wrapper");
        GuiLabel(header_status, client.status);
        if (GuiButton(folder_button, "Choose Folder")) {
            prompt_edit_mode = false;
            if (StartFolderPickerTask(&folder_picker_task, client.cwd)) {
                snprintf(client.status, sizeof(client.status), "Opening folder picker...");
            } else {
                snprintf(client.last_error, sizeof(client.last_error), "%s", "Folder picker already running or failed to start");
            }
        }

        DrawScrollTextPanel(
            ui_font,
            transcript_panel,
            "Assistant Stream",
            client.transcript[0] != '\0' ? client.transcript : "No streamed assistant output yet. Press Enter to start a turn.",
            20.0f,
            &transcript_scroll,
            center_height - 12
        );

        GuiPanel(state_panel, "RPC State");
        GuiLabel((Rectangle){state_panel.x + 18, state_panel.y + 40, state_panel.width - 36, 20}, TextFormat("running: %s", client.running ? "yes" : "no"));
        GuiLabel((Rectangle){state_panel.x + 18, state_panel.y + 64, state_panel.width - 36, 20}, TextFormat("initialized: %s", client.initialized ? "yes" : "no"));
        GuiLabel((Rectangle){state_panel.x + 18, state_panel.y + 88, state_panel.width - 36, 20}, TextFormat("thread ready: %s", client.thread_ready ? "yes" : "no"));
        GuiLabel((Rectangle){state_panel.x + 18, state_panel.y + 112, state_panel.width - 36, 20}, TextFormat("turn in flight: %s", client.turn_in_flight ? "yes" : "no"));
        GuiLine((Rectangle){state_panel.x + 12, state_panel.y + 144, state_panel.width - 24, 20}, "model");
        GuiLine((Rectangle){state_panel.x + 12, state_panel.y + 210, state_panel.width - 24, 20}, "working folder");
        GuiSetStyle(DEFAULT, TEXT_WRAP_MODE, TEXT_WRAP_WORD);
        GuiSetStyle(DEFAULT, TEXT_ALIGNMENT_VERTICAL, TEXT_ALIGN_TOP);
        GuiLabel((Rectangle){state_panel.x + 18, state_panel.y + 236, state_panel.width - 36, 74}, client.cwd);
        GuiLine((Rectangle){state_panel.x + 12, state_panel.y + 316, state_panel.width - 24, 20}, "thread id");
        GuiLabel((Rectangle){state_panel.x + 18, state_panel.y + 342, state_panel.width - 36, 60}, client.thread_id[0] != '\0' ? client.thread_id : "(none)");
        GuiLine((Rectangle){state_panel.x + 12, state_panel.y + 408, state_panel.width - 24, 20}, "last error");
        GuiLabel((Rectangle){state_panel.x + 18, state_panel.y + 434, state_panel.width - 36, state_panel.height - 456}, client.last_error[0] != '\0' ? client.last_error : "(none)");
        GuiSetStyle(DEFAULT, TEXT_WRAP_MODE, TEXT_WRAP_NONE);
        GuiSetStyle(DEFAULT, TEXT_ALIGNMENT_VERTICAL, TEXT_ALIGN_MIDDLE);

        if (client.model_count > 0 && model_options_cache[0] != '\0') {
            Rectangle model_dropdown = {state_panel.x + 18, state_panel.y + 170, state_panel.width - 36, 28};
            if (GuiDropdownBox(model_dropdown, model_options_cache, &model_dropdown_index, model_dropdown_edit_mode)) {
                if (model_dropdown_edit_mode) {
                    CodexRpcClient_SelectModel(&client, model_dropdown_index);
                }
                model_dropdown_edit_mode = !model_dropdown_edit_mode;
            }
        } else {
            const char *model_status = client.models_loading ? "Loading available models..." : "No model list yet. Press R to reconnect.";
            GuiLabel((Rectangle){state_panel.x + 18, state_panel.y + 170, state_panel.width - 36, 40}, model_status);
        }

        GuiPanel(prompt_box, "Prompt");
        GuiLabel(prompt_hint, prompt_edit_mode ? "Press Enter to commit, Esc to stop editing" : "Click the textbox to edit the next prompt");
        if (GuiButton(send_button, "Send")) {
            SubmitPrompt(&client, prompt, &prompt_edit_mode);
        }
        GuiSetStyle(TEXTBOX, BASE_COLOR_NORMAL, ColorToInt((Color){6, 6, 6, 255}));
        GuiSetStyle(TEXTBOX, BASE_COLOR_FOCUSED, ColorToInt((Color){12, 12, 12, 255}));
        GuiSetStyle(TEXTBOX, BASE_COLOR_PRESSED, ColorToInt((Color){18, 18, 18, 255}));
        GuiSetStyle(TEXTBOX, BORDER_COLOR_NORMAL, ColorToInt((Color){40, 40, 40, 255}));
        GuiSetStyle(TEXTBOX, BORDER_COLOR_FOCUSED, ColorToInt((Color){74, 74, 74, 255}));
        GuiSetStyle(TEXTBOX, BORDER_COLOR_PRESSED, ColorToInt((Color){96, 96, 96, 255}));
        GuiSetStyle(TEXTBOX, TEXT_COLOR_NORMAL, ColorToInt((Color){235, 235, 235, 255}));
        GuiSetStyle(TEXTBOX, TEXT_COLOR_FOCUSED, ColorToInt((Color){235, 235, 235, 255}));
        GuiSetStyle(TEXTBOX, TEXT_COLOR_PRESSED, ColorToInt((Color){255, 255, 255, 255}));
        if (GuiTextBox(prompt_input, prompt, CODEX_RPC_MAX_PROMPT, prompt_edit_mode)) {
            if (prompt_edit_mode) {
                if (IsKeyPressed(KEY_ENTER)) {
                    SubmitPrompt(&client, prompt, &prompt_edit_mode);
                } else {
                    prompt_edit_mode = false;
                }
            } else {
                prompt_edit_mode = true;
            }
        }

        GuiStatusBar(
            (Rectangle){(float)margin, (float)(screen_height - footer_height - 2), (float)(screen_width - (margin * 2)), (float)footer_height},
            prompt_edit_mode ? "raygui UI active | Enter sends | Esc exits edit | Choose Folder opens native picker | R restarts server" : "raygui UI active | Click prompt to edit | Enter sends when idle | Choose Folder opens native picker | R restarts server"
        );

        EndDrawing();
    }

    CodexRpcClient_Stop(&client);
    JoinFolderPickerTaskIfActive(&folder_picker_task);
    if (ui_font.texture.id != 0 && ui_font.texture.id != GetFontDefault().texture.id) {
        UnloadFont(ui_font);
    }
    CloseWindow();
    return 0;
}
