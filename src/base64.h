#ifndef TK_BASE64_H
#define TK_BASE64_H
#include <stddef.h>
#include <stdint.h>
int tk_b64_decode(const char *in, size_t in_len, uint8_t *out, size_t *out_len);
#endif
