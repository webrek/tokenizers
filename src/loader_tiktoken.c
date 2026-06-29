#include "loader_tiktoken.h"
#include "base64.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char *dup_err(const char *s){ char *p=malloc(strlen(s)+1); if(!p) return NULL; strcpy(p,s); return p; }

tk_model *tk_load_tiktoken_file(const char *path, const char *pattern, char **err) {
    if (err) *err = NULL;
    FILE *f = fopen(path, "rb");
    if (!f) { if (err) *err = dup_err("cannot open tiktoken file"); return NULL; }
    tk_model *m = tk_model_new(100000);
    char *line = NULL; size_t cap = 0; ssize_t n;
    uint8_t *buf = NULL; size_t bufcap = 0;
    while ((n = getline(&line, &cap, f)) != -1) {
        if (n == 0) continue;
        char *sp = memchr(line, ' ', (size_t)n);
        if (!sp) continue;
        size_t b64len = (size_t)(sp - line);
        if (b64len > bufcap) {
            bufcap = b64len;
            uint8_t *nb = realloc(buf, bufcap ? bufcap : 1);
            if (!nb) { free(buf); free(line); fclose(f); tk_model_free(m); if (err) *err = dup_err("out of memory"); return NULL; }
            buf = nb;
        }
        size_t outlen = 0;
        if (tk_b64_decode(line, b64len, buf, &outlen) != 0) {
            free(buf); free(line); fclose(f); tk_model_free(m);
            if (err) *err = dup_err("invalid base64 in tiktoken file"); return NULL;
        }
        uint32_t rank = (uint32_t)strtoul(sp + 1, NULL, 10);
        tk_model_add(m, buf, outlen, rank);
    }
    free(buf); free(line); fclose(f);
    /* store pattern string on the model; compiled in Task 5 */
    extern int tk_model_set_pattern_str(tk_model *, const char *);
    tk_model_set_pattern_str(m, pattern);
    return m;
}
