/* Base64, standard alphabet with padding. The reference writes the binary column
 * as base64, so the bytes have to match, not just the value.
 */
#include "aln.h"

#include <stddef.h>
#include <stdint.h>

static const char kAlphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

size_t aln_base64_encode(const unsigned char *input, size_t len, char *out, size_t cap) {
    const size_t needed = ((len + 2) / 3) * 4;
    if (cap < needed + 1) {
        return 0;
    }
    size_t written = 0;
    size_t index = 0;
    while (index + 3 <= len) {
        const uint32_t value = ((uint32_t)input[index] << 16) | ((uint32_t)input[index + 1] << 8) |
                               (uint32_t)input[index + 2];
        out[written++] = kAlphabet[(value >> 18) & 0x3F];
        out[written++] = kAlphabet[(value >> 12) & 0x3F];
        out[written++] = kAlphabet[(value >> 6) & 0x3F];
        out[written++] = kAlphabet[value & 0x3F];
        index += 3;
    }
    if (index < len) {
        uint32_t value = (uint32_t)input[index] << 16;
        int remaining = (int)(len - index);
        if (remaining == 2) {
            value |= (uint32_t)input[index + 1] << 8;
        }
        out[written++] = kAlphabet[(value >> 18) & 0x3F];
        out[written++] = kAlphabet[(value >> 12) & 0x3F];
        out[written++] = (remaining == 2) ? kAlphabet[(value >> 6) & 0x3F] : '=';
        out[written++] = '=';
    }
    out[written] = '\0';
    return written;
}

static int b64_value(char character) {
    if (character >= 'A' && character <= 'Z') {
        return character - 'A';
    }
    if (character >= 'a' && character <= 'z') {
        return character - 'a' + 26;
    }
    if (character >= '0' && character <= '9') {
        return character - '0' + 52;
    }
    if (character == '+') {
        return 62;
    }
    if (character == '/') {
        return 63;
    }
    return -1;
}

size_t aln_base64_decode(const char *text, size_t len, unsigned char *out, size_t cap) {
    if (len == 0 || (len % 4u) != 0u) {
        return 0;
    }
    size_t padding = 0;
    if (text[len - 1] == '=') {
        ++padding;
        if (text[len - 2] == '=') {
            ++padding;
        }
    }
    const size_t produced = (len / 4u) * 3u - padding;
    if (produced > cap) {
        return 0;
    }
    size_t written = 0;
    for (size_t index = 0; index < len; index += 4u) {
        const int last_quad = (index + 4u == len);
        int values[4];
        for (int slot = 0; slot < 4; ++slot) {
            const char character = text[index + (size_t)slot];
            if (character == '=') {
                /* Padding is only legal at the end of the last quad. */
                if (!last_quad || slot < 2) {
                    return 0;
                }
                if (slot == 2 && text[index + 3u] != '=') {
                    return 0;
                }
                values[slot] = 0;
            } else {
                values[slot] = b64_value(character);
                if (values[slot] < 0) {
                    return 0;
                }
            }
        }
        out[written++] = (uint8_t)((values[0] << 2) | (values[1] >> 4));
        if (written < produced) {
            out[written++] = (uint8_t)(((values[1] & 0xF) << 4) | (values[2] >> 2));
        }
        if (written < produced) {
            out[written++] = (uint8_t)(((values[2] & 0x3) << 6) | values[3]);
        }
    }
    return written;
}
