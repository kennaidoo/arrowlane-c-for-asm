/* The JSON escaper against the reference's own output for adversarial text.
 *
 * Every control character, the quote, the backslash, UTF-8 and a 300 byte string
 * are in the corpus. Escaping is where a codec becomes a security problem, so it is
 * checked against the reference rather than against an opinion.
 */
#include "aln.h"

#include <stdio.h>
#include <string.h>

static int hex_value(char character) {
    if (character >= '0' && character <= '9') {
        return character - '0';
    }
    if (character >= 'a' && character <= 'f') {
        return character - 'a' + 10;
    }
    if (character >= 'A' && character <= 'F') {
        return character - 'A' + 10;
    }
    return -1;
}

int main(void) {
    FILE *file = fopen(ALN_FIXTURE_DIR "/string_corpus.txt", "r");
    if (file == nullptr) {
        printf("FAIL: cannot open the corpus\n");
        return 1;
    }
    char line[2048];
    int checked = 0;
    int failed = 0;
    while (fgets(line, sizeof line, file) != nullptr) {
        size_t length = strlen(line);
        while (length > 0 && (line[length - 1] == '\n' || line[length - 1] == '\r')) {
            line[--length] = '\0';
        }
        const char *space = strchr(line, ' ');
        if (space == nullptr) {
            continue;
        }
        const size_t hex_length = (size_t)(space - line);
        const char *expected = space + 1;

        char text[600];
        size_t text_length = 0;
        for (size_t index = 0; index + 1 < hex_length + 1 && index < hex_length; index += 2) {
            const int high = hex_value(line[index]);
            const int low = hex_value(line[index + 1]);
            if (high < 0 || low < 0) {
                break;
            }
            text[text_length++] = (char)((high << 4) | low);
        }
        text[text_length] = '\0';

        char actual[2048];
        const size_t written = aln_json_string(actual, sizeof actual, text, text_length);
        ++checked;
        if (written == 0 || strcmp(actual, expected) != 0) {
            ++failed;
            if (failed <= 5) {
                printf("MISMATCH hex=%.*s\n  ours %s\n  reference %s\n", (int)hex_length, line,
                       actual, expected);
            }
        }
    }
    fclose(file);

    /* The buffer must never be overrun: a cap of 1 leaves room for the terminator. */
    char tiny[2];
    if (aln_json_string(tiny, sizeof tiny, "ab", 2) != 0) {
        ++failed;
        printf("FAIL: a one byte buffer should refuse a two character string\n");
    }
    ++checked;

    printf("%s: %d cases, %d failures\n", failed == 0 ? "ok" : "FAILED", checked, failed);
    return failed == 0 ? 0 : 1;
}
