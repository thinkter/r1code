#define _POSIX_C_SOURCE 200809L

#include "platform_dialogs.h"

#include <stdio.h>
#include <string.h>

#if !defined(_WIN32)
#include <sys/wait.h>
#endif

static void TrimTrailingWhitespace(char *text)
{
    int length = (int)strlen(text);
    while (length > 0 && (text[length - 1] == '\n' || text[length - 1] == '\r' || text[length - 1] == ' ' || text[length - 1] == '\t')) {
        text[--length] = '\0';
    }
}

static void EscapeSingleQuotes(const char *input, char *output, size_t output_size)
{
    size_t out = 0;

    for (size_t i = 0; input[i] != '\0' && out + 1 < output_size; ++i) {
        if (input[i] == '\'') {
            const char *replacement = "'\"'\"'";
            size_t replacement_length = strlen(replacement);
            if (out + replacement_length >= output_size) {
                break;
            }
            memcpy(output + out, replacement, replacement_length);
            out += replacement_length;
        } else {
            output[out++] = input[i];
        }
    }

    output[out] = '\0';
}

static bool RunCommandCapture(const char *command, char *output, size_t output_size)
{
    FILE *pipe = NULL;
    size_t total = 0;
    int status = 0;

    output[0] = '\0';
    pipe = popen(command, "r");
    if (pipe == NULL) {
        return false;
    }

    while (total + 1 < output_size) {
        size_t chunk = fread(output + total, 1, output_size - total - 1, pipe);
        total += chunk;
        if (chunk == 0) {
            break;
        }
    }

    output[total] = '\0';
    status = pclose(pipe);
    TrimTrailingWhitespace(output);

#if !defined(_WIN32)
    if (status == -1 || !WIFEXITED(status) || WEXITSTATUS(status) != 0) {
        return false;
    }
#else
    if (status != 0) {
        return false;
    }
#endif

    return output[0] != '\0';
}

bool OpenNativeFolderPicker(const char *start_dir, char *selected_dir, size_t selected_dir_size)
{
    char escaped_dir[1024];
    char command[4096];

    EscapeSingleQuotes(start_dir, escaped_dir, sizeof(escaped_dir));

#if defined(_WIN32)
    snprintf(
        command,
        sizeof(command),
        "powershell -NoProfile -Command \"Add-Type -AssemblyName System.Windows.Forms; "
        "$dialog = New-Object System.Windows.Forms.FolderBrowserDialog; "
        "$dialog.SelectedPath = '%s'; "
        "if ($dialog.ShowDialog() -eq [System.Windows.Forms.DialogResult]::OK) { "
        "[Console]::OutputEncoding = [System.Text.Encoding]::UTF8; "
        "Write-Output $dialog.SelectedPath }\"",
        escaped_dir
    );
    return RunCommandCapture(command, selected_dir, selected_dir_size);
#elif defined(__APPLE__)
    snprintf(
        command,
        sizeof(command),
        "osascript -e 'set chosenFolder to POSIX path of (choose folder default location POSIX file \"%s\")' -e 'return chosenFolder'",
        start_dir
    );
    return RunCommandCapture(command, selected_dir, selected_dir_size);
#else
    snprintf(
        command,
        sizeof(command),
        "sh -c \"if command -v zenity >/dev/null 2>&1; then "
        "zenity --file-selection --directory --filename='%s/'; "
        "elif command -v kdialog >/dev/null 2>&1; then "
        "kdialog --getexistingdirectory '%s'; "
        "elif command -v yad >/dev/null 2>&1; then "
        "yad --file --directory --filename='%s/'; "
        "else exit 1; fi\"",
        escaped_dir,
        escaped_dir,
        escaped_dir
    );
    return RunCommandCapture(command, selected_dir, selected_dir_size);
#endif
}
