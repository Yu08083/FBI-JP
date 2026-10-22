#pragma once

#include <stdbool.h>
#include <stddef.h>

#define TEXT_FILE_MAX_SIZE (32 * 1024)
#define TEXT_KEYBOARD_MAX_UNITS TEXT_FILE_MAX_SIZE
#define TEXT_KEYBOARD_BUFFER_SIZE (3 * TEXT_KEYBOARD_MAX_UNITS + 1)

typedef enum {
    TEXT_NEWLINE_LF,
    TEXT_NEWLINE_CRLF,
    TEXT_NEWLINE_CR
} text_newline;

typedef struct {
    bool bom;
    text_newline newline;
} text_format;

bool text_is_txt(const char* path);
bool text_decode(const void* bytes, size_t size, char* output, size_t capacity, text_format* format);
bool text_encode(const char* text, text_format format, void* output, size_t capacity, size_t* size);
