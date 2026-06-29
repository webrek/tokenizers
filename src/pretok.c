#define PCRE2_CODE_UNIT_WIDTH 8
#include <pcre2.h>
#include "model.h"
#include "pretok.h"
#include <string.h>
#include <stdlib.h>

static void pcre2_code_free_void(void *code) { pcre2_code_free((pcre2_code*)code); }

int tk_model_set_pattern(tk_model *m, const char *pattern) {
    int errnum; PCRE2_SIZE erroff;
    pcre2_code *code = pcre2_compile((PCRE2_SPTR)pattern, PCRE2_ZERO_TERMINATED,
                                     PCRE2_UTF | PCRE2_UCP, &errnum, &erroff, NULL);
    if (!code) return -1;
    pcre2_jit_compile(code, PCRE2_JIT_COMPLETE); /* best-effort; matching falls back if unavailable */
    tk_model__set_pcre2(m, code, pcre2_code_free_void);
    char **slot = tk_model__pattern_slot(m);
    free(*slot); *slot = malloc(strlen(pattern) + 1); strcpy(*slot, pattern);
    return 0;
}

int tk_pretok_split(const tk_model *m, const uint8_t *text, size_t len,
                    void (*emit)(void *ud, size_t start, size_t end), void *ud) {
    const pcre2_code *code = (const pcre2_code*)tk_model_pattern_code(m);
    if (!code) return -1;
    pcre2_match_data *md = pcre2_match_data_create(1, NULL);
    PCRE2_SIZE off = 0;
    while (off < len) {
        int rc = pcre2_match(code, (PCRE2_SPTR)text, len, off, 0, md, NULL);
        if (rc < 0) break; /* no more matches */
        PCRE2_SIZE *ov = pcre2_get_ovector_pointer(md);
        size_t s = ov[0], e = ov[1];
        if (e == s) { e = s + 1; } /* guard against zero-width to ensure progress */
        emit(ud, s, e);
        off = e;
    }
    pcre2_match_data_free(md);
    return 0;
}
