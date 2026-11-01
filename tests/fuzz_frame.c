/* Random bytes must never crash the frame decoder, and a frame that passes must
 * have a checksum that matches its own payload.
 *
 * Half the rounds start from a valid prefix, so the parser gets past the magic and
 * the version and has to reason about lengths.
 */
#include "aln.h"

#include <stdint.h>
#include <stdio.h>

int main(void) {
    uint32_t state = 0x9E3779B9u;
    unsigned char buffer[512];
    int accepted = 0;
    for (int round = 0; round < 20000; ++round) {
        const size_t length = 38u + (size_t)(state % (sizeof buffer - 38u));
        for (size_t index = 0; index < length; ++index) {
            state = state * 1664525u + 1013904223u;
            buffer[index] = (uint8_t)(state >> 16);
        }
        if ((round % 2) == 0) {
            buffer[0] = 'A';
            buffer[1] = 'L';
            buffer[2] = 'N';
            buffer[3] = '1';
            buffer[4] = 1;
            buffer[5] = 1;
        }
        struct aln_frame frame;
        if (aln_frame_decode(buffer, length, &frame) == ALN_OK) {
            ++accepted;
            const uint32_t crc = aln_crc32(frame.payload, (size_t)frame.payload_len);
            if (crc != frame.crc) {
                printf("FAIL: a frame passed with a checksum that does not match\n");
                return 1;
            }
        }
    }
    printf("ok: 20000 frames, %d accepted, none crashed\n", accepted);
    return 0;
}
