/* The tests that matter: the bytes must equal what the Python reference writes.
 *
 * The fixtures were produced by the reference itself, so a pass here is a claim
 * about compatibility, not about self consistency.
 */
#include "aln.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_failures = 0;
static int g_checks = 0;

#define CHECK(condition)                                                                    \
    do {                                                                                    \
        ++g_checks;                                                                         \
        if (!(condition)) {                                                                 \
            ++g_failures;                                                                   \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition);                     \
        }                                                                                   \
    } while (0)

static unsigned char *read_file(const char *path, size_t *length) {
    FILE *file = fopen(path, "rb");
    if (file == NULL) {
        return NULL;
    }
    fseek(file, 0, SEEK_END);
    const long size = ftell(file);
    fseek(file, 0, SEEK_SET);
    unsigned char *data = (unsigned char *)malloc((size_t)size);
    if (data == NULL) {
        fclose(file);
        return NULL;
    }
    *length = fread(data, 1, (size_t)size, file);
    fclose(file);
    return data;
}

static int build_rows3(struct aln_fixture *fixture, char **text_out, char ***names_out) {
    static const char *kNames[3] = {NULL, "row-1", "row-2"};
    fixture->rows = 3;
    fixture->id = (int64_t *)calloc(3, sizeof(int64_t));
    fixture->name_is_null = (int8_t *)calloc(3, sizeof(int8_t));
    fixture->score = (double *)calloc(3, sizeof(double));
    fixture->ok = (int8_t *)calloc(3, sizeof(int8_t));
    fixture->day = (int32_t *)calloc(3, sizeof(int32_t));
    fixture->amount = (int64_t *)calloc(3, sizeof(int64_t));
    fixture->ts_us = (int64_t *)calloc(3, sizeof(int64_t));
    fixture->blob = (int8_t *)calloc(3, 3);
    fixture->names = kNames;
    if (fixture->id == NULL || fixture->blob == NULL) {
        return 0;
    }
    aln_fill_fixture(3, fixture->id, fixture->name_is_null, fixture->score, fixture->ok,
                     fixture->day, fixture->amount, fixture->ts_us, fixture->blob);
    (void)text_out;
    (void)names_out;
    return 1;
}

static void test_crc_known_vector(void) {
    /* The standard check value for this polynomial. */
    CHECK(aln_crc32((const uint8_t *)"123456789", 9) == 0xCBF43926u);
}

static void test_crc_asm_matches_c(void) {
    if (!aln_crc32_have_asm()) {
        printf("note: no assembly kernel on this target, skipping the equality check\n");
        return;
    }
    uint8_t buffer[4096];
    uint32_t state = 12345u;
    for (size_t index = 0; index < sizeof buffer; ++index) {
        state = state * 1103515245u + 12345u;
        buffer[index] = (uint8_t)(state >> 16);
    }
    for (size_t length = 0; length <= sizeof buffer; length += 97) {
        CHECK(aln_crc32(buffer, length) == aln_crc32_asm(buffer, length));
    }
}

static void test_payload_matches_reference(void) {
    size_t expected_len = 0;
    unsigned char *expected = read_file(ALN_FIXTURE_DIR "/golden3.payload", &expected_len);
    CHECK(expected != NULL);
    if (expected == NULL) {
        return;
    }
    struct aln_fixture fixture;
    CHECK(build_rows3(&fixture, NULL, NULL));
    char *payload = (char *)malloc(8192);
    size_t payload_len = 0;
    CHECK(aln_json_fixture_encode(&fixture, payload, 8192, &payload_len) == ALN_OK);
    CHECK(payload_len == 606);
    CHECK(payload_len == expected_len);
    CHECK(memcmp(payload, expected, payload_len) == 0);
    if (payload_len != expected_len || memcmp(payload, expected, payload_len) != 0) {
        printf("--- ours ---\n%.*s\n--- reference ---\n%.*s\n", (int)payload_len, payload,
               (int)expected_len, expected);
    }
    free(payload);
    free(expected);
}

static void test_frame_matches_reference(void) {
    size_t schema_len = 0;
    unsigned char *schema = read_file(ALN_FIXTURE_DIR "/golden3.schema", &schema_len);
    size_t expected_len = 0;
    unsigned char *expected = read_file(ALN_FIXTURE_DIR "/golden3.frame", &expected_len);
    CHECK(schema != NULL && expected != NULL);
    if (schema == NULL || expected == NULL) {
        return;
    }
    /* The schema file is the base64 text the reference wrote, so decode it back to
     * the raw flatbuffer first, then let the frame encoder encode it again. */
    static uint8_t raw[4096];
    size_t raw_len = 0;
    for (size_t index = 0; index + 3 < schema_len; index += 4) {
        const char *quad = (const char *)schema + index;
        int values[4];
        for (int slot = 0; slot < 4; ++slot) {
            const char character = quad[slot];
            if (character >= 'A' && character <= 'Z') {
                values[slot] = character - 'A';
            } else if (character >= 'a' && character <= 'z') {
                values[slot] = character - 'a' + 26;
            } else if (character >= '0' && character <= '9') {
                values[slot] = character - '0' + 52;
            } else if (character == '+') {
                values[slot] = 62;
            } else if (character == '/') {
                values[slot] = 63;
            } else {
                values[slot] = 0;
            }
        }
        raw[raw_len++] = (uint8_t)((values[0] << 2) | (values[1] >> 4));
        raw[raw_len++] = (uint8_t)(((values[1] & 0xF) << 4) | (values[2] >> 2));
        raw[raw_len++] = (uint8_t)(((values[2] & 0x3) << 6) | values[3]);
    }
    /* Undo the padding the encoder added. */
    while (raw_len > 0 && schema_len >= 1) {
        const char last = (char)schema[schema_len - 1];
        if (last == '=') {
            --raw_len;
            --schema_len;
        } else {
            break;
        }
    }

    struct aln_fixture fixture;
    CHECK(build_rows3(&fixture, NULL, NULL));
    char *payload = (char *)malloc(8192);
    size_t payload_len = 0;
    CHECK(aln_json_fixture_encode(&fixture, payload, 8192, &payload_len) == ALN_OK);

    static uint8_t wire[65536];
    size_t wire_len = 0;
    CHECK(aln_frame_encode(ALN_LANE_JSON, 0, 3, 8, payload_len, raw, raw_len,
                           "arrowlane/0.4.0", (const uint8_t *)payload, payload_len, wire,
                           sizeof wire, &wire_len) == ALN_OK);
    CHECK(wire_len == 1372);
    CHECK(wire_len == expected_len);
    CHECK(wire_len == expected_len && memcmp(wire, expected, wire_len) == 0);
    if (wire_len != expected_len || memcmp(wire, expected, wire_len) != 0) {
        printf("frame mismatch: ours %zu bytes, reference %zu bytes\n", wire_len, expected_len);
        for (size_t index = 0; index < wire_len && index < expected_len; ++index) {
            if (wire[index] != expected[index]) {
                printf("first difference at %zu: ours %02x, reference %02x\n", index, wire[index],
                       expected[index]);
                break;
            }
        }
    }
    free(payload);
    free(schema);
    free(expected);
}

static void test_decode_and_errors(void) {
    size_t expected_len = 0;
    unsigned char *wire = read_file(ALN_FIXTURE_DIR "/golden3.frame", &expected_len);
    CHECK(wire != NULL);
    if (wire == NULL) {
        return;
    }
    struct aln_frame frame;
    CHECK(aln_frame_decode(wire, expected_len, &frame) == ALN_OK);
    CHECK(frame.lane == ALN_LANE_JSON);
    CHECK(frame.rows == 3);
    CHECK(frame.payload_len == 606);
    CHECK(frame.crc == 0xCA8AB4F0u);
    CHECK(frame.header_len == 728);

    CHECK(aln_frame_decode(wire, expected_len - 1, &frame) == ALN_ERR_TRUNCATED);

    unsigned char *copy = (unsigned char *)malloc(expected_len);
    memcpy(copy, wire, expected_len);
    copy[0] = 'X';
    CHECK(aln_frame_decode(copy, expected_len, &frame) == ALN_ERR_BAD_MAGIC);
    memcpy(copy, wire, expected_len);
    copy[4] = 9;
    CHECK(aln_frame_decode(copy, expected_len, &frame) == ALN_ERR_BAD_VERSION);
    memcpy(copy, wire, expected_len);
    copy[expected_len - 1] ^= 0xFFu;
    CHECK(aln_frame_decode(copy, expected_len, &frame) == ALN_ERR_CHECKSUM);
    free(copy);
    free(wire);
}

int main(void) {
    test_crc_known_vector();
    test_crc_asm_matches_c();
    test_payload_matches_reference();
    test_frame_matches_reference();
    test_decode_and_errors();
    printf("%s: %d checks, %d failures\n", g_failures == 0 ? "ok" : "FAILED", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
