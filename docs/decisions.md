# Decisions

Short, dated, and each one states what would reverse it.

## D1 The schema flatbuffer is supplied, not synthesised

The frame header carries the Arrow schema as a base64 flatbuffer. Generating that
flatbuffer is a second codec, and this project is about the frame and the text lane.
So `aln_frame_encode` takes the raw schema bytes from the caller and base64 encodes
them into the header.

Consequence: byte identity is proven against a schema the reference wrote, and the
claim is honest about that.

Reverse if this build needs to write frames for tables the caller cannot describe,
at which point a schema writer has to exist.

## D2 The Fortran slice stays small

Fortran owns the fixture arrays and the median. It does not own the wire, because
the wire is byte plumbing with pointer arithmetic, which is C's job and not
Fortran's strength.

Reverse if a numeric slice appears that is both hot and dense. There is not one
today.

## D3 The assembly kernel is a checksum, not a rewrite

The one loop where hand written code can be read, measured and compared against a C
reference is the checksum. Everything else stays in C so the compiler can inline
across it.

Reverse if a profile shows another loop spending more than a third of the encode.
The float formatter is the current candidate, and its fix is an algorithm, not
assembly.

## D4 The decode path validates and returns a view

Decoding NDJSON into columns is a parser plus a schema, which the reference already
does in pyarrow. This build verifies the prefix and the checksum and hands back the
payload.

Consequence: the decode benchmark number is not comparable to the other
implementations, and both the README and `docs/performance.md` say so.

Reverse if a caller needs a table back, which would mean a parser and a column
builder.
