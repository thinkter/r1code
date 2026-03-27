#include "text_panel.h"

#include "raygui.h"

#include <stdbool.h>
#include <string.h>

#define UI_FONT_SPACING 0.0f

typedef struct WrappedLineInfo {
    int start;
    int end;
    int next_start;
    bool ended_on_newline;
} WrappedLineInfo;

static float MeasureUiTextWidth(Font font, const char *text, float font_size)
{
    return MeasureTextEx(font, text, font_size, UI_FONT_SPACING).x;
}

static bool IsWrapBreakCodepoint(int codepoint)
{
    return codepoint == ' ' || codepoint == '/' || codepoint == '\\' || codepoint == '-' || codepoint == '_';
}

static WrappedLineInfo GetWrappedLineInfo(Font font, const char *text, int start, float max_width, float font_size)
{
    WrappedLineInfo line = {start, start, start, false};
    int cursor = start;
    int best_end = start;
    int last_break = start;

    if (text[start] == '\0') {
        return line;
    }

    while (text[cursor] != '\0') {
        int codepoint_size = 0;
        int codepoint = GetCodepointNext(text + cursor, &codepoint_size);
        int next = cursor + codepoint_size;
        char candidate[1024];
        int candidate_length = next - start;

        if (codepoint == '\n') {
            line.end = cursor;
            line.next_start = next;
            line.ended_on_newline = true;
            return line;
        }

        if (candidate_length >= (int)sizeof(candidate)) {
            candidate_length = (int)sizeof(candidate) - 1;
        }

        memcpy(candidate, text + start, (size_t)candidate_length);
        candidate[candidate_length] = '\0';

        if (MeasureUiTextWidth(font, candidate, font_size) <= max_width || best_end == start) {
            best_end = next;
            if (IsWrapBreakCodepoint(codepoint)) {
                last_break = next;
            }
            cursor = next;
            continue;
        }

        break;
    }

    if (last_break > start && text[cursor] != '\0') {
        best_end = last_break;
    }

    line.end = best_end;
    line.next_start = best_end;
    return line;
}

static int CountWrappedLines(Font font, const char *text, float max_width, float font_size)
{
    int start = 0;
    int line_count = 0;

    if (text == NULL || text[0] == '\0') {
        return 1;
    }

    while (text[start] != '\0') {
        WrappedLineInfo wrapped_line = GetWrappedLineInfo(font, text, start, max_width, font_size);

        if (wrapped_line.end <= start) {
            break;
        }

        line_count += 1;
        start = wrapped_line.next_start;
    }

    return line_count > 0 ? line_count : 1;
}

void DrawScrollTextPanel(
    Font font,
    Rectangle bounds,
    const char *title,
    const char *text,
    float font_size,
    Vector2 *scroll,
    int min_content_height
)
{
    Rectangle view = {0};
    int previous_wrap_mode = GuiGetStyle(DEFAULT, TEXT_WRAP_MODE);
    int previous_vertical_alignment = GuiGetStyle(DEFAULT, TEXT_ALIGNMENT_VERTICAL);
    int previous_text_size = GuiGetStyle(DEFAULT, TEXT_SIZE);
    int previous_spacing = GuiGetStyle(DEFAULT, TEXT_SPACING);
    int line_count = 0;
    int content_height = 0;
    Rectangle content_bounds = {0};
    Rectangle label_bounds = {0};
    float line_height = font_size + 6.0f;
    const char *display_text = (text != NULL && text[0] != '\0') ? text : "(empty)";

    GuiSetStyle(DEFAULT, TEXT_WRAP_MODE, TEXT_WRAP_WORD);
    GuiSetStyle(DEFAULT, TEXT_ALIGNMENT_VERTICAL, TEXT_ALIGN_TOP);
    GuiSetStyle(DEFAULT, TEXT_SIZE, (int)font_size);
    GuiSetStyle(DEFAULT, TEXT_SPACING, 1);

    line_count = CountWrappedLines(font, display_text, bounds.width - 44.0f, font_size);
    content_height = (int)(line_count * line_height) + 20;
    if (content_height < min_content_height) {
        content_height = min_content_height;
    }

    content_bounds = (Rectangle){0, 0, bounds.width - 24.0f, (float)content_height};
    GuiScrollPanel(bounds, title, content_bounds, scroll, &view);

    BeginScissorMode((int)view.x, (int)view.y, (int)view.width, (int)view.height);
    label_bounds = (Rectangle){view.x + scroll->x + 10.0f, view.y + scroll->y + 10.0f, content_bounds.width - 20.0f, content_bounds.height - 20.0f};
    GuiLabel(label_bounds, display_text);
    EndScissorMode();

    GuiSetStyle(DEFAULT, TEXT_WRAP_MODE, previous_wrap_mode);
    GuiSetStyle(DEFAULT, TEXT_ALIGNMENT_VERTICAL, previous_vertical_alignment);
    GuiSetStyle(DEFAULT, TEXT_SIZE, previous_text_size);
    GuiSetStyle(DEFAULT, TEXT_SPACING, previous_spacing);
}
