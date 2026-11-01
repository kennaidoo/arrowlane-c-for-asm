"""Write the float corpus: one line per value, the double's hex bits and the text
the reference emits for it.

The reference here is orjson, which is what the Python implementation's json lane
uses. Running this script is how the fixture is regenerated; the corpus is committed
so the C test needs no Python.

Run with an interpreter that has orjson, from the repository root.
"""

from __future__ import annotations

import math
import random
import struct

import orjson

EDGE_CASES = [
    0.0,
    -0.0,
    1.0,
    -1.0,
    0.1,
    1e-300,
    1e300,
    2.2250738585072014e-308,  # smallest normal
    1.7976931348623157e308,  # largest finite
    123456789.0,
    1e16,
    1e15,
    1e-7,
    0.5,
    1.0 / 3.0,
    2.0 / 3.0,
    1e21,
    1e-21,
    123456789012345678.0,
]


def main() -> None:
    random.seed(20261101)
    values = list(EDGE_CASES)
    while len(values) < 4000:
        bits = random.getrandbits(64)
        value = struct.unpack("<d", struct.pack("<Q", bits))[0]
        if math.isfinite(value):
            values.append(value)
    values.extend(float(index) / 3.0 for index in range(1, 500))

    lines = []
    for value in values:
        bits = struct.unpack("<Q", struct.pack("<d", value))[0]
        lines.append(f"{bits:016x} {orjson.dumps(value).decode('ascii')}\n")
    with open("tests/fixtures/float_corpus.txt", "w", encoding="ascii", newline="\n") as stream:
        stream.writelines(lines)
    print(f"wrote {len(values)} values")


if __name__ == "__main__":
    main()
