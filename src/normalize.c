#include "normalize.h"
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

/* minimal UTF-8 decode: returns codepoint, advances *i */
static uint32_t u8_next(const unsigned char *s, size_t len, size_t *i){
    uint32_t c=s[*i]; size_t n=1;
    if(c<0x80){} else if((c>>5)==0x6){c&=0x1F;n=2;} else if((c>>4)==0xE){c&=0x0F;n=3;} else if((c>>3)==0x1E){c&=0x07;n=4;} else {(*i)++;return 0xFFFD;}
    if(*i+n>len){(*i)=len;return 0xFFFD;}
    for(size_t k=1;k<n;k++) c=(c<<6)|(s[*i+k]&0x3F);
    *i+=n; return c;
}
static void u8_emit(char **buf,size_t *blen,size_t *bcap,uint32_t c){
    if(*blen+4>*bcap){ size_t ncap=*bcap?*bcap*2:64; char *t=realloc(*buf,ncap); if(!t) return; *buf=t; *bcap=ncap; }
    char *o=*buf+*blen;
    if(c<0x80){o[0]=c;*blen+=1;}
    else if(c<0x800){o[0]=0xC0|(c>>6);o[1]=0x80|(c&0x3F);*blen+=2;}
    else if(c<0x10000){o[0]=0xE0|(c>>12);o[1]=0x80|((c>>6)&0x3F);o[2]=0x80|(c&0x3F);*blen+=3;}
    else {o[0]=0xF0|(c>>18);o[1]=0x80|((c>>12)&0x3F);o[2]=0x80|((c>>6)&0x3F);o[3]=0x80|(c&0x3F);*blen+=4;}
}
/* Latin-1 Supplement accent strip (U+00C0..U+00FF -> ASCII base); 0 if none */
static uint32_t strip_latin1(uint32_t c){
    static const char *map="AAAAAAACEEEEIIIIDNOOOOOxOUUUUYPsaaaaaaaceeeeiiiidnooooo/ouuuuypy";
    if(c>=0xC0 && c<=0xFF){ char b=map[c-0xC0]; if(b!='x'&&b!='/') return (uint32_t)(unsigned char)b; }
    return 0;
}
static uint32_t to_lower(uint32_t c){
    if(c>='A'&&c<='Z') return c+32;
    if(c>=0xC0&&c<=0xDE&&c!=0xD7) return c+0x20; /* Latin-1 uppercase -> lower */
    return c;
}
static int is_space(uint32_t c){ return c==' '||c=='\t'||c=='\n'||c=='\r'||c==0x0c||c==0xA0; }
static int is_control(uint32_t c){ return (c<0x20&&!is_space(c))||c==0x7f||(c>=0x80&&c<=0x9F); }
static int is_cjk(uint32_t c){ return (c>=0x4E00&&c<=0x9FFF)||(c>=0x3400&&c<=0x4DBF)||(c>=0xF900&&c<=0xFAFF)||(c>=0x20000&&c<=0x2A6DF); }
static int is_punct(uint32_t c){
    if((c>=33&&c<=47)||(c>=58&&c<=64)||(c>=91&&c<=96)||(c>=123&&c<=126)) return 1; /* ASCII punct */
    return 0;
}

/* Safe span append: reallocs into temp; returns 0 on success, -1 on OOM (span skipped, old sp intact) */
static int sp_push(tk_span **sp, size_t *ns, size_t *scap, size_t start, size_t end){
    if(*ns==*scap){
        size_t ncap=*scap?*scap*2:16;
        tk_span *t=realloc(*sp,ncap*sizeof(tk_span));
        if(!t) return -1;
        *sp=t; *scap=ncap;
    }
    (*sp)[*ns].start=start; (*sp)[*ns].end=end; (*ns)++;
    return 0;
}

int tk_bert_normalize(const char *text,size_t len,int lowercase,int strip_accents,int handle_cjk,
                      char **out,size_t *out_len,tk_span **spans,size_t *nspans){
    char *buf=NULL; size_t blen=0,bcap=0;
    tk_span *sp=NULL; size_t ns=0,scap=0;
    int in_word=0; size_t word_start=0;
    #define ENDWORD() do{ if(in_word){ sp_push(&sp,&ns,&scap,word_start,blen); in_word=0;} }while(0)
    #define SOLO(emitc) do{ ENDWORD(); size_t s0=blen; u8_emit(&buf,&blen,&bcap,(emitc)); sp_push(&sp,&ns,&scap,s0,blen); }while(0)
    size_t i=0;
    while(i<len){
        uint32_t c=u8_next((const unsigned char*)text,len,&i);
        if(c==0||c==0xFFFD||is_control(c)) continue;      /* drop NUL, replacement char (U+FFFD), and controls */
        if(is_space(c)){ ENDWORD(); continue; }
        if(handle_cjk && is_cjk(c)){ SOLO(c); continue; }
        if(is_punct(c)){ SOLO(c); continue; }
        if(strip_accents){ uint32_t b=strip_latin1(c); if(b){ c=b; } }
        if(lowercase) c=to_lower(c);
        if(strip_accents){ uint32_t b=strip_latin1(c); if(b) c=b; } /* re-strip after lower */
        if(!in_word){ in_word=1; word_start=blen; }
        u8_emit(&buf,&blen,&bcap,c);
    }
    ENDWORD();
    if(!buf){ buf=malloc(1); }
    *out=buf; *out_len=blen; *spans=sp; *nspans=ns;
    return 0;
    #undef ENDWORD
    #undef SOLO
}
