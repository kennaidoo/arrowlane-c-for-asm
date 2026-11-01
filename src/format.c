/* The float formatter, on its own so it can be tested against the reference's
 * output for thousands of values instead of the eight columns of one fixture.
 *
 * Two rules, both taken from the reference's own output rather than from printf:
 *
 * 1. The digits are the shortest sequence that reads back as the same double.
 * 2. The layout is plain notation when the decimal exponent of the leading digit
 *    is -5 to 15, and scientific outside that, with an unpadded signed exponent:
 *    "1e+16", "1e-7", "-9.119925651242736e-309".
 *
 * printf disagrees with rule 2 in two places. Its %g switches to scientific at
 * exponent -5 and pads exponents to two digits, so 0.0000216730536470823 came out as
 * 2.16730536470823e-05. The corpus test found 26 such values out of 4499.
 *
 * Round-tripping is monotonic in precision, so the shortest precision is found by
 * halving the range: about five attempts, where counting upward can take seventeen.
 */
#include "aln.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define ALN_PLAIN_LOWEST (-5)
#define ALN_PLAIN_HIGHEST 15

size_t aln_format_double(char *out, size_t cap, double value) {
    if (value == 0.0) {
        return (size_t)snprintf(out, cap, signbit(value) ? "-0.0" : "0.0");
    }
    if (isnan(value) || isinf(value)) {
        return 0; /* the lane has no form for these */
    }

    /* Most doubles need 15 to 17 significant digits, so those are tried first and
     * the general search only runs when a shorter form exists. */
    char text[40];
    char candidate[40];
    int found = 0;
    for (int digits = 17; digits >= 15; --digits) {
        snprintf(candidate, sizeof candidate, "%.*e", digits - 1, value);
        if (strtod(candidate, NULL) != value) {
            break;
        }
        memcpy(text, candidate, sizeof text);
        found = digits;
    }
    if (found == 0) {
        /* Even 17 digits did not round-trip, which a double always should. */
        snprintf(text, sizeof text, "%.16e", value);
    } else if (found == 15) {
        /* 15 digits worked, so a shorter form may exist: halve the range below. */
        int low = 1;
        int high = 14;
        while (low <= high) {
            const int middle = (low + high) / 2;
            snprintf(candidate, sizeof candidate, "%.*e", middle - 1, value);
            if (strtod(candidate, NULL) == value) {
                memcpy(text, candidate, sizeof text);
                high = middle - 1;
            } else {
                low = middle + 1;
            }
        }
    }

    const char *cursor = text;
    const int negative = (*cursor == '-');
    if (negative) {
        ++cursor;
    }
    char digits[24];
    int count = 0;
    for (; *cursor != 'e' && *cursor != '\0'; ++cursor) {
        if (*cursor >= '0' && *cursor <= '9' && count < (int)sizeof digits) {
            digits[count++] = *cursor;
        }
    }
    const int exponent = (*cursor == 'e') ? atoi(cursor + 1) : 0;
    while (count > 1 && digits[count - 1] == '0') {
        --count;
    }

    size_t used = 0;
    const size_t limit = (cap == 0u) ? 0u : cap - 1u;
#define ALN_PUT(character)                                                                  \
    do {                                                                                    \
        if (used >= limit) {                                                                \
            return 0;                                                                       \
        }                                                                                   \
        out[used++] = (character);                                                          \
    } while (0)

    if (negative) {
        ALN_PUT('-');
    }
    if (exponent >= ALN_PLAIN_LOWEST && exponent <= ALN_PLAIN_HIGHEST) {
        if (exponent >= 0) {
            const int whole = exponent + 1;
            for (int index = 0; index < whole; ++index) {
                ALN_PUT(index < count ? digits[index] : '0');
            }
            ALN_PUT('.');
            if (count > whole) {
                for (int index = whole; index < count; ++index) {
                    ALN_PUT(digits[index]);
                }
            } else {
                ALN_PUT('0');
            }
        } else {
            ALN_PUT('0');
            ALN_PUT('.');
            for (int index = 0; index < -exponent - 1; ++index) {
                ALN_PUT('0');
            }
            for (int index = 0; index < count; ++index) {
                ALN_PUT(digits[index]);
            }
        }
    } else {
        ALN_PUT(digits[0]);
        if (count > 1) {
            ALN_PUT('.');
            for (int index = 1; index < count; ++index) {
                ALN_PUT(digits[index]);
            }
        }
        ALN_PUT('e');
        ALN_PUT(exponent < 0 ? '-' : '+');
        char exponent_text[8];
        const int length =
            snprintf(exponent_text, sizeof exponent_text, "%d", exponent < 0 ? -exponent : exponent);
        for (int index = 0; index < length; ++index) {
            ALN_PUT(exponent_text[index]);
        }
    }
#undef ALN_PUT
    out[used] = '\0';
    return used;
}
