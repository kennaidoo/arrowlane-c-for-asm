# arrowlane-c-for-asm

A third implementation of the arrowlane wire format, in C, with one slice in
Fortran and one in assembly. It writes bytes that the Python reference produces, and
it reads bytes the reference writes.

The wire format is a 38 byte prefix, a compact JSON header, and an opaque payload.
This implementation covers the `json` lane for the benchmark fixture, the frame in
both directions, and the checksum.

## Why three languages

The split is not decoration. It follows what each part of the job is:

| Slice | Language | Job | Why |
|---|---|---|---|
| Wire and text | C | prefix, header, NDJSON writer, base64 | byte work with no abstraction to pay for |
| Fixture and statistics | Fortran | the eight column arrays, the median | dense numeric loops, and it is honest about its own slice being small |
| Checksum | assembly | CRC-32, slicing by eight, x86-64 | the one loop where hand written code can be read and measured directly |

The Fortran slice is deliberately small. The codec is byte plumbing, and pretending
otherwise would be a worse design than admitting it.

## What is proven

The tests compare against files the Python reference wrote, so a pass is a claim
about compatibility rather than about self consistency:

- the NDJSON payload for the 3 row fixture is **606 bytes and identical to the byte**;
- the whole frame for that table is **1372 bytes and identical to the byte**, header included;
- the checksum of that payload is `0xCA8AB4F0`, which is what the reference computes;
- a frame the reference wrote decodes here, with the prefix fields and the checksum verified;
- damaged frames are refused: bad magic, bad version, truncation, and a single flipped payload byte.

## What this does not do

Honest scope, because the other three implementations do more:

- **no `ipc`, `ipc-zstd` or `shm` lane.** Only `json` is written. Frames on other lanes are recognised and rejected.
- **no schema synthesis.** The Arrow schema flatbuffer is supplied by the caller and base64 encoded into the header. This build does not generate the flatbuffer itself, which is why the byte identity claim is about a schema the reference produced.
- **no chunk streams.**
- **the assembly kernel is x86-64 System V only.** On other targets, including Windows, the portable C checksum runs instead, and the test says so instead of failing.

## Build

Linux or WSL, with a C compiler, gfortran, and CMake 3.20 or later:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
```

## Test

```bash
ctest --test-dir build --output-on-failure
```

Expected:

```
    Start 1: aln_test
1/3 Test #1: aln_test ..........................   Passed
    Start 2: cli_bench
2/3 Test #2: cli_bench .........................   Passed
    Start 3: cli_describe
3/3 Test #3: cli_describe ......................   Passed
100% tests passed, 0 tests failed out of 3
```

## Run

```bash
./build/arrowlane-c-for-asm describe
./build/arrowlane-c-for-asm bench --rows 200000 --repeats 5
```

`bench` prints one line in the same shape the other arrowlane implementations print,
so one parser reads all four:

```
json           42505756      885.642       37.858
```

## Measured

200,000 rows, Release, five repeats, median, on one machine:

| Implementation | Wire bytes | Encode ms | Decode ms |
|---|---|---|---|
| Python reference | 42,505,756 | 2,131.7 | 2,081.9 |
| C++ port | 42,505,784 | 244.7 | 827.4 |
| this project | **42,505,756** | 885.6 | 37.9 |

The encode column is a fair comparison: 2.4 times faster than the Python reference,
3.6 times slower than the C++ port. The gap is one function, the shortest-round-trip
float formatter, which searches `printf` precisions and reads each back. That is
correct and slow, and a Ryu style formatter is the next lever.

The decode column is **not** a comparison. This build validates the prefix and the
checksum and returns a view of the payload. The other two parse the payload into
columns. `docs/performance.md` states that plainly rather than claiming a win.

## Layout

```
include/aln.h          the whole public surface
src/frame.c            prefix, header JSON, encode and decode
src/json_fixture.c     the NDJSON writer, one branch per column type
src/base64.c           standard alphabet with padding
src/crc32.c            CRC-32 slicing by eight, tables built once
src/crc32_x86.S         the same algorithm in hand written x86-64 assembly
src/fixture.f90        the fixture arrays and the median
src/main.c             bench and describe
tests/aln_test.c       the compatibility tests
tests/fixtures/        golden payload, frame and schema, written by the reference
```

## License

MIT. See `LICENSE`.
