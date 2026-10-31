# Performance and compatibility, measured

All numbers from one run on one machine, Release build, 200,000 rows, five
repeats, median. The reference numbers come from the Python implementation measured
the same way on the same machine.

| Implementation | Wire bytes | Encode ms | Decode ms |
|---|---|---|---|
| Python reference | 42,505,756 | 2,131.7 | 2,081.9 |
| C++ port | 42,505,784 | 244.7 | 827.4 |
| this project, C with Fortran and asm | 42,505,756 | 885.6 | 37.9 |

## What the encode number means

The encode column is a fair comparison: every implementation produces the whole
NDJSON payload from the same table, and the byte counts agree.

This project is 2.4 times faster than the Python reference on encode, and 3.6 times
slower than the C++ port. The reason is one function. The float formatter here finds
the shortest decimal that reads back as the same double by trying precisions one to
seventeen with `printf` and reading each back with `strtod`. That is correct and it
is slow, and at 200,000 float values it dominates the encode. A Ryu style formatter
is the next lever, and it is a known piece of work rather than a mystery.

## What the decode number means, and why it is not a comparison

The decode column here is **not** comparable to the other two, and the honest reading
is that it measures less work.

- The Python reference parses every NDJSON line, checks it against the header schema,
  and builds a pyarrow table.
- The C++ port decodes the Arrow IPC body into a table.
- This project validates the 38 byte prefix, verifies the CRC-32 over the payload,
  and returns a pointer to the payload. It does not parse NDJSON back into columns.

So 37.9 ms is the cost of checking a frame, not the cost of rebuilding a table. The
number is useful, and it is not a win. The README says the same.

## Byte identity

| Fixture | Bytes | Result |
|---|---|---|
| 3 rows, NDJSON payload | 606 | identical to the reference, byte for byte |
| 3 rows, whole frame | 1,372 | identical to the reference, byte for byte |
| 200,000 rows, whole frame | 42,505,756 | identical to the reference, byte for byte |
| 3 rows, checksum | `0xCA8AB4F0` | identical to the reference |

The 200,000 row result is the strong one: the frame is built from a schema the
reference wrote, and the size agrees exactly with the reference's own wire size,
which means every field, separator and escape in the payload agrees too.

## The checksum kernel

The assembly kernel and the C kernel run the same slicing-by-eight algorithm over
the same tables, and the test compares them on 4,096 random bytes at every length in
steps of 97. They agree. On a target without the kernel, the C version runs and the
test reports that it skipped the equality check instead of failing.
