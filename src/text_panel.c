#include "text_panel.h"

#include "raygui.h"
#include "raylib.h"

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#define UI_FONT_SPACING 0.0f

// Draw text with word wrap inside a rectangle
// Based on raylib example: https://www.raylib.com/examples.html
static void DrawTextBoxed(Font font, const char *text, Rectangle rec, float fontSize, float spacing, bool wordWrap, Color tint)
{
    int length = TextLength(text);
    float textOffsetY = 0;
    float textOffsetX = 0.0f;
    float scaleFactor = fontSize / (float)font.baseSize;
    
    enum { MEASURE_STATE = 0, DRAW_STATE = 1 };
    int state = wordWrap ? MEASURE_STATE : DRAW_STATE;
    
    int startLine = -1;
    int endLine = -1;
    int lastk = -1;
    
    for (int i = 0, k = 0; i < length; i++, k++)
    {
        int codepointByteCount = 0;
        int codepoint = GetCodepointNext(&text[i], &codepointByteCount);
        int index = GetGlyphIndex(font, codepoint);
        
        if (codepoint == 0x3f) codepointByteCount = 1;
        i += (codepointByteCount - 1);
        
        float glyphWidth = 0;
        if (codepoint != '\n')
        {
            glyphWidth = (font.glyphs[index].advanceX == 0) ?
                font.recs[index].width * scaleFactor :
                font.glyphs[index].advanceX * scaleFactor;
            
            if (i + 1 < length) glyphWidth += spacing;
        }
        
        if (state == MEASURE_STATE)
        {
            if ((codepoint == ' ') || (codepoint == '\t') || (codepoint == '\n')) endLine = i;
            
            if ((textOffsetX + glyphWidth) > rec.width)
            {
                endLine = (endLine < 1) ? i : endLine;
                if (i == endLine) endLine -= codepointByteCount;
                if ((startLine + codepointByteCount) == endLine) endLine = (i - codepointByteCount);
                
                state = !state;
            }
            else if ((codepoint == '\n') || (i + 1 == length))
            {
                endLine = (codepoint == '\n') ? i : i + 1;
                state = !state;
            }
            else if (startLine < 0)
            {
                startLine = i;
            }
            
            if (state == DRAW_STATE)
            {
                textOffsetX = 0;
                i = startLine;
                glyphWidth = 0;
                
                int tmp = lastk;
                lastk = k - 1;
                k = tmp;
            }
        }
        else
        {
            if (codepoint == '\n')
            {
                textOffsetY += fontSize + 6.0f;  // Line spacing
                textOffsetX = 0;
                startLine = -1;
                endLine = -1;
                lastk = k;
                state = wordWrap ? MEASURE_STATE : DRAW_STATE;
            }
            else
            {
                if ((codepoint != ' ') && (codepoint != '\t'))
                {
                    DrawTextCodepoint(font, codepoint, 
                        (Vector2){ rec.x + textOffsetX, rec.y + textOffsetY }, 
                        fontSize, tint);
                }
                
                if (i == endLine)
                {
                    textOffsetY += fontSize + 6.0f;  // Line spacing
                    textOffsetX = 0;
                    startLine = -1;
                    endLine = -1;
                    lastk = k;
                    state = wordWrap ? MEASURE_STATE : DRAW_STATE;
                }
                else
                {
                    textOffsetX += glyphWidth;
                }
            }
        }
    }
}

typedef struct WrappedLineInfo {
    int start;
    int end;
    int next_start;
    bool ended_on_newline;
} WrappedLineInfo;

typedef struct WrapMetricsCacheEntry {
    bool used;
    uint32_t key_hash;
    unsigned int version;
    float width;
    float font_size;
    int line_count;
} WrapMetricsCacheEntry;

static WrapMetricsCacheEntry g_wrap_cache[4] = {0};

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

static uint32_t HashText(const char *text)
{
    uint32_t hash = 2166136261u;

    if (text == NULL) {
        return hash;
    }

    for (size_t i = 0; text[i] != '\0'; ++i) {
        hash ^= (uint32_t)(unsigned char)text[i];
        hash *= 16777619u;
    }

    return hash;
}

static int GetCachedLineCount(Font font, const char *title, const char *text, unsigned int text_version, float max_width, float font_size)
{
    uint32_t key_hash = HashText(title);
    size_t slot = (size_t)(key_hash % (uint32_t)(sizeof(g_wrap_cache) / sizeof(g_wrap_cache[0])));
    WrapMetricsCacheEntry *entry = &g_wrap_cache[slot];

    if (
        entry->used &&
        entry->key_hash == key_hash &&
        entry->version == text_version &&
        entry->width == max_width &&
        entry->font_size == font_size
    ) {
        return entry->line_count;
    }

    entry->used = true;
    entry->key_hash = key_hash;
    entry->version = text_version;
    entry->width = max_width;
    entry->font_size = font_size;
    entry->line_count = CountWrappedLines(font, text, max_width, font_size);
    return entry->line_count;
}

void DrawScrollTextPanel(
    Font font,
    Rectangle bounds,
    const char *title,
    const char *text,
    unsigned int text_version,
    float font_size,
    Vector2 *scroll,
    int min_content_height,
    bool *auto_follow
)
{
    // raygui uses RAYGUI_WINDOWBOX_STATUSBAR_HEIGHT (24) for title bar
    #define TITLE_BAR_HEIGHT 24
    
    Rectangle view = {0};
    int previous_wrap_mode = GuiGetStyle(DEFAULT, TEXT_WRAP_MODE);
    int previous_vertical_alignment = GuiGetStyle(DEFAULT, TEXT_ALIGNMENT_VERTICAL);
    int previous_text_size = GuiGetStyle(DEFAULT, TEXT_SIZE);
    int previous_spacing = GuiGetStyle(DEFAULT, TEXT_SPACING);
    int line_count = 0;
    int content_height = 0;
    Rectangle content_bounds = {0};
    float line_height = font_size + 6.0f;
    const char *display_text = (text != NULL && text[0] != '\0') ? text : "(empty)";
    
    // Account for scrollbar width and border
    int scrollbar_width = GuiGetStyle(LISTVIEW, SCROLLBAR_WIDTH);
    int border_width = GuiGetStyle(DEFAULT, BORDER_WIDTH);
    float text_area_width = bounds.width - scrollbar_width - (border_width * 2) - 20.0f;

    GuiSetStyle(DEFAULT, TEXT_WRAP_MODE, TEXT_WRAP_WORD);
    GuiSetStyle(DEFAULT, TEXT_ALIGNMENT_VERTICAL, TEXT_ALIGN_TOP);
    GuiSetStyle(DEFAULT, TEXT_SIZE, (int)font_size);
    GuiSetStyle(DEFAULT, TEXT_SPACING, 1);

    // Calculate content height based on wrapped text
    line_count = GetCachedLineCount(font, title, display_text, text_version, text_area_width, font_size);
    content_height = (int)(line_count * line_height) + 40;  // padding for top and bottom
    
    // min_content_height is just for visual appearance when content is short
    // It should NOT be used if actual content is larger
    if (content_height < min_content_height) {
        content_height = min_content_height;
    }

    // Content bounds define the scrollable area size
    // The width should match what GuiScrollPanel expects
    content_bounds = (Rectangle){0, 0, bounds.width - (border_width * 2), (float)content_height};

    // Calculate effective panel height (accounting for title bar if present)
    float effective_panel_height = bounds.height;
    if (title != NULL && title[0] != '\0') {
        effective_panel_height -= (TITLE_BAR_HEIGHT + 1);
    }
    
    // Calculate max scroll
    float max_scroll_y = content_bounds.height - effective_panel_height + (border_width * 2);
    if (max_scroll_y < 0.0f) {
        max_scroll_y = 0.0f;
    }

    // Handle auto-follow: scroll to bottom as new content arrives
    bool follow_bottom = (auto_follow != NULL) ? *auto_follow : false;

    // If following bottom, set scroll to max BEFORE GuiScrollPanel processes input
    if (follow_bottom && max_scroll_y > 0.0f) {
        scroll->y = -max_scroll_y;
    }
    
    // Remember what we set it to (this is what scroll would be if no user input)
    float expected_scroll_y = scroll->y;

    // Draw the scroll panel - this handles mouse wheel and scrollbar interaction internally
    // IMPORTANT: GuiScrollPanel consumes GetMouseWheelMove(), so don't call it before this!
    GuiScrollPanel(bounds, title, content_bounds, scroll, &view);

    // Detect if user scrolled to disable auto-follow
    // User scrolled if GuiScrollPanel changed scroll from what we set it to
    if (auto_follow != NULL) {
        if (scroll->y != expected_scroll_y) {
            // User scrolled via mouse wheel or scrollbar - disable auto-follow
            *auto_follow = false;
        }
        
        // Re-enable auto-follow if scrolled to bottom
        if (max_scroll_y > 0.0f) {
            float current_scroll = -scroll->y;
            if ((max_scroll_y - current_scroll) <= line_height) {
                *auto_follow = true;
            }
        }
    }

    // Draw text content within the clipped view area
    // NOTE: We use DrawTextEx directly instead of GuiLabel because raygui clips text
    // that exceeds the label bounds height, which breaks scrolling for long content.
    BeginScissorMode((int)view.x, (int)view.y, (int)view.width, (int)view.height);
    {
        Vector2 text_pos = {
            view.x + scroll->x + 10.0f,
            view.y + scroll->y + 10.0f
        };
        float wrap_width = view.width - 20.0f;
        Color text_color = GetColor(GuiGetStyle(LABEL, TEXT_COLOR_NORMAL));
        
        // Draw text with word wrap using raylib's DrawTextEx
        // We need to manually handle word wrapping since DrawTextEx doesn't wrap
        DrawTextBoxed(font, display_text, 
            (Rectangle){text_pos.x, text_pos.y, wrap_width, content_bounds.height},
            font_size, UI_FONT_SPACING, true, text_color);
    }
    EndScissorMode();

    GuiSetStyle(DEFAULT, TEXT_WRAP_MODE, previous_wrap_mode);
    GuiSetStyle(DEFAULT, TEXT_ALIGNMENT_VERTICAL, previous_vertical_alignment);
    GuiSetStyle(DEFAULT, TEXT_SIZE, previous_text_size);
    GuiSetStyle(DEFAULT, TEXT_SPACING, previous_spacing);
    
    #undef TITLE_BAR_HEIGHT
}
