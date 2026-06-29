#include "base64.h"
#include <assert.h>
#include <string.h>
#include <stdio.h>
static void chk(const char *b64, const char *expect, size_t elen) {
    uint8_t out[256]; size_t n = 0;
    assert(tk_b64_decode(b64, strlen(b64), out, &n) == 0);
    assert(n == elen && memcmp(out, expect, elen) == 0);
}
int main(void) {
    chk("IQ==", "!", 1);          /* 0x21 */
    chk("aGVsbG8=", "hello", 5);
    chk("AA==", "\x00", 1);
    chk("", "", 0);
    uint8_t out[8]; size_t n;
    assert(tk_b64_decode("!!", 2, out, &n) == -1); /* invalid */
    { uint8_t o2[16]; size_t n2;
      assert(tk_b64_decode("YQ========", 10, o2, &n2) == -1);  /* >2 padding rejected */
      assert(tk_b64_decode("YR==", 4, o2, &n2) == -1);          /* non-zero trailing bits rejected */
    }
    printf("test_base64 OK\n");
    return 0;
}
