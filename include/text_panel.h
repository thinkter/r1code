#pragma once

#include <stdbool.h>

#include "raylib.h"

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
);
