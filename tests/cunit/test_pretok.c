#include "model.h"
#include "pretok.h"
#include <assert.h>
#include <string.h>
#include <stdio.h>

static char pieces[16][32]; static int np;
static void emit(void *ud, size_t s, size_t e) {
    (void)ud; const uint8_t *t = (const uint8_t*)"ab cd";
    memcpy(pieces[np], t + s, e - s); pieces[np][e - s] = 0; np++;
}
int main(void) {
    tk_model *m = tk_model_new(8);
    /* simple pre-tokenizer: words or runs of spaces */
    assert(tk_model_set_pattern(m, "\\w+| +") == 0);
    np = 0;
    assert(tk_pretok_split(m, (const uint8_t*)"ab cd", 5, emit, NULL) == 0);
    assert(np == 3);
    assert(strcmp(pieces[0], "ab") == 0);
    assert(strcmp(pieces[1], " ") == 0);
    assert(strcmp(pieces[2], "cd") == 0);
    tk_model_free(m);
    printf("test_pretok OK\n");
    return 0;
}
