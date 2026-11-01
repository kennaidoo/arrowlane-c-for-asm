/* Fixture allocation, in one place, so the command line and the tests build the
 * same arrays from the same Fortran slice.
 */
#include "aln.h"

#include <stdlib.h>
#include <string.h>

enum aln_status aln_fixture_create(uint64_t rows, struct aln_fixture *out) {
    if (out == nullptr) {
        return ALN_ERR_INVALID;
    }
    memset(out, 0, sizeof *out);
    out->rows = rows;
    if (rows == 0) {
        return ALN_OK;
    }
    out->id = (int64_t *)calloc((size_t)rows, sizeof *out->id);
    out->name_is_null = (bool *)calloc((size_t)rows, sizeof *out->name_is_null);
    out->score = (double *)calloc((size_t)rows, sizeof *out->score);
    out->ok = (bool *)calloc((size_t)rows, sizeof *out->ok);
    out->day = (int32_t *)calloc((size_t)rows, sizeof *out->day);
    out->amount = (int64_t *)calloc((size_t)rows, sizeof *out->amount);
    out->ts_us = (int64_t *)calloc((size_t)rows, sizeof *out->ts_us);
    out->blob = (int8_t *)calloc((size_t)rows, 3u);
    if (out->id == nullptr || out->name_is_null == nullptr || out->score == nullptr || out->ok == nullptr ||
        out->day == nullptr || out->amount == nullptr || out->ts_us == nullptr || out->blob == nullptr) {
        aln_fixture_free(out);
        return ALN_ERR_NOMEM;
    }
    aln_fill_fixture((int64_t)rows, out->id, out->name_is_null, out->score, out->ok, out->day,
                     out->amount, out->ts_us, out->blob);
    return ALN_OK;
}

void aln_fixture_free(struct aln_fixture *fixture) {
    if (fixture == nullptr) {
        return;
    }
    free(fixture->id);
    free(fixture->name_is_null);
    free(fixture->score);
    free(fixture->ok);
    free(fixture->day);
    free(fixture->amount);
    free(fixture->ts_us);
    free(fixture->blob);
    memset(fixture, 0, sizeof *fixture);
}
