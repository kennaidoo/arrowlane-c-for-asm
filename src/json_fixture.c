/* The NDJSON writer for the benchmark fixture.
 *
 * Eight columns, written in schema order, with the same tagged objects the
 * reference writes for values JSON cannot carry:
 *   {"$":"date","v":"2026-01-01"}  {"$":"dec","v":"12.34"}
 *   {"$":"ts","v":"2026-10-28T12:00:00+00:00"}  {"$":"bin","v":"AAEC"}
 *
 * Floats are printed at the shortest precision that reads back as the same double,
 * which is what the reference's encoder emits. Integral values keep one decimal,
 * so 0 prints as 0.0 and not as 0.
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

static void w_json_string(struct writer *w, const char *text) {
    w_text(w, "\"");
    for (const unsigned char *cursor = (const unsigned char *)text; *cursor != 0; ++cursor) {
        const unsigned char value = *cursor;
        if (value == '"' || value == '\\') {
            const char pair[2] = {'\\', (char)value};
            w_bytes(w, pair, 2);
        } else if (value < 0x20) {
            w_format(w, "\\u%04x", (unsigned)value);
        } else {
            w_bytes(w, (const char *)cursor, 1);
        }
    }
    w_text(w, "\"");
}

/* Shortest decimal that reads back as the same double. */
static void w_double(struct writer *w, double value) {
    if (value == 0.0) {
        w_text(w, signbit(value) ? "-0.0" : "0.0");
        return;
    }
    if (value == (double)(int64_t)value && fabs(value) < 1e16) {
        w_format(w, "%.1f", value);
        return;
    }
    char scratch[40];
    for (int precision = 1; precision <= 17; ++precision) {
        snprintf(scratch, sizeof scratch, "%.*g", precision, value);
        if (strtod(scratch, NULL) == value) {
            w_text(w, scratch);
            return;
        }
    }
    w_format(w, "%.17g", value);
}

/* Days since 1970-01-01 to a civil date. Howard Hinnant's algorithm. */
static void civil_from_days(int64_t days, int *year, unsigned *month, unsigned *day) {
    int64_t z = days + 719468;
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
            w_json_string(&w, fixture->names[row]);
        }

        w_text(&w, ",\"score\":");
        w_double(&w, fixture->score[row]);

        w_text(&w, ",\"ok\":");
        w_text(&w, fixture->ok[row] ? "true" : "false");

        w_text(&w, ",\"day\":");
        w_date(&w, fixture->day[row]);

        w_text(&w, ",\"amount\":");
        w_decimal(&w, fixture->amount[row]);

        w_text(&w, ",\"ts\":");
        w_timestamp(&w, fixture->ts_us[row]);

        w_text(&w, ",\"blob\":{\"$\":\"bin\",\"v\":\"");
        if (aln_base64_encode((const uint8_t *)fixture->blob + row * 3u, 3, base64,
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
    if (written != NULL) {
        *written = w.pos;
    }
    return ALN_OK;
}
