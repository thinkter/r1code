#include "ui_theme.h"

#include "raygui.h"

#include <stddef.h>

#define UI_SMALL_FONT_SIZE 18.0f
#define MAX_FONT_CODEPOINTS 4096

static int BuildUiCodepointSet(int *codepoints, int capacity)
{
    const int ranges[][2] = {
        {0x0020, 0x00FF},
        {0x0100, 0x024F},
        {0x2000, 0x206F},
        {0x20A0, 0x20CF},
        {0x2190, 0x21FF},
        {0x2200, 0x22FF},
        {0x2500, 0x257F},
        {0x2580, 0x259F},
        {0x25A0, 0x25FF},
        {0x2600, 0x26FF},
        {0x2700, 0x27BF},
        {0x2B00, 0x2BFF},
        {0x1F300, 0x1F64F},
        {0x1F680, 0x1F6FF},
        {0x1F900, 0x1FAFF},
    };
    int count = 0;

    for (size_t r = 0; r < (sizeof(ranges) / sizeof(ranges[0])); ++r) {
        for (int cp = ranges[r][0]; cp <= ranges[r][1] && count < capacity; ++cp) {
            codepoints[count++] = cp;
        }
    }

    return count;
}

Font LoadUiFont(void)
{
    const char *font_paths[] = {
        "/usr/share/fonts/TTF/DejaVuSans.ttf",
        "/usr/share/fonts/TTF/JetBrainsMono-Regular.ttf",
    };
    int codepoints[MAX_FONT_CODEPOINTS];
    int codepoint_count = BuildUiCodepointSet(codepoints, MAX_FONT_CODEPOINTS);

    for (size_t i = 0; i < (sizeof(font_paths) / sizeof(font_paths[0])); ++i) {
        if (FileExists(font_paths[i])) {
            Font font = LoadFontEx(font_paths[i], 32, codepoints, codepoint_count);
            if (font.texture.id != 0) {
                SetTextureFilter(font.texture, TEXTURE_FILTER_BILINEAR);
                return font;
            }
        }
    }

    return GetFontDefault();
}

void ConfigureUiTheme(Font ui_font)
{
    GuiSetFont(ui_font);
    GuiSetStyle(DEFAULT, TEXT_SIZE, (int)UI_SMALL_FONT_SIZE);
    GuiSetStyle(DEFAULT, TEXT_SPACING, 1);
    GuiSetStyle(DEFAULT, BACKGROUND_COLOR, ColorToInt((Color){24, 28, 36, 255}));
    GuiSetStyle(DEFAULT, BASE_COLOR_NORMAL, ColorToInt((Color){30, 35, 45, 255}));
    GuiSetStyle(DEFAULT, BASE_COLOR_FOCUSED, ColorToInt((Color){44, 54, 72, 255}));
    GuiSetStyle(DEFAULT, BASE_COLOR_PRESSED, ColorToInt((Color){44, 54, 72, 255}));
    GuiSetStyle(DEFAULT, BORDER_COLOR_NORMAL, ColorToInt((Color){70, 84, 104, 255}));
    GuiSetStyle(DEFAULT, BORDER_COLOR_FOCUSED, ColorToInt((Color){130, 198, 255, 255}));
    GuiSetStyle(DEFAULT, BORDER_COLOR_PRESSED, ColorToInt((Color){130, 198, 255, 255}));
    GuiSetStyle(DEFAULT, TEXT_COLOR_NORMAL, ColorToInt((Color){220, 226, 235, 255}));
    GuiSetStyle(DEFAULT, TEXT_COLOR_FOCUSED, ColorToInt((Color){220, 226, 235, 255}));
    GuiSetStyle(DEFAULT, TEXT_COLOR_PRESSED, ColorToInt((Color){220, 226, 235, 255}));
    GuiSetStyle(DEFAULT, LINE_COLOR, ColorToInt((Color){70, 84, 104, 255}));
    GuiSetStyle(STATUSBAR, BASE_COLOR_NORMAL, ColorToInt((Color){33, 39, 51, 255}));
    GuiSetStyle(STATUSBAR, TEXT_COLOR_NORMAL, ColorToInt(RAYWHITE));
    GuiSetStyle(BUTTON, BASE_COLOR_NORMAL, ColorToInt((Color){44, 54, 72, 255}));
    GuiSetStyle(BUTTON, BASE_COLOR_FOCUSED, ColorToInt((Color){55, 67, 88, 255}));
    GuiSetStyle(BUTTON, BASE_COLOR_PRESSED, ColorToInt((Color){36, 45, 60, 255}));
    GuiSetStyle(BUTTON, BORDER_COLOR_NORMAL, ColorToInt((Color){130, 198, 255, 255}));
    GuiSetStyle(BUTTON, BORDER_COLOR_FOCUSED, ColorToInt((Color){130, 198, 255, 255}));
    GuiSetStyle(BUTTON, BORDER_COLOR_PRESSED, ColorToInt((Color){130, 198, 255, 255}));
    GuiSetStyle(BUTTON, TEXT_COLOR_NORMAL, ColorToInt(RAYWHITE));
    GuiSetStyle(TEXTBOX, TEXT_ALIGNMENT, TEXT_ALIGN_LEFT);
    GuiSetStyle(TEXTBOX, BORDER_WIDTH, 2);
    GuiSetStyle(TEXTBOX, TEXT_PADDING, 10);
}
