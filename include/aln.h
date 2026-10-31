/* arrowlane in C, with a Fortran slice and an assembly kernel.
 *
 * The frame is the same wire format the Python reference writes: a 38 byte
 * prefix, a compact JSON header, and an opaque payload. This header is the whole
 * public surface. Everything is plain C99 so the assembly and Fortran slices can
 * sit behind it.
 */
#ifndef ALN_H
#define ALN_H

#include <stddef.h>
#include <stdint.h>

#define ALN_MAGIC_0 'A'
#define ALN_MAGIC_1 'L'
#define ALN_MAGIC_2 'N'
#define ALN_MAGIC_3 '1'
#define ALN_VERSION 1u
#define ALN_PREFIX_BYTES 38u
#define ALN_MAX_HEADER_BYTES 65535u
#define ALN_MAX_PAYLOAD_BYTES 536870912u
#define ALN_MAX_UNCOMPRESSED_BYTES 1073741824u

enum aln_lane {
    ALN_LANE_JSON = 1,
    ALN_LANE_IPC = 2,
    ALN_LANE_IPC_ZSTD = 3,
    ALN_LANE_SHM = 4
};

enum aln_status {
    ALN_OK = 0,
    ALN_ERR_BAD_MAGIC,
    ALN_ERR_BAD_VERSION,
    ALN_ERR_UNKNOWN_LANE,
    ALN_ERR_TRUNCATED,
    ALN_ERR_OVERSIZE,
    ALN_ERR_CHECKSUM,
    ALN_ERR_BAD_HEADER,
    ALN_ERR_INVALID,
    ALN_ERR_NOMEM
};

/* A decoded frame. `header` and `payload` point into the caller's buffer. */
struct aln_frame {
    uint8_t lane;
    uint16_t flags;
    uint64_t rows;
    uint64_t payload_len;
    uint64_t uncompressed_len;
    uint32_t crc;
    uint16_t header_len;
    const uint8_t *header;
    const uint8_t *payload;
};

/* The fixture the benchmarks use, filled by the Fortran slice. */
struct aln_fixture {
    uint64_t rows;
    int64_t *id;
    int8_t *name_is_null;
    const char *const *names;
    double *score;
    int8_t *ok;
    int32_t *day;
    int64_t *amount; /* decimal128(9,2), carried as an int64 */
    int64_t *ts_us;  /* microseconds since the epoch, UTC */
    int8_t *blob;    /* three bytes per row */
};

const char *aln_status_text(enum aln_status status);

/* The checksum the wire format uses: CRC-32, the polynomial zlib calls crc32. */
uint32_t aln_crc32(const uint8_t *data, size_t len);
/* The same checksum from the assembly kernel. Falls back to C when absent. */
uint32_t aln_crc32_asm(const uint8_t *data, size_t len);
int aln_crc32_have_asm(void);

/* Base64, standard alphabet with padding, as the reference writes it. */
size_t aln_base64_encode(const uint8_t *input, size_t len, char *out, size_t cap);

/* The header JSON, byte for byte what the reference writes: sorted keys, no
 * spaces. `media_type` and `producer` are plain ASCII. */
size_t aln_header_json(char *out, size_t cap, const char *media_type, uint64_t ncols,
                       const char *producer, const char *schema_base64);

/* Encodes prefix, header and payload into `out`. `schema` is the raw Arrow schema
 * flatbuffer, base64 encoded into the header by this function. */
enum aln_status aln_frame_encode(uint8_t lane, uint16_t flags, uint64_t rows, uint64_t ncols,
                                 uint64_t uncompressed_len, const uint8_t *schema,
                                 size_t schema_len, const char *producer,
                                 const uint8_t *payload, size_t payload_len, uint8_t *out,
                                 size_t cap, size_t *written);

/* Validates the prefix and the checksum, and fills `out`. */
enum aln_status aln_frame_decode(const uint8_t *wire, size_t len, struct aln_frame *out);

/* The NDJSON payload for the fixture, byte for byte what the reference writes. */
enum aln_status aln_json_fixture_encode(const struct aln_fixture *fixture, char *out,
                                        size_t cap, size_t *written);

/* Fortran slice. */
void aln_fill_fixture(int64_t rows, int64_t *id, int8_t *name_is_null, double *score,
                      int8_t *ok, int32_t *day, int64_t *amount, int64_t *ts_us,
                      int8_t *blob);
double aln_median(const double *values, int64_t count);

#endif /* ALN_H */
