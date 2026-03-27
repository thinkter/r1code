#pragma once

#include "raylib.h"

void DrawScrollTextPanel(
    Font font,
    Rectangle bounds,
    const char *title,
    const char *text,
    float font_size,
    Vector2 *scroll,
    int min_content_height
);
