/*---------------------------------------------------------------------------*\
  test_extract_v2.c
  rade_extract_v2.c(_extract_symbol 移植)が Python(gen_extract_ref.py)と
  一致するか検証。z_hat(latent)と rx_sym_td を照合する。
\*---------------------------------------------------------------------------*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "rade_extract_v2.h"

static int read_meta(const char *p,int *M,int *Ncp,int *Ns,int *Nc,
                     int *ld,float *Fs,int *sym_len,int *num_calls){
    FILE *f=fopen(p,"r"); if(!f){fprintf(stderr,"meta開けません\n");return -1;}
    char line[256];
    while(fgets(line,sizeof(line),f)){
        char *eq=strchr(line,'='); if(!eq)continue; *eq=0; float v=(float)atof(eq+1);
        if(!strcmp(line,"M"))*M=(int)v; else if(!strcmp(line,"Ncp"))*Ncp=(int)v;
        else if(!strcmp(line,"Ns"))*Ns=(int)v; else if(!strcmp(line,"Nc"))*Nc=(int)v;
        else if(!strcmp(line,"latent_dim"))*ld=(int)v; else if(!strcmp(line,"Fs"))*Fs=v;
        else if(!strcmp(line,"sym_len"))*sym_len=(int)v;
        else if(!strcmp(line,"num_calls"))*num_calls=(int)v;
    }
    fclose(f); return 0;
}
static float* rd(const char *p,int n){
    float *b=(float*)malloc(sizeof(float)*n); FILE *f=fopen(p,"rb");
    if(!f){fprintf(stderr,"開けません:%s\n",p);free(b);return NULL;}
    if((int)fread(b,sizeof(float),n,f)!=n){fprintf(stderr,"%s:サイズ不足\n",p);fclose(f);free(b);return NULL;}
    fclose(f); return b;
}

int main(void){
    int M=0,Ncp=0,Ns=0,Nc=0,ld=0,sym_len=0,num_calls=0; float Fs=0;
    if(read_meta("extract_meta.txt",&M,&Ncp,&Ns,&Nc,&ld,&Fs,&sym_len,&num_calls))return 1;
    printf("meta: M=%d Ncp=%d Ns=%d Nc=%d latent_dim=%d sym_len=%d num_calls=%d\n",
           M,Ncp,Ns,Nc,ld,sym_len,num_calls);

    /* Wfwd (M*Nc 複素) */
    float *wf=rd("extract_wfwd.f32",M*Nc*2); if(!wf)return 1;
    RADE_COMP *Wfwd=(RADE_COMP*)malloc(sizeof(RADE_COMP)*M*Nc);
    for(int i=0;i<M*Nc;i++){Wfwd[i].real=wf[2*i];Wfwd[i].imag=wf[2*i+1];}

    /* rx_buf 系列, params, 基準 z_hat / sym_td */
    int buflen=3*sym_len;
    float *rb=rd("extract_rxbuf.f32",num_calls*buflen*2); if(!rb)return 1;
    float *pp=rd("extract_params.f32",num_calls*2); if(!pp)return 1;
    float *zref=rd("extract_zhat.f32",num_calls*ld); if(!zref)return 1;
    float *stref=rd("extract_symtd.f32",num_calls*M*2); if(!stref)return 1;

    /* state 初期化 */
    rade_extract_v2_state st;
    RADE_COMP *rx_phase_vec=(RADE_COMP*)malloc(sizeof(RADE_COMP)*sym_len);
    RADE_COMP *rx_i=(RADE_COMP*)malloc(sizeof(RADE_COMP)*Ns*sym_len);
    RADE_COMP *rx_sym_td=(RADE_COMP*)malloc(sizeof(RADE_COMP)*M);
    rext_init(&st,M,Ncp,Ns,Nc,ld,Fs,Wfwd,rx_phase_vec,rx_i,rx_sym_td);

    RADE_COMP *rxbuf=(RADE_COMP*)malloc(sizeof(RADE_COMP)*buflen);
    float *zhat=(float*)malloc(sizeof(float)*ld);
    int fail=0; float max_rel=0.0f;

    for(int call=0;call<num_calls;call++){
        const float *b=&rb[call*buflen*2];
        for(int i=0;i<buflen;i++){rxbuf[i].real=b[2*i];rxbuf[i].imag=b[2*i+1];}
        float delta_hat=pp[call*2], freq_offset=pp[call*2+1];

        rext_extract(&st,rxbuf,delta_hat,freq_offset,zhat);

        /* z_hat 照合 */
        const float *zw=&zref[call*ld];
        for(int i=0;i<ld;i++){
            float rel=fabsf(zhat[i]-zw[i])/(fabsf(zw[i])+1e-6f);
            if(rel>max_rel)max_rel=rel;
            if(rel>1e-3f && fabsf(zhat[i]-zw[i])>1e-4f){
                if(fail<10)printf("  [z不一致] call=%d i=%d C=%.6f Py=%.6f rel=%.3e\n",call,i,zhat[i],zw[i],rel);
                fail++;
            }
        }
        /* rx_sym_td 照合 */
        const float *sw=&stref[call*M*2];
        for(int i=0;i<M;i++){
            float cr=st.rx_sym_td[i].real, ci=st.rx_sym_td[i].imag;
            float pr=sw[2*i], pi=sw[2*i+1];
            float rr=fabsf(cr-pr)/(fabsf(pr)+1e-6f), ri=fabsf(ci-pi)/(fabsf(pi)+1e-6f);
            if(rr>max_rel)max_rel=rr; if(ri>max_rel)max_rel=ri;
            if((rr>1e-3f&&fabsf(cr-pr)>1e-4f)||(ri>1e-3f&&fabsf(ci-pi)>1e-4f)){
                if(fail<10)printf("  [symtd不一致] call=%d i=%d C=(%.5f,%.5f) Py=(%.5f,%.5f)\n",call,i,cr,ci,pr,pi);
                fail++;
            }
        }
        printf("  call=%d delta_hat=%.1f freq=%.1f z_hat[:3]=[%.5f %.5f %.5f]\n",
               call,delta_hat,freq_offset,zhat[0],zhat[1],zhat[2]);
    }

    printf("最大相対誤差 = %.3e\n",max_rel);
    if(fail==0){printf("=== extract C=Python 一致 成功 ===\n");return 0;}
    else{printf("=== 不一致 %d 箇所 ===\n",fail);return 2;}
}
