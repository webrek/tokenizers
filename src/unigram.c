#include "unigram.h"
#include <stdlib.h>
#include <string.h>
#include <float.h>

static const char MS[3]={(char)0xE2,(char)0x96,(char)0x81}; /* U+2581 */
static int boundary(const char *s,size_t i){ return (s[i]&0xC0)!=0x80; } /* codepoint start */

int tk_unigram_encode(const tk_model *m,const double *scores,const char *text,size_t len,
                      const tk_ug_opts *o,uint32_t **out_ids,size_t *n_out){
    /* Metaspace: prefix + replace spaces with ▁ */
    size_t cap=len*3+3; char *buf=malloc(cap?cap:1); size_t blen=0;
    if(o->add_prefix_space){ memcpy(buf,MS,3); blen=3; }
    for(size_t i=0;i<len;i++){
        if(text[i]==' '){ memcpy(buf+blen,MS,3); blen+=3; }
        else buf[blen++]=text[i];
    }
    size_t n=blen;
    /* Viterbi — use double precision to match the HF tokenizers library (f64). */
    double *best=malloc((n+1)*sizeof(double));
    size_t *bp_i=malloc((n+1)*sizeof(size_t));
    uint32_t *bp_id=malloc((n+1)*sizeof(uint32_t));
    for(size_t j=0;j<=n;j++){ best[j]=-DBL_MAX; bp_i[j]=(size_t)-1; bp_id[j]=o->unk_id; }
    best[0]=0;
    for(size_t i=0;i<n;i++){
        if(best[i]==-DBL_MAX) continue;
        if(!boundary(buf,i)) continue;
        size_t jmax = i+o->max_piece_len; if(jmax>n) jmax=n;
        for(size_t j=i+1;j<=jmax;j++){
            if(j<n && !boundary(buf,j)) continue; /* only at codepoint boundaries */
            uint32_t id=tk_model_rank(m,(const uint8_t*)(buf+i),j-i);
            if(id==TK_RANK_MAX) continue;
            double s=best[i]+scores[id];
            if(s>best[j]){ best[j]=s; bp_i[j]=i; bp_id[j]=id; }
        }
        /* unk fallback: always relax one codepoint so every position stays reachable */
        size_t e=i+1; while(e<n && !boundary(buf,e)) e++;
        if(best[i]+o->unk_score > best[e]){ best[e]=best[i]+o->unk_score; bp_i[e]=i; bp_id[e]=o->unk_id; }
    }
    /* backtrack */
    uint32_t *rev=malloc((n+1)*sizeof(uint32_t)); size_t rn=0;
    size_t j=n;
    while(j>0 && bp_i[j]!=(size_t)-1){ rev[rn++]=bp_id[j]; j=bp_i[j]; }
    uint32_t *ids=malloc((rn?rn:1)*sizeof(uint32_t));
    for(size_t k=0;k<rn;k++) ids[k]=rev[rn-1-k];
    free(rev); free(best); free(bp_i); free(bp_id); free(buf);
    *out_ids=ids; *n_out=rn; return 0;
}
