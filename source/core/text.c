#include <stdint.h>
#include <string.h>

#include "text.h"

static bool text_valid_utf8(const unsigned char* bytes, size_t size) {
    for(size_t i = 0; i < size;) {
        uint32_t code = bytes[i++];
        size_t extra = 0;
        uint32_t minimum = 0;
        if(code < 0x80) {
            if((code < 0x20 && code != '\t' && code != '\n' && code != '\r') || code == 0x7F) {
                return false;
            }
            continue;
        } else if(code >= 0xC2 && code <= 0xDF) {
            code &= 0x1F;
            extra = 1;
            minimum = 0x80;
        } else if(code >= 0xE0 && code <= 0xEF) {
            code &= 0x0F;
            extra = 2;
            minimum = 0x800;
        } else if(code >= 0xF0 && code <= 0xF4) {
            code &= 0x07;
            extra = 3;
            minimum = 0x10000;
        } else {
            return false;
        }
        if(extra > size - i) {
            return false;
        }
        for(size_t j = 0; j < extra; j++) {
            unsigned char next = bytes[i++];
            if((next & 0xC0) != 0x80) {
                return false;
            }
            code = (code << 6) | (next & 0x3F);
        }
        if(code < minimum || code > 0x10FFFF || (code >= 0xD800 && code <= 0xDFFF)) {
            return false;
        }
    }
    return true;
}

bool text_is_txt(const char* path) {
    if(path == NULL) {
        return false;
    }
    const char* extension = strrchr(path, '.');
    return extension != NULL && strlen(extension) == 4
           && (extension[1] == 't' || extension[1] == 'T')
           && (extension[2] == 'x' || extension[2] == 'X')
           && (extension[3] == 't' || extension[3] == 'T');
}

bool text_decode(const void* bytes, size_t size, char* output, size_t capacity, text_format* format) {
    if(bytes == NULL || output == NULL || format == NULL || size > TEXT_FILE_MAX_SIZE) {
        return false;
    }
    const unsigned char* input = bytes;
    if(!text_valid_utf8(input, size)) {
        return false;
    }
    format->bom = size >= 3 && memcmp(input, "\xEF\xBB\xBF", 3) == 0;
    format->newline = TEXT_NEWLINE_LF;
    bool foundNewline = false;
    size_t position = 0;
    for(size_t i = format->bom ? 3 : 0; i < size; i++) {
        unsigned char value = input[i];
        if(value == '\r') {
            bool crlf = i + 1 < size && input[i + 1] == '\n';
            if(!foundNewline) {
                format->newline = crlf ? TEXT_NEWLINE_CRLF : TEXT_NEWLINE_CR;
                foundNewline = true;
            }
            if(crlf) {
                i++;
            }
            value = '\n';
        } else if(value == '\n' && !foundNewline) {
            foundNewline = true;
        }
        if(position + 1 >= capacity) {
            return false;
        }
        output[position++] = value;
    }
    if(position >= capacity) {
        return false;
    }
    output[position] = '\0';
    return true;
}

bool text_encode(const char* text, text_format format, void* output, size_t capacity, size_t* size) {
    if(text == NULL || output == NULL || size == NULL || format.newline > TEXT_NEWLINE_CR) {
        return false;
    }
    size_t length = strlen(text);
    if(!text_valid_utf8((const unsigned char*) text, length)) {
        return false;
    }
    unsigned char* bytes = output;
    size_t position = 0;
    if(format.bom) {
        if(capacity < 3) {
            return false;
        }
        memcpy(bytes, "\xEF\xBB\xBF", 3);
        position = 3;
    }
    for(size_t i = 0; i < length; i++) {
        char value = text[i];
        if(value == '\r' && i + 1 < length && text[i + 1] == '\n') {
            i++;
        }
        if(value == '\n' || value == '\r') {
            size_t required = format.newline == TEXT_NEWLINE_CRLF ? 2 : 1;
            if(required > capacity - position) {
                return false;
            }
            bytes[position++] = format.newline == TEXT_NEWLINE_LF ? '\n' : '\r';
            if(required == 2) {
                bytes[position++] = '\n';
            }
        } else {
            if(position >= capacity) {
                return false;
            }
            bytes[position++] = value;
        }
    }
    *size = position;
    return true;
}

size_t text_index_lines(const char* text, size_t* offsets, size_t capacity) {
    if(text == NULL || offsets == NULL || capacity == 0) {
        return 0;
    }
    size_t count = 1;
    offsets[0] = 0;
    for(size_t i = 0; text[i] != '\0'; i++) {
        if(text[i] == '\n') {
            if(count >= capacity) {
                return 0;
            }
            offsets[count++] = i + 1;
        }
    }
    return count;
}

bool text_line_span(const char* text, size_t line, size_t* start, size_t* length) {
    if(text == NULL || start == NULL || length == NULL) {
        return false;
    }
    const char* current = text;
    for(size_t i = 0; i < line; i++) {
        const char* end = strchr(current, '\n');
        if(end == NULL) {
            return false;
        }
        current = end + 1;
    }
    const char* end = strchr(current, '\n');
    *start = current - text;
    *length = end != NULL ? (size_t) (end - current) : strlen(current);
    return true;
}

static bool text_replace_span(char* text, size_t capacity, size_t start, size_t length, const char* replacement) {
    if(text == NULL || replacement == NULL) {
        return false;
    }
    size_t currentSize = strlen(text);
    size_t replacementSize = strlen(replacement);
    if(start > currentSize || length > currentSize - start || currentSize - length >= capacity
       || replacementSize >= capacity - (currentSize - length)
       || !text_valid_utf8((const unsigned char*) replacement, replacementSize)) {
        return false;
    }
    memmove(text + start + replacementSize, text + start + length, currentSize - start - length + 1);
    memcpy(text + start, replacement, replacementSize);
    return true;
}

bool text_replace_line(char* text, size_t capacity, size_t line, const char* replacement) {
    size_t start = 0;
    size_t length = 0;
    return text_line_span(text, line, &start, &length) && text_replace_span(text, capacity, start, length, replacement);
}

bool text_insert_line(char* text, size_t capacity, size_t line, bool after) {
    size_t start = 0;
    size_t length = 0;
    return text_line_span(text, line, &start, &length)
           && text_replace_span(text, capacity, start + (after ? length : 0), 0, "\n");
}

bool text_delete_line(char* text, size_t capacity, size_t line) {
    size_t start = 0;
    size_t length = 0;
    if(!text_line_span(text, line, &start, &length)) {
        return false;
    }
    if(text[start + length] == '\n') {
        length++;
    } else if(start > 0) {
        start--;
        length++;
    }
    return text_replace_span(text, capacity, start, length, "");
}

bool text_find_line(const char* text, const char* query, size_t startLine, size_t* foundLine) {
    if(text == NULL || query == NULL || query[0] == '\0' || foundLine == NULL) {
        return false;
    }
    size_t start = 0;
    size_t length = 0;
    if(!text_line_span(text, startLine, &start, &length)) {
        start = 0;
    }
    const char* match = strstr(text + start, query);
    if(match == NULL && start > 0) {
        match = strstr(text, query);
    }
    if(match == NULL) {
        return false;
    }
    *foundLine = 0;
    for(const char* p = text; p < match; p++) {
        if(*p == '\n') {
            (*foundLine)++;
        }
    }
    return true;
}
