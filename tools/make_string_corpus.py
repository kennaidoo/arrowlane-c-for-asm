"""Write the string corpus: adversarial text and the exact JSON literal the
reference emits for it.

Escaping is the place where a codec becomes a security problem, so it is tested
against the reference with quotes, backslashes, every control character, and UTF-8
rather than with one happy path.

Each line is the hex of the UTF-8 bytes, a space, then the expected literal. The
expected part may contain spaces, so a reader splits on the first space only.
"""

from __future__ import annotations

import orjson

CASES = [
    "",
    "row-1",
    'quote " inside',
    "backslash \\ inside",
    "both \\\" together",
    "newline \n here",
    "tab \t here",
    "carriage \r here",
    "nul byte \x00 here",
    "bell \x07 and escape \x1b",
    "unit separator \x1f",
    "delete \x7f",
    "cafe \u00e9",
    "japanese \u65e5\u672c\u8a9e",
    "emoji \U0001f600",
    "combining a\u0301",
    "long " + "x" * 300,
    "/slash/ and <tag> & ampersand",
    "already escaped \\u0041",
    "{}[]:, and spaces",
]


def main() -> None:
    lines = []
    for text in CASES:
        raw = text.encode("utf-8")
        expected = orjson.dumps(text).decode("utf-8")
        lines.append(f"{raw.hex()} {expected}\n")
    with open("tests/fixtures/string_corpus.txt", "w", encoding="utf-8", newline="\n") as stream:
        stream.writelines(lines)
    print(f"wrote {len(CASES)} strings")


if __name__ == "__main__":
    main()
