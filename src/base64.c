/* Base64, standard alphabet with padding. The reference writes the binary column
 * as base64, so the bytes have to match, not just the value.
 */
#include "aln.h"

#include <stddef.h>
#include <stdint.h>

static const char kAlphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

size_t aln_base64_encode(const uint8_t *input, size_t len, char *out, size_t cap) {
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
