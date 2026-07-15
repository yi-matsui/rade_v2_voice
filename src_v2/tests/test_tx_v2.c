/*---------------------------------------------------------------------------*\
  test_tx_v2.c
  送信部全体(rade_tx_v2.c + rade_enc_v2.c)が Python(gen_tx_ref.py)と
  一致するか検証。jh0veq
\*---------------------------------------------------------------------------*/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "rade_tx_v2.h"

extern const WeightArray radeencv2_arrays[];

static int mi(const char*k,const char*p){FILE*f=fopen(p,"r");char l[256];int v=0;
    if(!f)return 0; while(fgets(l,sizeof(l),f)){char*e=strchr(l,'=');if(!e)continue;*e=0;
    if(!strcmp(l,k)){v=atoi(e+1);break;}} fclose(f);return v;}
static float* rd(const char*p,int n){float*b=malloc(sizeof(float)*n);FILE*f=fopen(p,"rb");
    if(!f){fprintf(stderr,"開けません:%s\n",p);free(b);return NULL;}
    if((int)fread(b,sizeof(float),n,f)!=n){fprintf(stderr,"%s:size\n",p);fclose(f);free(b);return NULL;}
    fclose(f);return b;}

int main(void){
    const char*MT="tx_meta.txt";
    int M=mi("M",MT),Ncp=mi("Ncp",MT),Ns=mi("Ns",MT),Nc=mi("Nc",MT),sym_len=mi("sym_len",MT);
    int feature_dim=mi("feature_dim",MT),frames_per_step=mi("frames_per_step",MT);
    int num_steps=mi("num_steps",MT);
    int input_dim=feature_dim*frames_per_step;
    printf("meta: M=%d Ncp=%d Ns=%d Nc=%d sym_len=%d fd=%d fps=%d num=%d\n",
           M,Ncp,Ns,Nc,sym_len,feature_dim,frames_per_step,num_steps);

    float*winv_il=rd("tx_winv.f32",Nc*M*2); if(!winv_il)return 1;
    RADE_COMP*Winv=malloc(sizeof(RADE_COMP)*Nc*M);
    for(int i=0;i<Nc*M;i++){Winv[i].real=winv_il[2*i];Winv[i].imag=winv_il[2*i+1];}

    float*ins=rd("tx_in.f32",num_steps*input_dim); if(!ins)return 1;
    float*ref_il=rd("tx_ref.f32",num_steps*Ns*sym_len*2); if(!ref_il)return 1;

    RADEEncV2 enc_model;
    if(init_radeencv2(&enc_model,radeencv2_arrays)!=0){fprintf(stderr,"init失敗\n");return 1;}
    RADEEncV2State enc_state;
    rade_init_encoder_v2(&enc_state);

    rade_tx_v2_state tx;
    rtx_init(&tx,M,Ncp,Ns,Nc,56,feature_dim,frames_per_step,Winv,&enc_state,&enc_model,0);

    RADE_COMP*tx_out=malloc(sizeof(RADE_COMP)*Ns*sym_len);
    int fail=0; float max_rel=0;
    for(int s=0;s<num_steps;s++){
        rtx_transmit_frame(&tx,&ins[s*input_dim],tx_out);
        const float*w=&ref_il[s*Ns*sym_len*2];
        for(int i=0;i<Ns*sym_len;i++){
            float rr=fabsf(tx_out[i].real-w[2*i])/(fabsf(w[2*i])+1e-4f);
            float ri=fabsf(tx_out[i].imag-w[2*i+1])/(fabsf(w[2*i+1])+1e-4f);
            if(rr>max_rel)max_rel=rr; if(ri>max_rel)max_rel=ri;
            if((rr>1e-2f&&fabsf(tx_out[i].real-w[2*i])>1e-3f)||
               (ri>1e-2f&&fabsf(tx_out[i].imag-w[2*i+1])>1e-3f)){
                if(fail<10)printf("  [不一致] step=%d i=%d C=(%.5f,%.5f) Py=(%.5f,%.5f)\n",
                                  s,i,tx_out[i].real,tx_out[i].imag,w[2*i],w[2*i+1]);
                fail++;
            }
        }
        printf("  step=%d tx[:3]=[(%.5f,%.5f) (%.5f,%.5f) (%.5f,%.5f)]\n",s,
               tx_out[0].real,tx_out[0].imag,tx_out[1].real,tx_out[1].imag,tx_out[2].real,tx_out[2].imag);
    }
    printf("最大相対誤差=%.3e\n",max_rel);
    if(fail==0){printf("=== 送信部 C=Python 一致 成功 ===\n");return 0;}
    else{printf("=== 不一致 %d箇所 ===\n",fail);return 2;}
}
