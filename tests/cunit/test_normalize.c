#include "normalize.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
static int span_is(const char *buf, tk_span s, const char *exp){ size_t n=s.end-s.start; return n==strlen(exp) && memcmp(buf+s.start,exp,n)==0; }
int main(void){
    char *out; size_t olen; tk_span *sp; size_t ns;
    assert(tk_bert_normalize("Hello, World!", 13, 1,1,1, &out,&olen,&sp,&ns)==0);
    assert(ns==4 && span_is(out,sp[0],"hello") && span_is(out,sp[1],",") && span_is(out,sp[2],"world") && span_is(out,sp[3],"!"));
    free(out); free(sp);
    /* accent strip: café -> cafe */
    assert(tk_bert_normalize("caf\xC3\xA9", 5, 1,1,1, &out,&olen,&sp,&ns)==0);
    assert(ns==1 && span_is(out,sp[0],"cafe")); free(out); free(sp);
    /* CJK spacing: each ideograph isolated */
    assert(tk_bert_normalize("\xE4\xBD\xA0\xE5\xA5\xBD", 6, 1,1,1, &out,&olen,&sp,&ns)==0);
    assert(ns==2); free(out); free(sp);
    /* no-lowercase keeps case */
    assert(tk_bert_normalize("AB", 2, 0,0,0, &out,&olen,&sp,&ns)==0);
    assert(ns==1 && span_is(out,sp[0],"AB")); free(out); free(sp);
    printf("test_normalize OK\n"); return 0;
}
