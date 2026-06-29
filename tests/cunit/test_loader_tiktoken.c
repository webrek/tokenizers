#include "model.h"
#include "loader_tiktoken.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
int main(void) {
    char *err = NULL;
    tk_model *m = tk_load_tiktoken_file("tests/fixtures/mini.tiktoken", "x", &err);
    assert(m != NULL && err == NULL);
    assert(tk_model_rank(m, (const uint8_t*)"a", 1) == 0);
    assert(tk_model_rank(m, (const uint8_t*)"ab", 2) == 3);
    assert(tk_model_rank(m, (const uint8_t*)"bc", 2) == 4);
    assert(tk_model_vocab_size(m) == 5);
    tk_model_free(m);

    tk_model *bad = tk_load_tiktoken_file("tests/fixtures/does-not-exist", "x", &err);
    assert(bad == NULL && err != NULL);
    free(err);
    printf("test_loader_tiktoken OK\n");
    return 0;
}
