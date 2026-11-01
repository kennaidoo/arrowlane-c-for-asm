/* The command line: bench and describe.
 *
 * `bench` prints one line per lane in the same shape the other arrowlane
 * implementations print, so one parser reads all four:
 *   <lane> <wire_bytes> <encode_ms> <validate_ms>
 *
 * The last column is frame validation: the prefix and the checksum are verified and
 * a view of the payload comes back. It is not a decode into columns, and the README
 * says so.
 */
#include "aln.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define ALN_VERSION_TEXT "0.1.0"
#define ALN_SCHEMA_CAP 8192

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

static int read_schema(char *buffer, size_t cap, size_t *length) {
    FILE *file = fopen(ALN_SCHEMA_PATH, "rb");
    if (file == nullptr) {
        return 0;
    }
    *length = fread(buffer, 1, cap - 1u, file);
    fclose(file);
    buffer[*length] = '\0';
    return 1;
}

static int cmd_bench(uint64_t rows, int repeats, int as_json) {
    struct aln_fixture fixture;
    if (aln_fixture_create(rows, &fixture) != ALN_OK) {
        fprintf(stderr, "error: %s\n", aln_status_text(ALN_ERR_NOMEM));
        return 1;
    }

    /* This build does not synthesise the Arrow schema flatbuffer, so the bench uses
     * the base64 the reference wrote for the same table. */
    static char schema_b64[ALN_SCHEMA_CAP];
    size_t schema_len = 0;
    if (!read_schema(schema_b64, sizeof schema_b64, &schema_len)) {
        fprintf(stderr, "error: cannot open %s\n", ALN_SCHEMA_PATH);
        aln_fixture_free(&fixture);
        return 1;
    }

    const size_t payload_cap = (size_t)rows * 256u + 4096u;
    char *payload = (char *)malloc(payload_cap);
    unsigned char *wire = (uint8_t *)malloc(payload_cap + 65536u);
    double *encode_samples = (double *)calloc((size_t)repeats, sizeof(double));
    double *validate_samples = (double *)calloc((size_t)repeats, sizeof(double));
    if (payload == nullptr || wire == nullptr || encode_samples == nullptr || validate_samples == nullptr) {
        fprintf(stderr, "error: %s\n", aln_status_text(ALN_ERR_NOMEM));
        free(payload);
        free(wire);
        free(encode_samples);
        free(validate_samples);
        aln_fixture_free(&fixture);
        return 1;
    }

    size_t wire_len = 0;
    for (int repeat = 0; repeat < repeats; ++repeat) {
        const double start = now_ms();
        size_t payload_len = 0;
        enum aln_status status =
            aln_json_fixture_encode(&fixture, payload, payload_cap, &payload_len);
        if (status != ALN_OK) {
            fprintf(stderr, "error: %s\n", aln_status_text(status));
            return 1;
        }
        /* The shipped encoder, not a copy of it. */
        status = aln_frame_encode_b64(ALN_LANE_JSON, 0, rows, 8, payload_len, schema_b64,
                                      "arrowlane/" ALN_VERSION_TEXT, (const unsigned char *)payload,
                                      payload_len, wire, payload_cap + 65536u, &wire_len);
        if (status != ALN_OK) {
            fprintf(stderr, "error: %s\n", aln_status_text(status));
            return 1;
        }
        const double encoded = now_ms();

        struct aln_frame frame;
        status = aln_frame_decode(wire, wire_len, &frame);
        if (status != ALN_OK) {
            fprintf(stderr, "error: %s\n", aln_status_text(status));
            return 1;
        }
        const double validated = now_ms();
        encode_samples[repeat] = encoded - start;
        validate_samples[repeat] = validated - encoded;
    }
    const double encode_ms = aln_median(encode_samples, repeats);
    const double validate_ms = aln_median(validate_samples, repeats);

    if (as_json) {
        printf("{\"lane\":\"json\",\"rows\":%llu,\"wire_bytes\":%llu,\"encode_ms\":%.3f,"
               "\"validate_ms\":%.3f,\"crc32_asm\":%s}\n",
               (unsigned long long)rows, (unsigned long long)wire_len, encode_ms, validate_ms,
               aln_crc32_have_asm() ? "true" : "false");
    } else {
        printf("%-10s %12llu %12.3f %12.3f\n", "json", (unsigned long long)wire_len, encode_ms,
               validate_ms);
    }
    free(payload);
    free(wire);
    free(encode_samples);
    free(validate_samples);
    aln_fixture_free(&fixture);
    return 0;
}

int main(int argc, char **argv) {
    if (argc < 2) {
        printf("usage: arrowlane-c-for-asm <bench|describe> [--rows N] [--repeats N] [--json]\n");
        return 2;
    }
    if (strcmp(argv[1], "describe") == 0) {
        return cmd_describe();
    }
    if (strcmp(argv[1], "bench") == 0) {
        uint64_t rows = 200000;
        int repeats = 5;
        int as_json = 0;
        for (int index = 2; index < argc; ++index) {
            if (strcmp(argv[index], "--rows") == 0 && index + 1 < argc) {
                rows = (uint64_t)strtoull(argv[++index], nullptr, 10);
            } else if (strcmp(argv[index], "--repeats") == 0 && index + 1 < argc) {
                repeats = atoi(argv[++index]);
            } else if (strcmp(argv[index], "--json") == 0) {
                as_json = 1;
            }
        }
        if (repeats < 1) {
            repeats = 1;
        }
        return cmd_bench(rows, repeats, as_json);
    }
    fprintf(stderr, "error: unknown command %s\n", argv[1]);
    return 2;
}
