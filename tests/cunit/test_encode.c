#include "model.h"
#include "engine.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
int main(void) {
    tk_model *m = tk_model_new(8);
    tk_model_add(m, (const uint8_t*)"a", 1, 0);
    tk_model_add(m, (const uint8_t*)"b", 1, 1);
    tk_model_add(m, (const uint8_t*)"c", 1, 2);
    tk_model_add(m, (const uint8_t*)" ", 1, 3);
    tk_model_add(m, (const uint8_t*)"ab", 2, 4);
    assert(tk_model_set_pattern(m, "\\w+| +") == 0);
    tk_model_add_special(m, "<|end|>", 7, 100);

    tk_ids ids; tk_ids_init(&ids);
    assert(tk_encode_ordinary(m, (const uint8_t*)"ab c", 4, &ids) == 0);
    /* "ab" -> [4] ; " " -> [3] ; "c" -> [2] */
    assert(ids.len == 3 && ids.data[0] == 4 && ids.data[1] == 3 && ids.data[2] == 2);
    tk_ids_free(&ids);

    assert(tk_count(m, (const uint8_t*)"ab c", 4) == 3);

    /* disallowed special present -> error */
    tk_ids_init(&ids); char *err = NULL;
    int rc = tk_encode(m, (const uint8_t*)"a<|end|>", 8, NULL, 0, 1, &ids, &err);
    assert(rc != 0 && err != NULL); free(err); tk_ids_free(&ids);

    /* allowed special -> emitted as id 100 */
    tk_ids_init(&ids); err = NULL;
    const char *allow[] = { "<|end|>" };
    rc = tk_encode(m, (const uint8_t*)"a<|end|>", 8, allow, 1, 1, &ids, &err);
    assert(rc == 0 && ids.len == 2 && ids.data[0] == 0 && ids.data[1] == 100);
    tk_ids_free(&ids);

    /* decode round-trip of ordinary ids */
    uint8_t *out; size_t olen; uint32_t r[] = {4,3,2};
    assert(tk_decode(m, r, 3, &out, &olen, &err) == 0);
    assert(olen == 4 && memcmp(out, "ab c", 4) == 0); free(out);

    tk_model_free(m);
    printf("test_encode OK\n");
    return 0;
}
