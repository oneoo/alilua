/*
 ok-file-formats
 Copyright (c) 2014 David Brackeen
 
 This software is provided 'as-is', without any express or implied warranty.
 In no event will the authors be held liable for any damages arising from the
 use of this software. Permission is granted to anyone to use this software
 for any purpose, including commercial applications, and to alter it and
 redistribute it freely, subject to the following restrictions:
 
 1. The origin of this software must not be misrepresented; you must not
    claim that you wrote the original software. If you use this software in a
    product, an acknowledgment in the product documentation would be appreciated
    but is not required.
 2. Altered source versions must be plainly marked as such, and must not be
    misrepresented as being the original software.
 3. This notice may not be removed or altered from any source distribution.
 */
#include "ok_mo.h"
#include <memory.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h> // For vsnprintf
#include <stdlib.h>

#ifndef min
#define min(a, b) ((a) < (b) ? (a) : (b))
#endif

int file_input_func(void *user_data, unsigned char *buffer, const int count) {
    FILE *fp = (FILE *)user_data;
    if (buffer && count > 0) {
        return (int)fread(buffer, 1, (size_t)count, fp);
    }
    else if (fseek(fp, count, SEEK_CUR) == 0) {
        return count;
    }
    else {
        return 0;
    }
}

// See https://www.gnu.org/software/gettext/manual/html_node/MO-Files.html

// MARK: MO helper functions

struct ok_mo_string {
    char *key;
    char *value;
    int num_plural_variants;
};

typedef struct {
    ok_mo *mo;
    
    uint8_t *key_offset_buffer;
    uint8_t *value_offset_buffer;
    
    // Input
    void *input_data;
    ok_mo_input_func input_func;
    
} mo_decoder;

static void decode_mo2(mo_decoder *decoder);

static void ok_mo_cleanup(ok_mo *mo) {
    if (mo) {
        if (mo->strings) {
            uint32_t i = 0;
            for (i = 0; i < mo->num_strings; i++) {
                free(mo->strings[i].key);
                free(mo->strings[i].value);
            }
            free(mo->strings);
            mo->strings = NULL;
        }
        mo->num_strings = 0;
    }
}

__attribute__((__format__ (__printf__, 2, 3)))
static void ok_mo_error(ok_mo *mo, const char *format, ... ) {
    if (mo) {
        ok_mo_cleanup(mo);
        if (format) {
            va_list args;
            va_start(args, format);
            vsnprintf(mo->error_message, sizeof(mo->error_message), format, args);
            va_end(args);
        }
    }
}

static void decode_mo(ok_mo *mo, void *input_data, ok_mo_input_func input_func) {
    if (mo) {
        mo_decoder *decoder = calloc(1, sizeof(mo_decoder));
        if (!decoder) {
            ok_mo_error(mo, "Couldn't allocate decoder.");
            return;
        }
        decoder->mo = mo;
        decoder->input_data = input_data;
        decoder->input_func = input_func;
        
        decode_mo2(decoder);
        
        if (decoder->key_offset_buffer) {
            free(decoder->key_offset_buffer);
        }
        if (decoder->value_offset_buffer) {
            free(decoder->value_offset_buffer);
        }
        free(decoder);
    }
}

static bool ok_read(mo_decoder *decoder, uint8_t *data, const int length) {
    if (decoder->input_func(decoder->input_data, data, length) == length) {
        return true;
    }
    else {
        ok_mo_error(decoder->mo, "Read error: error calling input function.");
        return false;
    }
}

static bool ok_seek(mo_decoder *decoder, const int length) {
    return ok_read(decoder, NULL, length);
}

// MARK: Public API

ok_mo *ok_mo_read(void *user_data, ok_mo_input_func input_func) {
    ok_mo *mo = calloc(1, sizeof(ok_mo));
    if (input_func) {
        decode_mo(mo, user_data, input_func);
    }
    else {
        ok_mo_error(mo, "Invalid argument: input_func is NULL");
    }
    return mo;
}

void ok_mo_free(ok_mo *mo) {
    if (mo) {
        ok_mo_cleanup(mo);
        free(mo);
    }
}

// MARK: Decoding

static inline uint16_t read16(const uint8_t *data, const bool little_endian) {
    if (little_endian) {
        return (uint16_t)((data[1] << 8) | data[0]);
    }
    else {
        return (uint16_t)((data[0] << 8) | data[1]);
    }
}

static inline uint32_t read32(const uint8_t *data, const bool little_endian) {
    if (little_endian) {
        return (uint32_t)((data[3] << 24) | (data[2] << 16) | (data[1] << 8) | data[0]);
    }
    else {
        return (uint32_t)((data[0] << 24) | (data[1] << 16) | (data[2] << 8) | data[3]);
    }
}

static void decode_mo2(mo_decoder *decoder) {
    ok_mo *mo = decoder->mo;
    uint8_t header[20];
    if (!ok_read(decoder, header, sizeof(header))) {
        return;
    }
    
    // Magic number
    uint32_t magic = read32(header, true);
    bool little_endian;
    if (magic == 0x950412de) {
        little_endian = true;
    }
    else if (magic == 0xde120495) {
        little_endian = false;
    }
    else {
        ok_mo_error(mo, "Not a gettext MO file");
        return;
    }
    
    // Header
    const uint16_t major_version = read16(header + 4, little_endian);
    //const uint16_t minor_version = read16(header + 6, little_endian); // ignore minor_version
    mo->num_strings = read32(header + 8, little_endian);
    const uint32_t key_offset = read32(header + 12, little_endian);
    const uint32_t value_offset = read32(header + 16, little_endian);
    
    if (!(major_version == 0 || major_version == 1)) {
        ok_mo_error(mo, "Not a gettext MO file (version %d)", major_version);
        return;
    }
    
    if (mo->num_strings == 0) {
        ok_mo_error(mo, "No strings found");
        return;
    }
    
    mo->strings = calloc(mo->num_strings, sizeof(struct ok_mo_string));
    decoder->key_offset_buffer = malloc(8 * mo->num_strings);
    decoder->value_offset_buffer = malloc(8 * mo->num_strings);
    if (!mo->strings || !decoder->key_offset_buffer || !decoder->value_offset_buffer) {
        ok_mo_error(mo, "Couldn't allocate arrays");
        return;
    }
    
    // Read offsets and lengths
    // Using "tell" because the seek functions only support relative seeking.
    int tell = sizeof(header);
    if (!ok_seek(decoder, key_offset - tell)) {
        return;
    }
    if (!ok_read(decoder, decoder->key_offset_buffer, 8 * mo->num_strings)) {
        ok_mo_error(mo, "Couldn't get key offsets");
        return;
    }
    tell = key_offset + 8 * mo->num_strings;
    if (!ok_seek(decoder, value_offset - tell)) {
        return;
    }
    if (!ok_read(decoder, decoder->value_offset_buffer, 8 * mo->num_strings)) {
        ok_mo_error(mo, "Couldn't get value offsets");
        return;
    }
    tell = value_offset + 8 * mo->num_strings;
    
    // Read keys
    // Assumes keys are sorted, per the spec.
    uint32_t i = 0;
    for (i = 0; i < mo->num_strings; i++) {
        uint32_t length = read32(decoder->key_offset_buffer + 8 * i, little_endian);
        uint32_t offset = read32(decoder->key_offset_buffer + 8 * i + 4, little_endian);
        
        mo->strings[i].key = malloc(length + 1);
        if (!mo->strings[i].key) {
            ok_mo_error(mo, "Couldn't allocate strings");
            return;
        }
        if (!ok_seek(decoder, offset - tell)) {
            return;
        }
        if (!ok_read(decoder, (uint8_t*)mo->strings[i].key, length + 1)) {
            return;
        }
        tell = offset + length + 1;
    }
    
    // Read values
    for (i = 0; i < mo->num_strings; i++) {
        uint32_t length = read32(decoder->value_offset_buffer + 8 * i, little_endian);
        uint32_t offset = read32(decoder->value_offset_buffer + 8 * i + 4, little_endian);
        
        mo->strings[i].value = malloc(length + 1);
        if (!mo->strings[i].value) {
            ok_mo_error(mo, "Couldn't allocate strings");
            return;
        }
        if (!ok_seek(decoder, offset - tell)) {
            return;
        }
        if (!ok_read(decoder, (uint8_t*)mo->strings[i].value, length + 1)) {
            return;
        }
        // Count the zeros. It is the number of plural variants.
        mo->strings[i].num_plural_variants = 0;
        const char *ch = mo->strings[i].value;
        const char *end = mo->strings[i].value + length;
        while (ch < end) {
            if (*ch++ == 0) {
                mo->strings[i].num_plural_variants++;
            }
        }
        tell = offset + length + 1;
    }
}

// MARK: Getters

static int bsearch_strcmp(const void *s1, const void *s2) {
    const char *key = s1;
    const struct ok_mo_string * elem = s2;
    return strcmp(key, (elem)->key);
}

static struct ok_mo_string *find_value(ok_mo *mo, const char *context, const char *key) {
    if (!mo || !key) {
        return NULL;
    }
    else if (!context) {
        return bsearch(key, mo->strings, mo->num_strings, sizeof(mo->strings[0]), bsearch_strcmp);
    }
    else {
        // Complete key is (context + EOT + key)
        const size_t context_length = strlen(context);
        const size_t complete_key_length = context_length + 1 + strlen(key) + 1;
        char * complete_key = malloc(complete_key_length);
        if (!complete_key) {
            return NULL;
        }
        strcpy(complete_key, context);
        complete_key[context_length] = 4; // EOT
        strcpy(complete_key + context_length + 1, key);
        complete_key[complete_key_length] = 0;
        struct ok_mo_string *r = bsearch(complete_key, mo->strings, mo->num_strings,
                                         sizeof(mo->strings[0]), bsearch_strcmp);
        free(complete_key);
        return r;
    }
}

const char *ok_mo_value(ok_mo *mo, const char *key) {
    return ok_mo_value_in_context(mo, NULL, key);
}

const char *ok_mo_plural_value(ok_mo *mo, const char *key, const char *plural_key, const int n) {
    return ok_mo_plural_value_in_context(mo, NULL, key, plural_key, n);
}

const char *ok_mo_value_in_context(ok_mo *mo, const char *context, const char *key) {
    struct ok_mo_string *s = find_value(mo, context, key);
    return s ? s->value : key;
}

static int get_plural_index(const int num_variants, const int n) {
    // This is probably too simple for some languages
    return n <= 0 ? num_variants : min(n-1, num_variants);
}

const char *ok_mo_plural_value_in_context(ok_mo *mo, const char *context, const char *key, const char *plural_key,
                                          const int n) {
    struct ok_mo_string *s = find_value(mo, context, key);
    if (s) {
        // This is probably too simple for some languages
        const int plural_index = get_plural_index(s->num_plural_variants, n);
        const char *v = s->value;
        int i = 0;
        for (i = 0; i < plural_index; i++) {
            while (*v++ != 0) { }
        }
        return v;
    }
    else {
        if (get_plural_index(1, n) == 0) {
            return key;
        }
        else {
            return plural_key;
        }
    }
}

// MARK: Unicode

unsigned int ok_utf8_strlen(const char *utf8) {
    // Might consider faster version of this if needed.
    // See http://www.daemonology.net/blog/2008-06-05-faster-utf8-strlen.html
    unsigned int len = 0;
    if (utf8) {
        const unsigned char *in = (const unsigned char *)utf8;
        while (*in != 0) {
            int skip;
            if (*in < 0xc0) {
                skip = 0;
            }
            else if (*in < 0xe0) {
                skip = 1;
            }
            else if (*in < 0xf0) {
                skip = 2;
            }
            else {
                skip = 3;
            }
            // Sanity check: check for malformed string
            int i = 0;
            for (i = 0; i < skip; i++) {
                in++;
                if (*in < 128) {
                    break;
                }
            }
            len++;
            in++;
        }
    }
    return len;
}

unsigned int ok_utf8_to_unicode(const char *utf8, uint32_t *dest, const unsigned int n) {
    if (!utf8 || !dest || n == 0) {
        return 0;
    }
    
    const unsigned char *in = (const unsigned char *)utf8;
    unsigned int len = 0;
    while (len < n && *in != 0) {
        if (*in < 0xc0) {
            dest[len] = in[0];
            in++;
        }
        else if (*in < 0xe0) {
            dest[len] = ((in[0] & 0x1f) << 6) | (in[1] & 0x3f);
            in += 2;
        }
        else if (*in < 0xf0) {
            dest[len] = ((in[0] & 0x0f) << 12) | ((in[1] & 0x3f) << 6) | (in[2] & 0x3f);
            in += 3;
        }
        else {
            dest[len] = ((in[0] & 0x07) << 18) | ((in[1] & 0x3f) << 6) | ((in[2] & 0x3f) << 6) | (in[3] & 0x3f);
            in += 4;
        }
        len++;
    }
    dest[len] = 0;
    return len;
}
