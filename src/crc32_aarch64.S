/* The checksum kernel for aarch64, using the CRC extension.
 *
 * ARMv8 has two CRC families. crc32b/h/w/x compute CRC-32 with the IEEE
 * polynomial, which is the one zlib calls crc32 and the one this wire format uses.
 * crc32cb/ch/cw/cx compute CRC-32C with the Castagnoli polynomial, which is a
 * different checksum and the wrong one here.
 *
 * The hardware does in one instruction what the portable C kernel does with eight
 * lookup tables, so this is the case where assembly is worth reading.
 */
    .arch armv8-a+crc
    .text
    .globl aln_crc32_asm
    .type aln_crc32_asm, %function
/* uint32_t aln_crc32_asm(const unsigned char *data, size_t len)
 *   x0 = data, x1 = len, returns in w0
 */
aln_crc32_asm:
    mvn     w2, wzr                 // crc = 0xFFFFFFFF
1:
    cmp     x1, #8
    b.lo    2f
    ldr     x3, [x0], #8
    crc32x  w2, w2, x3              // eight bytes at a time
    sub     x1, x1, #8
    b       1b
2:
    cbz     x1, 3f
    ldrb    w3, [x0], #1
    crc32b  w2, w2, w3              // the tail, one byte at a time
    sub     x1, x1, #1
    b       2b
3:
    mvn     w0, w2                  // final xor
    ret
    .size aln_crc32_asm, .-aln_crc32_asm

    .section .note.GNU-stack,"",%progbits
