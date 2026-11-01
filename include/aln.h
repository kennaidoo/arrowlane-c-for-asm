/* arrowlane in C23, with a Fortran 2023 slice and an assembly kernel.
 *
 * The frame is the same wire format the Python reference writes: a 38 byte
 * prefix, a compact JSON header, and an opaque payload.
 *
 * This header assumes C23 throughout. The features are used because they earn
 * their place, not for decoration:
 *
 *   [[nodiscard]]        every status returning call must be checked
 *   constexpr            the wire constants are constant expressions, not macros
 *   nullptr              one null pointer value, of its own type
 *   enum : unsigned char fixed underlying type, so the ABI cannot drift
 *   digit separators     the CRC polynomial reads as 0xEDB8'8320
 *   bool                 a keyword, and the type the Fortran c_bool maps to
 */
#ifndef ALN_H
#define ALN_H

#include <stddef.h>
#include <stdint.h>

#define ALN_NODISCARD [[nodiscard]]

constexpr char ALN_MAGIC_0 = 'A';
constexpr char ALN_MAGIC_1 = 'L';
constexpr char ALN_MAGIC_2 = 'N';
constexpr char ALN_MAGIC_3 = '1';
constexpr uint32_t ALN_VERSION = 1u;
constexpr size_t ALN_PREFIX_BYTES = 38u;
constexpr size_t ALN_MAX_HEADER_BYTES = 65'535u;
constexpr size_t ALN_MAX_PAYLOAD_BYTES = 536'870'912u;
constexpr size_t ALN_MAX_UNCOMPRESSED_BYTES = 1'073'741'824u;
constexpr uint32_t ALN_CRC32_POLYNOMIAL = 0xEDB8'8320u;

static_assert(ALN_PREFIX_BYTES == 38u, "the wire prefix is 38 bytes by definition");
static_assert(sizeof(uint64_t) == 8u, "the wire format assumes 8 byte integers");

enum aln_lane : unsigned char {
    ALN_LANE_JSON = 1,
    ALN_LANE_IPC = 2,
    ALN_LANE_IPC_ZSTD = 3,
    ALN_LANE_SHM = 4
};

enum aln_status : unsigned char {
    ALN_OK = 0,
    ALN_ERR_BAD_MAGIC,
    ALN_ERR_BAD_VERSION,
    ALN_ERR_UNKNOWN_LANE,
    ALN_ERR_TRUNCATED,
    ALN_ERR_OVERSIZE,
    ALN_ERR_CHECKSUM,
    ALN_ERR_BAD_HEADER,
    ALN_ERR_INVALID,
    ALN_ERR_NOMEM,
    ALN_ERR_BUFFER_SMALL
};

/* A decoded frame. `header` and `payload` point into the caller's buffer. */
struct aln_frame {
    unsigned char lane;
    uint16_t flags;
    uint64_t rows;
    uint64_t payload_len;
    uint64_t uncompressed_len;
    uint32_t crc;
    uint16_t header_len;
    const unsigned char *header;
    const unsigned char *payload;
};

/* The fixture the benchmarks use, filled by the Fortran slice. The two flag
 * columns are bool because that is what a Fortran logical(c_bool) is. */
struct aln_fixture {
    uint64_t rows;
    int64_t *id;
    bool *name_is_null;
    double *score;
    bool *ok;
    int32_t *day;
    int64_t *amount; /* decimal128(9,2), carried as an int64 */
    int64_t *ts_us;  /* microseconds since the epoch, UTC */
    int8_t *blob;    /* three bytes per row */
};

ALN_NODISCARD const char *aln_status_text(enum aln_status status);

/* The checksum the wire format uses: CRC-32, the polynomial zlib calls crc32. */
ALN_NODISCARD uint32_t aln_crc32(const unsigned char *data, size_t len);
/* The same checksum from the assembly kernel. Falls back to C when absent. */
ALN_NODISCARD uint32_t aln_crc32_asm(const unsigned char *data, size_t len);
ALN_NODISCARD bool aln_crc32_have_asm(void);

/* Shortest decimal that reads back as the same double, as the reference writes it.
 * Returns the length written, or 0 when the value has no form in this lane. */
ALN_NODISCARD size_t aln_format_double(char *out, size_t cap, double value);

/* Base64, standard alphabet with padding, as the reference writes it. */
ALN_NODISCARD size_t aln_base64_encode(const unsigned char *input, size_t len, char *out,
                                       size_t cap);

/* Writes a JSON string literal, quotes included, escaping exactly what the
 * reference escapes: quote, backslash, the five short control escapes, and any
 * other character below 0x20 as \u00XX. UTF-8 passes through unchanged.
 *
 * The length is explicit because untrusted text may contain a NUL, and a codec that
 * stops at the first NUL silently truncates the record. Returns the length written,
 * or 0 when the buffer is too small. */
ALN_NODISCARD size_t aln_json_string(char *out, size_t cap, const char *text, size_t len);

/* Decodes standard base64 with padding. Returns the byte count, or 0 on bad input. */
ALN_NODISCARD size_t aln_base64_decode(const char *text, size_t len, unsigned char *out,
                                       size_t cap);

/* The header JSON, byte for byte what the reference writes: sorted keys, no
 * spaces. `media_type` and `producer` are plain ASCII. */
ALN_NODISCARD size_t aln_header_json(char *out, size_t cap, const char *media_type,
                                     uint64_t ncols, const char *producer,
                                     const char *schema_base64);

/* Encodes prefix, header and payload into `out`. `schema` is the raw Arrow schema
 * flatbuffer, base64 encoded into the header by this function. */
ALN_NODISCARD enum aln_status aln_frame_encode(
    enum aln_lane lane, uint16_t flags, uint64_t rows, uint64_t ncols, uint64_t uncompressed_len,
    const unsigned char *schema, size_t schema_len, const char *producer,
    const unsigned char *payload, size_t payload_len, unsigned char *out, size_t cap,
    size_t *written);

/* The same, for a caller that already holds the schema as base64 text. The bench
 * uses this so it measures the shipped encoder instead of a second copy of it. */
ALN_NODISCARD enum aln_status aln_frame_encode_b64(
    enum aln_lane lane, uint16_t flags, uint64_t rows, uint64_t ncols, uint64_t uncompressed_len,
    const char *schema_base64, const char *producer, const unsigned char *payload,
    size_t payload_len, unsigned char *out, size_t cap, size_t *written);

/* Validates the prefix and the checksum, and fills `out`. */
ALN_NODISCARD enum aln_status aln_frame_decode(const unsigned char *wire, size_t len,
                                               struct aln_frame *out);

/* The NDJSON payload for the fixture, byte for byte what the reference writes. */
ALN_NODISCARD enum aln_status aln_json_fixture_encode(const struct aln_fixture *fixture,
                                                      char *out, size_t cap, size_t *written);

/* Allocates the fixture arrays and fills them from the Fortran slice. */
ALN_NODISCARD enum aln_status aln_fixture_create(uint64_t rows, struct aln_fixture *out);
void aln_fixture_free(struct aln_fixture *fixture);

/* Fortran 2023 slice. */
void aln_fill_fixture(int64_t rows, int64_t *id, bool *name_is_null, double *score, bool *ok,
                      int32_t *day, int64_t *amount, int64_t *ts_us, int8_t *blob);
ALN_NODISCARD double aln_median(const double *values, int64_t count);

#endif /* ALN_H */
