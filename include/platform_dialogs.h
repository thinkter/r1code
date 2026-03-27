#pragma once

#include <stdbool.h>
#include <stddef.h>

bool OpenNativeFolderPicker(const char *start_dir, char *selected_dir, size_t selected_dir_size);
