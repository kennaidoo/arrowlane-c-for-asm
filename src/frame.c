/* The wire frame: a 38 byte prefix, a compact JSON header, an opaque payload.
 *
 * The prefix is fixed width, little endian, and carries the payload checksum, so a
 * reader can reject a damaged frame before it parses or allocates anything.
 */
#include "aln.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void put_u16(uint8_t *p, uint16_t value) {
    p[0] = (uint8_t)(value & 0xFFu);
    p[1] = (uint8_t)((value >> 8) & 0xFFu);
}

static void put_u32(uint8_t *p, uint32_t value) {
    p[0] = (uint8_t)(value & 0xFFu);
    p[1] = (uint8_t)((value >> 8) & 0xFFu);
    p[2] = (uint8_t)((value >> 16) & 0xFFu);
    p[3] = (uint8_t)((value >> 24) & 0xFFu);
}

static void put_u64(uint8_t *p, uint64_t value) {
    for (int index = 0; index < 8; ++index) {
        p[index] = (uint8_t)((value >> (8 * index)) & 0xFFu);
    }
}

static uint16_t get_u16(const unsigned char *p) {
    return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}

static uint32_t get_u32(const unsigned char *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) |
           ((uint32_t)p[3] << 24);
}

static uint64_t get_u64(const unsigned char *p) {
    uint64_t value = 0;
    for (int index = 0; index < 8; ++index) {
        value |= ((uint64_t)p[index]) << (8 * index);
    }
    return value;
}

const char *aln_status_text(enum aln_status status) {
    switch (status) {
    case ALN_OK:
        return "ok";
    case ALN_ERR_BAD_MAGIC:
        return "bad magic";
    case ALN_ERR_BAD_VERSION:
        return "unsupported version";
    case ALN_ERR_UNKNOWN_LANE:
        return "unknown lane";
    case ALN_ERR_TRUNCATED:
        return "truncated frame";
    case ALN_ERR_OVERSIZE:
        return "oversize frame";
    case ALN_ERR_CHECKSUM:
        return "checksum mismatch";
    case ALN_ERR_BAD_HEADER:
        return "bad header";
    case ALN_ERR_INVALID:
        return "invalid argument";
    case ALN_ERR_NOMEM:
        return "out of memory";
    case ALN_ERR_BUFFER_SMALL:
        return "buffer too small";
    }
    return "unknown status";
}

size_t aln_header_json(char *out, size_t cap, const char *media_type, uint64_t ncols,
                       const char *producer, const char *schema_base64) {
    const int written =
        snprintf(out, cap, "{\"media_type\":\"%s\",\"ncols\":%llu,\"producer\":\"%s\",\"schema\":\"%s\"}",
                 media_type, (unsigned long long)ncols, producer, schema_base64);
    if (written < 0 || (size_t)written >= cap) {
        return 0;
    }
    return (size_t)written;
}

enum aln_status aln_frame_encode_b64(enum aln_lane lane, uint16_t flags, uint64_t rows,
                                     uint64_t ncols,
                                     uint64_t uncompressed_len, const char *schema_base64,
                                     const char *producer, const unsigned char *payload,
                                     size_t payload_len, unsigned char *out, size_t cap,
                                     size_t *written) {
    if (lane != ALN_LANE_JSON) {
        /* This build writes the json lane. The other lanes are read only here. */
        return ALN_ERR_INVALID;
    }
    if (payload_len > ALN_MAX_PAYLOAD_BYTES) {
        return ALN_ERR_OVERSIZE;
    }

    char header[ALN_MAX_HEADER_BYTES + 1u];
    const size_t header_len = aln_header_json(header, sizeof header, "application/x-ndjson",
                                              ncols, producer, schema_base64);
    if (header_len == 0) {
        return ALN_ERR_OVERSIZE;
    }

    const size_t total = ALN_PREFIX_BYTES + header_len + payload_len;
    if (cap < total) {
        return ALN_ERR_BUFFER_SMALL;
    }

    const uint32_t crc =
        aln_crc32_have_asm() ? aln_crc32_asm(payload, payload_len) : aln_crc32(payload, payload_len);

    uint8_t *prefix = out;
    prefix[0] = ALN_MAGIC_0;
    prefix[1] = ALN_MAGIC_1;
    prefix[2] = ALN_MAGIC_2;
    prefix[3] = ALN_MAGIC_3;
    prefix[4] = (uint8_t)ALN_VERSION;
    prefix[5] = lane;
    put_u16(prefix + 6, flags);
    put_u64(prefix + 8, (uint64_t)payload_len);
    put_u64(prefix + 16, uncompressed_len);
    put_u64(prefix + 24, rows);
    put_u32(prefix + 32, crc);
    put_u16(prefix + 36, (uint16_t)header_len);

    memcpy(out + ALN_PREFIX_BYTES, header, header_len);
    memcpy(out + ALN_PREFIX_BYTES + header_len, payload, payload_len);
    if (written != nullptr) {
        *written = total;
    }
    return ALN_OK;
}

enum aln_status aln_frame_encode(enum aln_lane lane, uint16_t flags, uint64_t rows,
                                 uint64_t ncols,
                                 uint64_t uncompressed_len, const unsigned char *schema,
                                 size_t schema_len, const char *producer,
                                 const unsigned char *payload, size_t payload_len, unsigned char *out,
                                 size_t cap, size_t *written) {
    if (schema_len > (ALN_MAX_HEADER_BYTES * 3u) / 4u) {
        return ALN_ERR_OVERSIZE;
    }
    const size_t schema_cap = ((schema_len + 2u) / 3u) * 4u + 1u;
    char *schema_base64 = (char *)malloc(schema_cap);
    if (schema_base64 == nullptr) {
        return ALN_ERR_NOMEM;
    }
    if (aln_base64_encode(schema, schema_len, schema_base64, schema_cap) == 0) {
        free(schema_base64);
        return ALN_ERR_INVALID;
    }
    const enum aln_status status =
        aln_frame_encode_b64(lane, flags, rows, ncols, uncompressed_len, schema_base64, producer,
                             payload, payload_len, out, cap, written);
    free(schema_base64);
    return status;
}

enum aln_status aln_frame_decode(const unsigned char *wire, size_t len, struct aln_frame *out) {
    if (len < ALN_PREFIX_BYTES) {
        return ALN_ERR_TRUNCATED;
    }
    if (wire[0] != ALN_MAGIC_0 || wire[1] != ALN_MAGIC_1 || wire[2] != ALN_MAGIC_2 ||
        wire[3] != ALN_MAGIC_3) {
        return ALN_ERR_BAD_MAGIC;
    }
    if (wire[4] != (uint8_t)ALN_VERSION) {
        return ALN_ERR_BAD_VERSION;
    }
    const uint8_t lane = wire[5];
    if (lane < ALN_LANE_JSON || lane > ALN_LANE_SHM) {
        return ALN_ERR_UNKNOWN_LANE;
    }
    const uint16_t flags = get_u16(wire + 6);
    const uint64_t payload_len = get_u64(wire + 8);
    const uint64_t uncompressed_len = get_u64(wire + 16);
    const uint64_t rows = get_u64(wire + 24);
    const uint32_t crc = get_u32(wire + 32);
    const uint16_t header_len = get_u16(wire + 36);

    /* A uint16 cannot exceed ALN_MAX_HEADER_BYTES, so that limit needs no check. */
    if (payload_len > ALN_MAX_PAYLOAD_BYTES || uncompressed_len > ALN_MAX_UNCOMPRESSED_BYTES) {
        return ALN_ERR_OVERSIZE;
    }
    if ((uint64_t)ALN_PREFIX_BYTES + header_len + payload_len != (uint64_t)len) {
        return ALN_ERR_TRUNCATED;
    }
    const unsigned char *payload = wire + ALN_PREFIX_BYTES + header_len;
    const uint32_t actual =
        aln_crc32_have_asm() ? aln_crc32_asm(payload, (size_t)payload_len)
                             : aln_crc32(payload, (size_t)payload_len);
    if (actual != crc) {
        return ALN_ERR_CHECKSUM;
    }
    if (out != nullptr) {
        out->lane = lane;
        out->flags = flags;
        out->rows = rows;
        out->payload_len = payload_len;
        out->uncompressed_len = uncompressed_len;
        out->crc = crc;
        out->header_len = header_len;
        out->header = wire + ALN_PREFIX_BYTES;
        out->payload = payload;
    }
    return ALN_OK;
}
