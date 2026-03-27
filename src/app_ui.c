#include "app_ui.h"

#include "raylib.h"
#include "raygui.h"

#include "codex_rpc.h"
#include "platform_dialogs.h"
#include "text_panel.h"
#include "ui_theme.h"

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#define UI_LOG_FONT_SIZE 16.0f

int RunApplication(void)
{
    CodexRpcClient client;
    Font ui_font = {0};
    char prompt[CODEX_RPC_MAX_PROMPT] = "Summarize what this workspace contains.";
    bool prompt_edit_mode = false;
    Vector2 transcript_scroll = {0};
    Vector2 log_scroll = {0};

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
        int log_height = available_height > 320 ? 140 : 110;
        int center_height = available_height - prompt_height - log_height - (gap * 2);
        int content_width = screen_width - (margin * 2);
        int side_width = 300;
        int left_width = 0;
        int right_x = 0;
        int prompt_y = content_top + center_height + gap;
        int log_y = prompt_y + prompt_height + gap;
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
        Rectangle log_panel = {(float)margin, (float)log_y, (float)(screen_width - (margin * 2)), (float)log_height};

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
            log_y = prompt_y + prompt_height + gap;
            prompt_box.y = (float)prompt_y;
            prompt_input.y = (float)(prompt_y + 44);
            prompt_hint.y = (float)(prompt_y + 18);
            send_button.y = (float)(prompt_y + 14);
            log_panel.y = (float)log_y;
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
                CodexRpcClient_SendPrompt(&client, prompt);
                prompt_edit_mode = false;
            }
            if (!prompt_edit_mode && IsKeyPressed(KEY_ENTER)) {
                CodexRpcClient_SendPrompt(&client, prompt);
            }

            if (IsKeyPressed(KEY_R)) {
                CodexRpcClient_Stop(&client);
                CodexRpcClient_Start(&client);
            }
        }

        CodexRpcClient_Update(&client);

        BeginDrawing();
        ClearBackground(GetColor(GuiGetStyle(DEFAULT, BACKGROUND_COLOR)));

        GuiPanel(header_panel, NULL);
        GuiLabel(header_title, "Codex CLI App Server Wrapper");
        GuiLabel(header_status, client.status);
        if (GuiButton(folder_button, "Choose Folder")) {
            char selected_dir[512];

            prompt_edit_mode = false;
            if (OpenNativeFolderPicker(client.cwd, selected_dir, sizeof(selected_dir))) {
                snprintf(client.cwd, sizeof(client.cwd), "%s", selected_dir);
                snprintf(client.status, sizeof(client.status), "Working folder updated");
            } else {
                snprintf(client.last_error, sizeof(client.last_error), "%s", "No native folder picker was available or the dialog was cancelled");
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
        GuiLine((Rectangle){state_panel.x + 12, state_panel.y + 144, state_panel.width - 24, 20}, "working folder");
        GuiSetStyle(DEFAULT, TEXT_WRAP_MODE, TEXT_WRAP_WORD);
        GuiSetStyle(DEFAULT, TEXT_ALIGNMENT_VERTICAL, TEXT_ALIGN_TOP);
        GuiLabel((Rectangle){state_panel.x + 18, state_panel.y + 170, state_panel.width - 36, 74}, client.cwd);
        GuiLine((Rectangle){state_panel.x + 12, state_panel.y + 250, state_panel.width - 24, 20}, "thread id");
        GuiLabel((Rectangle){state_panel.x + 18, state_panel.y + 276, state_panel.width - 36, 60}, client.thread_id[0] != '\0' ? client.thread_id : "(none)");
        GuiLine((Rectangle){state_panel.x + 12, state_panel.y + 342, state_panel.width - 24, 20}, "last error");
        GuiLabel((Rectangle){state_panel.x + 18, state_panel.y + 368, state_panel.width - 36, state_panel.height - 390}, client.last_error[0] != '\0' ? client.last_error : "(none)");
        GuiSetStyle(DEFAULT, TEXT_WRAP_MODE, TEXT_WRAP_NONE);
        GuiSetStyle(DEFAULT, TEXT_ALIGNMENT_VERTICAL, TEXT_ALIGN_MIDDLE);

        GuiPanel(prompt_box, "Prompt");
        GuiLabel(prompt_hint, prompt_edit_mode ? "Press Enter to commit, Esc to stop editing" : "Click the textbox to edit the next prompt");
        if (GuiButton(send_button, "Send")) {
            CodexRpcClient_SendPrompt(&client, prompt);
            prompt_edit_mode = false;
        }
        GuiSetStyle(TEXTBOX, BASE_COLOR_NORMAL, ColorToInt((Color){30, 35, 45, 255}));
        GuiSetStyle(TEXTBOX, BASE_COLOR_FOCUSED, ColorToInt((Color){44, 54, 72, 255}));
        GuiSetStyle(TEXTBOX, BASE_COLOR_PRESSED, ColorToInt((Color){44, 54, 72, 255}));
        GuiSetStyle(TEXTBOX, BORDER_COLOR_NORMAL, ColorToInt((Color){70, 84, 104, 255}));
        GuiSetStyle(TEXTBOX, BORDER_COLOR_FOCUSED, ColorToInt((Color){130, 198, 255, 255}));
        GuiSetStyle(TEXTBOX, BORDER_COLOR_PRESSED, ColorToInt((Color){130, 198, 255, 255}));
        GuiSetStyle(TEXTBOX, TEXT_COLOR_NORMAL, ColorToInt((Color){220, 226, 235, 255}));
        GuiSetStyle(TEXTBOX, TEXT_COLOR_FOCUSED, ColorToInt((Color){220, 226, 235, 255}));
        GuiSetStyle(TEXTBOX, TEXT_COLOR_PRESSED, ColorToInt((Color){220, 226, 235, 255}));
        if (GuiTextBox(prompt_input, prompt, CODEX_RPC_MAX_PROMPT, prompt_edit_mode)) {
            if (prompt_edit_mode) {
                if (IsKeyPressed(KEY_ENTER)) CodexRpcClient_SendPrompt(&client, prompt);
                prompt_edit_mode = false;
            } else {
                prompt_edit_mode = true;
            }
        }

        {
            char joined_logs[CODEX_RPC_MAX_LOG_LINES * CODEX_RPC_MAX_LOG_LINE] = {0};

            if (client.log_line_count <= 0) {
                snprintf(joined_logs, sizeof(joined_logs), "%s", "No transport log yet.");
            } else {
                for (int i = 0; i < client.log_line_count; ++i) {
                    strncat(joined_logs, client.log_lines[i], sizeof(joined_logs) - strlen(joined_logs) - 1);
                    if (i + 1 < client.log_line_count) {
                        strncat(joined_logs, "\n", sizeof(joined_logs) - strlen(joined_logs) - 1);
                    }
                }
            }

            DrawScrollTextPanel(ui_font, log_panel, "Transport Log", joined_logs, UI_LOG_FONT_SIZE, &log_scroll, log_height - 12);
        }

        GuiStatusBar(
            (Rectangle){(float)margin, (float)(screen_height - footer_height - 2), (float)(screen_width - (margin * 2)), (float)footer_height},
            prompt_edit_mode ? "raygui UI active | Enter sends | Esc exits edit | Choose Folder opens native picker | R restarts server" : "raygui UI active | Click prompt to edit | Enter sends when idle | Choose Folder opens native picker | R restarts server"
        );

        EndDrawing();
    }

    CodexRpcClient_Stop(&client);
    if (ui_font.texture.id != 0 && ui_font.texture.id != GetFontDefault().texture.id) {
        UnloadFont(ui_font);
    }
    CloseWindow();
    return 0;
}
