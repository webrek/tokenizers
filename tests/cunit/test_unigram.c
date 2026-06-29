#include "model.h"
#include "unigram.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
int main(void){
    tk_model *m=tk_model_new(16);
    /* pieces: "▁he"=0 "llo"=1 "▁"=2 "h"=3 "e"=4 "l"=5 "o"=6 "<unk>"=7 */
    tk_model_add(m,(const uint8_t*)"\xE2\x96\x81he",5,0);
    tk_model_add(m,(const uint8_t*)"llo",3,1);
    tk_model_add(m,(const uint8_t*)"\xE2\x96\x81",3,2);
    tk_model_add(m,(const uint8_t*)"h",1,3);
    tk_model_add(m,(const uint8_t*)"e",1,4);
    tk_model_add(m,(const uint8_t*)"l",1,5);
    tk_model_add(m,(const uint8_t*)"o",1,6);
    tk_model_add(m,(const uint8_t*)"<unk>",5,7);
    /* scores: prefer "▁he"(-1) + "llo"(-1) over char-by-char (-2 each) */
    double sc[8]={-1,-1,-3,-2,-2,-2,-2,-10};
    tk_ug_opts o={7,-10.0,1,5}; /* unk id 7, add prefix ▁, max piece len 5 bytes */
    uint32_t *ids; size_t n;
    assert(tk_unigram_encode(m,sc,"hello",5,&o,&ids,&n)==0);
    /* with add_prefix_space: "▁hello" -> "▁he"+"llo" = [0,1] */
    assert(n==2 && ids[0]==0 && ids[1]==1); free(ids);
    tk_model_free(m);
    printf("test_unigram OK\n"); return 0;
}
