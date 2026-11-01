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
    if (file == nullptr) {
        return nullptr;
    }
    fseek(file, 0, SEEK_END);
    const long size = ftell(file);
    fseek(file, 0, SEEK_SET);
    unsigned char *data = (unsigned char *)malloc((size_t)size);
    if (data == nullptr) {
        fclose(file);
        return nullptr;
    }
    *length = fread(data, 1, (size_t)size, file);
    fclose(file);
    return data;
}

static void test_crc_known_vector(void) {
    /* The standard check value for this polynomial. */
    CHECK(aln_crc32((const unsigned char *)"123456789", 9) == 0xCBF43926u);
}

static void test_crc_asm_matches_c(void) {
    if (!aln_crc32_have_asm()) {
        printf("note: no assembly kernel on this target, skipping the equality check\n");
        return;
    }
    unsigned char buffer[4096];
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
    CHECK(expected != nullptr);
    if (expected == nullptr) {
        return;
    }
    struct aln_fixture fixture;
    CHECK(aln_fixture_create(3, &fixture) == ALN_OK);
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
    aln_fixture_free(&fixture);
}

static void test_base64_round_trip(void) {
    unsigned char raw[64];
    for (size_t index = 0; index < sizeof raw; ++index) {
        raw[index] = (uint8_t)(index * 37u);
    }
    for (size_t length = 0; length <= sizeof raw; ++length) {
        char text[128];
        unsigned char back[64];
        CHECK(aln_base64_encode(raw, length, text, sizeof text) == ((length + 2) / 3) * 4);
        if (length == 0) {
            continue;
        }
        CHECK(aln_base64_decode(text, strlen(text), back, sizeof back) == length);
        CHECK(memcmp(raw, back, length) == 0);
    }
    unsigned char scratch[8];
    CHECK(aln_base64_decode("A", 1, scratch, sizeof scratch) == 0);
    CHECK(aln_base64_decode("!!!!", 4, scratch, sizeof scratch) == 0);
    CHECK(aln_base64_decode("QQ=A", 4, scratch, sizeof scratch) == 0);
}

static void test_frame_matches_reference(void) {
    size_t schema_len = 0;
    unsigned char *schema = read_file(ALN_FIXTURE_DIR "/golden3.schema", &schema_len);
    size_t expected_len = 0;
    unsigned char *expected = read_file(ALN_FIXTURE_DIR "/golden3.frame", &expected_len);
    CHECK(schema != nullptr && expected != nullptr);
    if (schema == nullptr || expected == nullptr) {
        return;
    }
    /* The schema file is the base64 text the reference wrote, so decode it back to
     * the raw flatbuffer, then let the frame encoder encode it again. */
    static unsigned char raw[4096];
    const size_t raw_len = aln_base64_decode((const char *)schema, schema_len, raw, sizeof raw);
    CHECK(raw_len > 0);

    struct aln_fixture fixture;
    CHECK(aln_fixture_create(3, &fixture) == ALN_OK);
    char *payload = (char *)malloc(8192);
    size_t payload_len = 0;
    CHECK(aln_json_fixture_encode(&fixture, payload, 8192, &payload_len) == ALN_OK);

    static unsigned char wire[65536];
    size_t wire_len = 0;
    CHECK(aln_frame_encode(ALN_LANE_JSON, 0, 3, 8, payload_len, raw, raw_len,
                           "arrowlane/0.4.0", (const unsigned char *)payload, payload_len, wire,
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
    aln_fixture_free(&fixture);
}

static void test_decode_and_errors(void) {
    size_t expected_len = 0;
    unsigned char *wire = read_file(ALN_FIXTURE_DIR "/golden3.frame", &expected_len);
    CHECK(wire != nullptr);
    if (wire == nullptr) {
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
    test_base64_round_trip();
    test_frame_matches_reference();
    test_decode_and_errors();
    printf("%s: %d checks, %d failures\n", g_failures == 0 ? "ok" : "FAILED", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
