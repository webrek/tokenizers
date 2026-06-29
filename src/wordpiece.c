#include "wordpiece.h"
#include "normalize.h"
#include <stdlib.h>
#include <string.h>
/* count UTF-8 codepoints in [s,s+n) */
static size_t cp_count(const char *s,size_t n){ size_t c=0; for(size_t i=0;i<n;i++) if((s[i]&0xC0)!=0x80) c++; return c; }
/* advance one codepoint */
static size_t cp_adv(const char *s,size_t n,size_t i){ i++; while(i<n && (s[i]&0xC0)==0x80) i++; return i; }

int tk_wordpiece_encode(const tk_model *m,const char *text,size_t len,const tk_wp_opts *o,
                        uint32_t **out_ids,size_t *n_out){
    char *norm; size_t nlen; tk_span *sp; size_t ns;
    tk_bert_normalize(text,len,o->lowercase,o->strip_accents,o->handle_cjk,&norm,&nlen,&sp,&ns);
    uint32_t *ids=NULL; size_t cap=0,no=0;
    char tmp[512];
    for(size_t w=0;w<ns;w++){
        const char *word=norm+sp[w].start; size_t wlen=sp[w].end-sp[w].start;
        size_t emit_start=no; int bad=0;
        if(cp_count(word,wlen) > o->max_chars){ bad=1; }
        size_t start=0;
        while(!bad && start<wlen){
            size_t end=wlen, best_end=0; uint32_t best_id=TK_RANK_MAX;
            /* longest match from start, shrinking end by codepoints */
            while(end>start){
                size_t plen=end-start; const char *piece=word+start;
                uint32_t id;
                if(start>0){ /* prepend prefix */
                    if(o->prefix_len+plen<=sizeof(tmp)){ memcpy(tmp,o->prefix,o->prefix_len); memcpy(tmp+o->prefix_len,piece,plen);
                        id=tk_model_rank(m,(const uint8_t*)tmp,o->prefix_len+plen); }
                    else id=TK_RANK_MAX;
                } else id=tk_model_rank(m,(const uint8_t*)piece,plen);
                if(id!=TK_RANK_MAX){ best_end=end; best_id=id; break; }
                /* shrink end by one codepoint */
                size_t e=start; while(e<end){ size_t ne=cp_adv(word,wlen,e); if(ne>=end) break; e=ne; } end=e;
                if(end<=start) break;
            }
            if(best_id==TK_RANK_MAX){ bad=1; break; }
            if(no==cap){cap=cap?cap*2:16; ids=realloc(ids,cap*sizeof(uint32_t));}
            ids[no++]=best_id; start=best_end;
        }
        if(bad){ no=emit_start; if(no==cap){cap=cap?cap*2:16; ids=realloc(ids,cap*sizeof(uint32_t));} ids[no++]=o->unk_id; }
    }
    free(norm); free(sp);
    *out_ids=ids; *n_out=no; return 0;
}
