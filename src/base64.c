#include "base64.h"
static int8_t dec_tab(unsigned char c) {
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+') return 62;
    if (c == '/') return 63;
    return -1;
}
int tk_b64_decode(const char *in, size_t in_len, uint8_t *out, size_t *out_len) {
    while (in_len > 0 && in[in_len - 1] == '=') in_len--;
    size_t o = 0; uint32_t acc = 0; int bits = 0;
    for (size_t i = 0; i < in_len; i++) {
        int8_t v = dec_tab((unsigned char)in[i]);
        if (v < 0) return -1;
        acc = (acc << 6) | (uint32_t)v; bits += 6;
        if (bits >= 8) { bits -= 8; out[o++] = (uint8_t)((acc >> bits) & 0xFF); }
    }
    *out_len = o;
    return 0;
}
