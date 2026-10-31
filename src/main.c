/* The command line: bench, describe, selftest.
 *
 * `bench` prints one line per lane in the same shape the other arrowlane
 * implementations print, so one parser reads all four:
 *   <lane> <wire_bytes> <encode_ms> <decode_ms>
 */
#include "aln.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define ALN_VERSION_TEXT "0.1.0"

static double now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec * 1000.0 + (double)ts.tv_nsec / 1e6;
}

static int cmd_describe(void) {
    printf("{\"kind\":\"arrowlane\",\"name\":\"arrowlane-c-for-asm\",\"version\":\"%s\","
           "\"frame\":{\"magic\":\"ALN1\",\"version\":1,\"prefix_bytes\":38,"
           "\"max_header_bytes\":65535,\"max_payload_bytes\":536870912},"
           "\"lanes\":[\"json\"],"
           "\"capabilities\":{\"schema\":\"caller supplied\",\"json\":\"fixture\","
           "\"ipc\":\"absent\",\"ipc_zstd\":\"absent\",\"shm\":\"absent\","
           "\"crc32_asm\":%s,\"bench\":true},"
           "\"slices\":{\"c\":\"frame, header, json writer, base64\","
           "\"fortran\":\"fixture arrays, median\","
           "\"asm\":\"crc32 slicing by eight\"}}\n",
           ALN_VERSION_TEXT, aln_crc32_have_asm() ? "true" : "false");
    return 0;
}

static int build_fixture(uint64_t rows, struct aln_fixture *fixture, char **names_storage,
                         char ***names, void **blocks) {
    fixture->rows = rows;
    fixture->id = (int64_t *)calloc(rows, sizeof(int64_t));
    fixture->name_is_null = (int8_t *)calloc(rows, sizeof(int8_t));
    fixture->score = (double *)calloc(rows, sizeof(double));
    fixture->ok = (int8_t *)calloc(rows, sizeof(int8_t));
    fixture->day = (int32_t *)calloc(rows, sizeof(int32_t));
    fixture->amount = (int64_t *)calloc(rows, sizeof(int64_t));
    fixture->ts_us = (int64_t *)calloc(rows, sizeof(int64_t));
    fixture->blob = (int8_t *)calloc(rows, 3);
    char **names_array = (char **)calloc(rows, sizeof(char *));
    char *text = (char *)calloc(rows, 32);
    if (fixture->id == NULL || fixture->name_is_null == NULL || fixture->score == NULL ||
        fixture->ok == NULL || fixture->day == NULL || fixture->amount == NULL ||
        fixture->ts_us == NULL || fixture->blob == NULL || names_array == NULL || text == NULL) {
        return 0;
    }
    for (uint64_t row = 0; row < rows; ++row) {
        names_array[row] = text + row * 32;
    }
    fixture->names = (const char *const *)names_array;

    /* Fortran owns the numbers. */
    aln_fill_fixture((int64_t)rows, fixture->id, fixture->name_is_null, fixture->score, fixture->ok,
                     fixture->day, fixture->amount, fixture->ts_us, fixture->blob);
    /* Names are text, so C builds them from the same rule. */
    for (uint64_t row = 1; row < rows; ++row) {
        snprintf(names_array[row], 32, "row-%llu", (unsigned long long)row);
    }
    *names_storage = text;
    *names = names_array;
    *blocks = fixture;
    return 1;
}

static void free_fixture(struct aln_fixture *fixture, char *text, char **names) {
    free(fixture->id);
    free(fixture->name_is_null);
    free(fixture->score);
    free(fixture->ok);
    free(fixture->day);
    free(fixture->amount);
    free(fixture->ts_us);
    free(fixture->blob);
    free(text);
    free(names);
}

static int cmd_bench(uint64_t rows, int repeats) {
    struct aln_fixture fixture;
    char *text = NULL;
    char **names = NULL;
    void *blocks = NULL;
    if (!build_fixture(rows, &fixture, &text, &names, &blocks)) {
        fprintf(stderr, "error: out of memory\n");
        return 1;
    }

    /* The schema blob is supplied by the caller in this build, so the bench uses
     * the one the reference wrote for the same table. */
    FILE *schema_file = fopen(ALN_SCHEMA_PATH, "rb");
    if (schema_file == NULL) {
        fprintf(stderr, "error: cannot open %s\n", ALN_SCHEMA_PATH);
        free_fixture(&fixture, text, names);
        return 1;
    }
    static char schema_b64[4096];
    const size_t schema_len = fread(schema_b64, 1, sizeof schema_b64 - 1, schema_file);
    fclose(schema_file);
    schema_b64[schema_len] = '\0';

    const size_t payload_cap = (size_t)rows * 256u + 4096u;
    char *payload = (char *)malloc(payload_cap);
    uint8_t *wire = (uint8_t *)malloc(payload_cap + 65536u);
    double *encode_samples = (double *)calloc((size_t)repeats, sizeof(double));
    double *decode_samples = (double *)calloc((size_t)repeats, sizeof(double));
    if (payload == NULL || wire == NULL || encode_samples == NULL || decode_samples == NULL) {
        fprintf(stderr, "error: out of memory\n");
        free_fixture(&fixture, text, names);
        return 1;
    }

    size_t payload_len = 0;
    size_t wire_len = 0;
    for (int repeat = 0; repeat < repeats; ++repeat) {
        const double start = now_ms();
        if (aln_json_fixture_encode(&fixture, payload, payload_cap, &payload_len) != ALN_OK) {
            fprintf(stderr, "error: payload buffer too small\n");
            return 1;
        }
        /* The bench takes the schema as already base64 text, so wrap it directly. */
        const size_t header_len = aln_header_json((char *)wire + ALN_PREFIX_BYTES, 65536,
                                                  "application/x-ndjson", 8, "arrowlane/" ALN_VERSION_TEXT,
                                                  schema_b64);
        if (header_len == 0) {
            fprintf(stderr, "error: header too large\n");
            return 1;
        }
        const uint32_t crc = aln_crc32_have_asm() ? aln_crc32_asm((const uint8_t *)payload, payload_len)
                                                  : aln_crc32((const uint8_t *)payload, payload_len);
        wire[0] = 'A';
        wire[1] = 'L';
        wire[2] = 'N';
        wire[3] = '1';
        wire[4] = 1;
        wire[5] = ALN_LANE_JSON;
        wire[6] = 0;
        wire[7] = 0;
        for (int index = 0; index < 8; ++index) {
            wire[8 + index] = (uint8_t)((payload_len >> (8 * index)) & 0xFFu);
            wire[16 + index] = (uint8_t)((payload_len >> (8 * index)) & 0xFFu);
            wire[24 + index] = (uint8_t)((rows >> (8 * index)) & 0xFFu);
        }
        wire[32] = (uint8_t)(crc & 0xFFu);
        wire[33] = (uint8_t)((crc >> 8) & 0xFFu);
        wire[34] = (uint8_t)((crc >> 16) & 0xFFu);
        wire[35] = (uint8_t)((crc >> 24) & 0xFFu);
        wire[36] = (uint8_t)(header_len & 0xFFu);
        wire[37] = (uint8_t)((header_len >> 8) & 0xFFu);
        memcpy(wire + ALN_PREFIX_BYTES + header_len, payload, payload_len);
        wire_len = ALN_PREFIX_BYTES + header_len + payload_len;
        const double encoded = now_ms();

        struct aln_frame frame;
        if (aln_frame_decode(wire, wire_len, &frame) != ALN_OK) {
            fprintf(stderr, "error: decode failed\n");
            return 1;
        }
        const double decoded = now_ms();
        encode_samples[repeat] = encoded - start;
        decode_samples[repeat] = decoded - encoded;
    }

    printf("%-10s %12llu %12.3f %12.3f\n", "json", (unsigned long long)wire_len,
           aln_median(encode_samples, repeats), aln_median(decode_samples, repeats));
    free(payload);
    free(wire);
    free(encode_samples);
    free(decode_samples);
    free_fixture(&fixture, text, names);
    return 0;
}

int main(int argc, char **argv) {
    if (argc < 2) {
        printf("usage: arrowlane-c-for-asm <bench|describe> [--rows N] [--repeats N]\n");
        return 2;
    }
    if (strcmp(argv[1], "describe") == 0) {
        return cmd_describe();
    }
    if (strcmp(argv[1], "bench") == 0) {
        uint64_t rows = 200000;
        int repeats = 5;
        for (int index = 2; index + 1 < argc; index += 2) {
            if (strcmp(argv[index], "--rows") == 0) {
                rows = (uint64_t)strtoull(argv[index + 1], NULL, 10);
            } else if (strcmp(argv[index], "--repeats") == 0) {
                repeats = atoi(argv[index + 1]);
            }
        }
        if (repeats < 1) {
            repeats = 1;
        }
        return cmd_bench(rows, repeats);
    }
    fprintf(stderr, "error: unknown command %s\n", argv[1]);
    return 2;
}
