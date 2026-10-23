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
size_t text_index_lines(const char* text, size_t* offsets, size_t capacity);
bool text_line_span(const char* text, size_t line, size_t* start, size_t* length);
bool text_replace_line(char* text, size_t capacity, size_t line, const char* replacement);
bool text_insert_line(char* text, size_t capacity, size_t line, bool after);
bool text_delete_line(char* text, size_t capacity, size_t line);
bool text_find_line(const char* text, const char* query, size_t startLine, size_t* foundLine);
