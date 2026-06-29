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

    /* (review fix) decode of a SPECIAL id must reconstruct the special's bytes */
    {
        uint32_t sids[] = { 100 };
        uint8_t *sp; size_t splen; char *e2 = NULL;
        assert(tk_decode(m, sids, 1, &sp, &splen, &e2) == 0);
        assert(splen == 7 && memcmp(sp, "<|end|>", 7) == 0);
        free(sp);
    }

    /* (review fix) unlisted special with disallow_unlisted=0 -> encoded as ORDINARY bytes */
    {
        tk_model_add_special(m, "abab", 4, 200);   /* a special whose bytes tokenize cleanly: ab+ab */
        tk_ids tids; tk_ids_init(&tids); char *e3 = NULL;
        int rc3 = tk_encode(m, (const uint8_t*)"abab", 4, NULL, 0, /*disallow_unlisted=*/0, &tids, &e3);
        assert(rc3 == 0 && tids.len == 2 && tids.data[0] == 4 && tids.data[1] == 4); /* ab, ab */
        tk_ids_free(&tids);
    }

    tk_model_free(m);
    printf("test_encode OK\n");
    return 0;
}
