/*---------------------------------------------------------------------------*\
  test_enc_v2.c
  rade_enc_v2.c(V2 stateful encoder)が Python(gen_enc_ref.py)と一致するか検証。
  jh0veq
\*---------------------------------------------------------------------------*/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "rade_enc_v2.h"

extern const WeightArray radeencv2_arrays[];

static int meta_int(const char*k,const char*p){FILE*f=fopen(p,"r");char l[256];int v=0;
    if(!f)return 0; while(fgets(l,sizeof(l),f)){char*e=strchr(l,'=');if(!e)continue;*e=0;
    if(!strcmp(l,k)){v=atoi(e+1);break;}} fclose(f);return v;}
static float* rd(const char*p,int n){float*b=malloc(sizeof(float)*n);FILE*f=fopen(p,"rb");
    if(!f){fprintf(stderr,"開けません:%s\n",p);free(b);return NULL;}
    if((int)fread(b,sizeof(float),n,f)!=n){fprintf(stderr,"%s:size\n",p);fclose(f);free(b);return NULL;}
    fclose(f);return b;}

int main(void){
    int feature_dim=meta_int("feature_dim","enc_meta.txt");
    int frames_per_step=meta_int("frames_per_step","enc_meta.txt");
    int latent_dim=meta_int("latent_dim","enc_meta.txt");
    int num_steps=meta_int("num_steps","enc_meta.txt");
    int input_dim=feature_dim*frames_per_step;
    printf("meta: feature_dim=%d frames_per_step=%d latent_dim=%d num_steps=%d\n",
           feature_dim,frames_per_step,latent_dim,num_steps);

    float*ins=rd("enc_in.f32",num_steps*input_dim); if(!ins)return 1;
    float*ref=rd("enc_ref.f32",num_steps*latent_dim); if(!ref)return 1;

    RADEEncV2 model;
    if(init_radeencv2(&model,radeencv2_arrays)!=0){fprintf(stderr,"init失敗\n");return 1;}
    RADEEncV2State enc_state;
    rade_init_encoder_v2(&enc_state);

    float z[64];
    int fail=0; float max_rel=0;
    for(int s=0;s<num_steps;s++){
        rade_core_encoder_v2(&enc_state,&model,z,&ins[s*input_dim],0,1);
        const float*want=&ref[s*latent_dim];
        for(int i=0;i<latent_dim;i++){
            float rel=fabsf(z[i]-want[i])/(fabsf(want[i])+1e-4f);
            if(rel>max_rel)max_rel=rel;
            if(rel>1e-2f && fabsf(z[i]-want[i])>1e-3f){
                if(fail<10)printf("  [不一致] step=%d i=%d C=%.5f Py=%.5f rel=%.3e\n",s,i,z[i],want[i],rel);
                fail++;
            }
        }
        printf("  step=%d z[:3]=[%.5f %.5f %.5f]\n",s,z[0],z[1],z[2]);
    }
    printf("最大相対誤差=%.3e\n",max_rel);
    if(fail==0){printf("=== encoder C=Python 一致 成功 ===\n");return 0;}
    else{printf("=== 不一致 %d箇所 ===\n",fail);return 2;}
}
