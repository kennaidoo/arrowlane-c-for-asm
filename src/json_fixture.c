/* The NDJSON writer for the benchmark fixture, and the JSON string escaper.
 *
 * Eight columns, written in schema order, with the same tagged objects the
 * reference writes for values JSON cannot carry:
 *   {"$":"date","v":"2026-01-01"}  {"$":"dec","v":"12.34"}
 *   {"$":"ts","v":"2026-10-28T12:00:00+00:00"}  {"$":"bin","v":"AAEC"}
 *
 * The escaper is public and tested against the reference, because untrusted text
 * reaching the wire is the one place where a codec becomes a security problem.
 */
#include "aln.h"

#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct writer {
    char *out;
    size_t cap;
    size_t pos;
    int overflow;
};

static void w_bytes(struct writer *w, const char *text, size_t len) {
    if (w->overflow) {
        return;
    }
    if (w->pos + len > w->cap) {
        w->overflow = 1;
        return;
    }
    memcpy(w->out + w->pos, text, len);
    w->pos += len;
}

static void w_text(struct writer *w, const char *text) { w_bytes(w, text, strlen(text)); }

static void w_format(struct writer *w, const char *format, ...) {
    char scratch[64];
    va_list args;
    va_start(args, format);
    const int written = vsnprintf(scratch, sizeof scratch, format, args);
    va_end(args);
    if (written < 0 || (size_t)written >= sizeof scratch) {
        w->overflow = 1;
        return;
    }
    w_bytes(w, scratch, (size_t)written);
}

size_t aln_json_string(char *out, size_t cap, const char *text, size_t len) {
    /* The reference escapes the quote, the backslash, the five short control
     * escapes, and any other character below 0x20 as \u00XX. UTF-8 passes through
     * unchanged. A hex table is used rather than printf, so there is no format
     * string to get wrong. */
    static const char kHex[] = "0123456789abcdef";
    size_t used = 0;
    const size_t limit = (cap == 0u) ? 0u : cap - 1u;
#define ALN_PUT(character)                                                                  \
    do {                                                                                    \
        if (used >= limit) {                                                                \
            return 0;                                                                       \
        }                                                                                   \
        out[used++] = (character);                                                          \
    } while (0)

    ALN_PUT('"');
    for (size_t index = 0; index < len; ++index) {
        const unsigned char value = (unsigned char)text[index];
        switch (value) {
        case '"':
            ALN_PUT('\\');
            ALN_PUT('"');
            break;
        case '\\':
            ALN_PUT('\\');
            ALN_PUT('\\');
            break;
        case 0x08u:
            ALN_PUT('\\');
            ALN_PUT('b');
            break;
        case 0x09u:
            ALN_PUT('\\');
            ALN_PUT('t');
            break;
        case 0x0Au:
            ALN_PUT('\\');
            ALN_PUT('n');
            break;
        case 0x0Cu:
            ALN_PUT('\\');
            ALN_PUT('f');
            break;
        case 0x0Du:
            ALN_PUT('\\');
            ALN_PUT('r');
            break;
        default:
            if (value < 0x20u) {
                ALN_PUT('\\');
                ALN_PUT('u');
                ALN_PUT('0');
                ALN_PUT('0');
                ALN_PUT(kHex[(value >> 4) & 0x0Fu]);
                ALN_PUT(kHex[value & 0x0Fu]);
            } else {
                ALN_PUT((char)value);
            }
            break;
        }
    }
    ALN_PUT('"');
#undef ALN_PUT
    out[used] = '\0';
    return used;
}

/* Days since 1970-01-01 to a civil date. Howard Hinnant's algorithm. */
static void civil_from_days(int64_t days, int *year, unsigned *month, unsigned *day) {
    const int64_t z = days + 719468;
    const int64_t era = (z >= 0 ? z : z - 146096) / 146097;
    const unsigned doe = (unsigned)(z - era * 146097);
    const unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    const int64_t y = (int64_t)yoe + era * 400;
    const unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    const unsigned mp = (5 * doy + 2) / 153;
    const unsigned d = doy - (153 * mp + 2) / 5 + 1;
    const unsigned m = mp < 10 ? mp + 3 : mp - 9;
    *year = (int)(y + (m <= 2 ? 1 : 0));
    *month = m;
    *day = d;
}

static void w_date(struct writer *w, int32_t days) {
    int year = 0;
    unsigned month = 0;
    unsigned day = 0;
    civil_from_days(days, &year, &month, &day);
    w_text(w, "{\"$\":\"date\",\"v\":\"");
    w_format(w, "%04d-%02u-%02u", year, month, day);
    w_text(w, "\"}");
}

static void w_timestamp(struct writer *w, int64_t micros) {
    const int64_t seconds = micros / 1000000;
    const int64_t rest = micros % 1000000;
    const int64_t days = seconds / 86400;
    int64_t into_day = seconds % 86400;
    if (into_day < 0) {
        into_day += 86400;
    }
    int year = 0;
    unsigned month = 0;
    unsigned day = 0;
    civil_from_days(days, &year, &month, &day);
    const unsigned hour = (unsigned)(into_day / 3600);
    const unsigned minute = (unsigned)((into_day % 3600) / 60);
    const unsigned second = (unsigned)(into_day % 60);
    w_text(w, "{\"$\":\"ts\",\"v\":\"");
    w_format(w, "%04d-%02u-%02uT%02u:%02u:%02u", year, month, day, hour, minute, second);
    if (rest != 0) {
        w_format(w, ".%06lld", (long long)(rest < 0 ? -rest : rest));
    }
    w_text(w, "+00:00\"}");
}

static void w_decimal(struct writer *w, int64_t value) {
    const int64_t scale = 100; /* decimal128(9,2) */
    const int64_t whole = value / scale;
    int64_t fraction = value % scale;
    if (fraction < 0) {
        fraction = -fraction;
    }
    w_text(w, "{\"$\":\"dec\",\"v\":\"");
    if (value < 0 && whole == 0) {
        w_text(w, "-");
    }
    w_format(w, "%lld.%02lld", (long long)whole, (long long)fraction);
    w_text(w, "\"}");
}

enum aln_status aln_json_fixture_encode(const struct aln_fixture *fixture, char *out, size_t cap,
                                        size_t *written) {
    struct writer w = {out, cap, 0, 0};
    char base64[8];
    for (uint64_t row = 0; row < fixture->rows; ++row) {
        if (row > 0) {
            w_text(&w, "\n");
        }
        w_text(&w, "{\"id\":");
        w_format(&w, "%lld", (long long)fixture->id[row]);

        w_text(&w, ",\"name\":");
        if (fixture->name_is_null[row]) {
            w_text(&w, "null");
        } else {
            /* The reference names a row from its index, so there is nothing to store. */
            char name[32];
            snprintf(name, sizeof name, "row-%llu", (unsigned long long)row);
            char escaped[80];
            const size_t length = aln_json_string(escaped, sizeof escaped, name, strlen(name));
            if (length == 0) {
                return ALN_ERR_BUFFER_SMALL;
            }
            w_bytes(&w, escaped, length);
        }

        w_text(&w, ",\"score\":");
        {
            char number[40];
            const size_t length = aln_format_double(number, sizeof number, fixture->score[row]);
            if (length == 0) {
                return ALN_ERR_INVALID;
            }
            w_bytes(&w, number, length);
        }

        w_text(&w, ",\"ok\":");
        w_text(&w, fixture->ok[row] ? "true" : "false");

        w_text(&w, ",\"day\":");
        w_date(&w, fixture->day[row]);

        w_text(&w, ",\"amount\":");
        w_decimal(&w, fixture->amount[row]);

        w_text(&w, ",\"ts\":");
        w_timestamp(&w, fixture->ts_us[row]);

        w_text(&w, ",\"blob\":{\"$\":\"bin\",\"v\":\"");
        if (aln_base64_encode((const unsigned char *)fixture->blob + row * 3u, 3, base64,
                              sizeof base64) == 0) {
            return ALN_ERR_INVALID;
        }
        w_text(&w, base64);
        w_text(&w, "\"}");

        w_text(&w, "}");
        if (w.overflow) {
            return ALN_ERR_OVERSIZE;
        }
    }
    if (written != nullptr) {
        *written = w.pos;
    }
    return ALN_OK;
}
