#include "model.h"
#include "wordpiece.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
int main(void){
    tk_model *m=tk_model_new(16);
    /* vocab: "un"=0 "##aff"=1 "##able"=2 "[UNK]"=3 "play"=4 "##ing"=5 */
    tk_model_add(m,(const uint8_t*)"un",2,0);
    tk_model_add(m,(const uint8_t*)"##aff",5,1);
    tk_model_add(m,(const uint8_t*)"##able",6,2);
    tk_model_add(m,(const uint8_t*)"[UNK]",5,3);
    tk_model_add(m,(const uint8_t*)"play",4,4);
    tk_model_add(m,(const uint8_t*)"##ing",5,5);
    tk_wp_opts o={3,"##",2,100,1,1,1};
    uint32_t *ids; size_t n;
    assert(tk_wordpiece_encode(m,"unaffable",9,&o,&ids,&n)==0);
    assert(n==3 && ids[0]==0 && ids[1]==1 && ids[2]==2); free(ids);     /* un ##aff ##able */
    assert(tk_wordpiece_encode(m,"playing",7,&o,&ids,&n)==0);
    assert(n==2 && ids[0]==4 && ids[1]==5); free(ids);                  /* play ##ing */
    assert(tk_wordpiece_encode(m,"zzz",3,&o,&ids,&n)==0);
    assert(n==1 && ids[0]==3); free(ids);                              /* [UNK] */
    tk_model_free(m);
    printf("test_wordpiece OK\n"); return 0;
}
