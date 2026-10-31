/* The checksum kernel, hand written for x86-64 System V.
 *
 * It runs the same slicing by eight algorithm as crc32.c over the same tables,
 * so the two must agree on every input, and the test checks that on random data.
 * On any other target this file is not compiled and the C path is used.
 */
    .intel_syntax noprefix
    .text
    .globl aln_crc32_asm
    .type aln_crc32_asm, @function
/* uint32_t aln_crc32_asm(const uint8_t *data, size_t len)
 *   rdi = data, rsi = len, eax = result
 */
aln_crc32_asm:
    mov     eax, -1
    lea     r8, [rip + aln_crc32_tables]
    cmp     rsi, 8
    jb      .Ltail

.Lloop8:
    mov     ecx, dword ptr [rdi]
    xor     eax, ecx
    movzx   ecx, al
    mov     edx, dword ptr [r8 + rcx*4 + 7168]
    movzx   ecx, ah
    xor     edx, dword ptr [r8 + rcx*4 + 6144]
    mov     ecx, eax
    shr     ecx, 16
    movzx   ecx, cl
    xor     edx, dword ptr [r8 + rcx*4 + 5120]
    mov     ecx, eax
    shr     ecx, 24
    xor     edx, dword ptr [r8 + rcx*4 + 4096]
    movzx   ecx, byte ptr [rdi + 4]
    xor     edx, dword ptr [r8 + rcx*4 + 3072]
    movzx   ecx, byte ptr [rdi + 5]
    xor     edx, dword ptr [r8 + rcx*4 + 2048]
    movzx   ecx, byte ptr [rdi + 6]
    xor     edx, dword ptr [r8 + rcx*4 + 1024]
    movzx   ecx, byte ptr [rdi + 7]
    xor     edx, dword ptr [r8 + rcx*4]
    mov     eax, edx
    add     rdi, 8
    sub     rsi, 8
    cmp     rsi, 8
    jae     .Lloop8

.Ltail:
    test    rsi, rsi
    je      .Ldone

.Lbyte:
    movzx   ecx, byte ptr [rdi]
    xor     ecx, eax
    movzx   ecx, cl
    shr     eax, 8
    xor     eax, dword ptr [r8 + rcx*4]
    inc     rdi
    dec     rsi
    jne     .Lbyte

.Ldone:
    not     eax
    ret
    .size aln_crc32_asm, .-aln_crc32_asm

    .section .note.GNU-stack,"",@progbits
