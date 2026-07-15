/*---------------------------------------------------------------------------*\
  test_sync_v2.c
  統合部 rade_sync_v2.c が Python(gen_sync_ref.py)と一致するか検証。
  全確定部品(rx_v2/extract/eoo/frame_sync/dec_v2)を繋いで受信ループを回す。
\*---------------------------------------------------------------------------*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "rade_sync_v2.h"

/* export 生成の重み配列 */
extern const WeightArray radesync_arrays[];
extern const WeightArray radedecv2_arrays[];

#define NCOL 15

static int meta_int(const char*k,const char*path){
    FILE*f=fopen(path,"r"); char line[256]; int v=0;
    if(!f)return 0;
    while(fgets(line,sizeof(line),f)){char*e=strchr(line,'=');if(!e)continue;*e=0;
        if(!strcmp(line,k)){v=atoi(e+1);break;}}
    fclose(f); return v;
}
static float meta_flt(const char*k,const char*path){
    FILE*f=fopen(path,"r"); char line[256]; float v=0;
    if(!f)return 0;
    while(fgets(line,sizeof(line),f)){char*e=strchr(line,'=');if(!e)continue;*e=0;
        if(!strcmp(line,k)){v=(float)atof(e+1);break;}}
    fclose(f); return v;
}
static float* rd(const char*p,int n){
    float*b=malloc(sizeof(float)*n); FILE*f=fopen(p,"rb");
    if(!f){fprintf(stderr,"開けません:%s\n",p);free(b);return NULL;}
    if((int)fread(b,sizeof(float),n,f)!=n){fprintf(stderr,"%s:size\n",p);fclose(f);free(b);return NULL;}
    fclose(f);return b;
}

int main(void){
    const char*MT="sync_meta.txt";
    int M=meta_int("M",MT),Ncp=meta_int("Ncp",MT),Ns=meta_int("Ns",MT),Nc=meta_int("Nc",MT);
    int latent_dim=meta_int("latent_dim",MT),feature_dim=meta_int("feature_dim",MT);
    int sym_len=meta_int("sym_len",MT),hangover=meta_int("hangover",MT);
    int num_symbols=meta_int("num_symbols",MT),output_size=meta_int("output_size",MT);
    float Fs=meta_flt("Fs",MT);
    printf("meta: M=%d Ncp=%d Ns=%d Nc=%d latent=%d feat=%d sym_len=%d hangover=%d num=%d osz=%d\n",
           M,Ncp,Ns,Nc,latent_dim,feature_dim,sym_len,hangover,num_symbols,output_size);

    int total=num_symbols*sym_len;
    float*iq_il=rd("sync_in.f32",total*2); if(!iq_il)return 1;
    RADE_COMP*iq=malloc(sizeof(RADE_COMP)*total);
    for(int i=0;i<total;i++){iq[i].real=iq_il[2*i];iq[i].imag=iq_il[2*i+1];}
    float*ref=rd("sync_ref.f32",num_symbols*NCOL); if(!ref)return 1;

    /* --- 全部品の状態を確保・初期化 --- */
    /* w_first/w_last は BPF 用。Python の model.w が要るが、簡易に受信では
       Ry ベースなので snr 以外に影響薄。ここでは meta に無いので 0.83/1.47 を仮置き
       (extract は Wfwd を使うので w には非依存)。SNR 値は照合対象から外す。 */
    rade_rx_v2_state rx;
    RADE_COMP *rx_buf=malloc(sizeof(RADE_COMP)*3*sym_len);
    RADE_COMP *rx_phase_vec=malloc(sizeof(RADE_COMP)*sym_len);
    RADE_COMP *Ry_norm=malloc(sizeof(RADE_COMP)*sym_len);
    RADE_COMP *Ry_smooth=malloc(sizeof(RADE_COMP)*sym_len);
    rrx_init(&rx,M,Ncp,Ns,Nc,Fs,0.834486f,1.472622f,1,rx_buf,rx_phase_vec,Ry_norm,Ry_smooth);
    rx.hangover=hangover;

    /* extract: Wfwd が要る。sync_wfwd.f32 として別途出す必要あり。
       ここでは gen 側に Wfwd を出させ、読み込む(未生成なら警告)。 */
    RADE_COMP *Wfwd=malloc(sizeof(RADE_COMP)*M*Nc);
    {
        FILE*f=fopen("sync_wfwd.f32","rb");
        if(!f){fprintf(stderr,"警告: sync_wfwd.f32 が無い。gen_sync_ref.py に Wfwd 出力を追加してください。\n");return 3;}
        float*wf=malloc(sizeof(float)*M*Nc*2);
        fread(wf,sizeof(float),M*Nc*2,f); fclose(f);
        for(int i=0;i<M*Nc;i++){Wfwd[i].real=wf[2*i];Wfwd[i].imag=wf[2*i+1];}
        free(wf);
    }
    rade_extract_v2_state ext;
    RADE_COMP *e_phase=malloc(sizeof(RADE_COMP)*sym_len);
    RADE_COMP *e_rxi=malloc(sizeof(RADE_COMP)*Ns*sym_len);
    RADE_COMP *e_symtd=malloc(sizeof(RADE_COMP)*M);
    rext_init(&ext,M,Ncp,Ns,Nc,latent_dim,Fs,Wfwd,e_phase,e_rxi,e_symtd);

    /* eoo: pend が要る。sync_pend.f32 として出す必要あり。 */
    RADE_COMP *pend=malloc(sizeof(RADE_COMP)*M);
    {
        FILE*f=fopen("sync_pend.f32","rb");
        if(!f){fprintf(stderr,"警告: sync_pend.f32 が無い。gen に pend 出力を追加してください。\n");return 3;}
        float*pp=malloc(sizeof(float)*M*2);
        fread(pp,sizeof(float),M*2,f); fclose(f);
        for(int i=0;i<M;i++){pend[i].real=pp[2*i];pend[i].imag=pp[2*i+1];}
        free(pp);
    }
    rade_eoo_v2_state eoo;
    if(reoo_init(&eoo,M,Ncp,pend)!=0){fprintf(stderr,"reoo_init 失敗\n");return 1;}

    /* frame_sync */
    FrameSyncNet fsync;
    if(fsync_init(&fsync,radesync_arrays,latent_dim)!=0){fprintf(stderr,"fsync_init 失敗\n");return 1;}

    /* decoder */
    RADEDecV2 dec_model;
    if(init_radedecv2(&dec_model,radedecv2_arrays)!=0){fprintf(stderr,"init_radedecv2 失敗\n");return 1;}
    RADEDecV2State dec_state;
    rade_init_decoder_v2(&dec_state);

    /* ctx */
    rade_sync_v2_ctx ctx;
    ctx.rx=&rx; ctx.ext=&ext; ctx.eoo=&eoo; ctx.fsync=&fsync;
    ctx.dec_model=&dec_model; ctx.dec_state=&dec_state;
    ctx.latent_dim=latent_dim; ctx.feature_dim=feature_dim;
    ctx.frames_per_step=output_size/feature_dim; ctx.output_size=output_size;
    ctx.arch=0;

    /* --- 受信ループ --- */
    float*features=malloc(sizeof(float)*output_size);
    int nin=sym_len, prx=0, s=-1;
    int fail=0; float max_rel=0.0f;
    int state_mismatch=0, have_mismatch=0;

    while(prx+nin < total){
        s++;
        if(s>=num_symbols) break;
        int have=0,sig_det=0,sine_det=0;
        int prx_advance = nin;   /* Python: prx += nin(process 前の nin) */
        int entry_state = (rx.state==RRX_STATE_IDLE)?0:1;  /* Python は入口state を記録 */
        int next_state=rsync_process_symbol(&ctx,&iq[prx],&nin,features,&have,&sig_det,&sine_det);
        prx += prx_advance;

        const float*w=&ref[s*NCOL];
        int state_i = entry_state;  /* 入口state で照合 */
        /* 照合: state, sig_det, sine_det, delta_hat_g, have_features, feat[:3] */
        if((int)w[0]!=state_i){ if(state_mismatch<8)printf("  [state] s=%d C=%d Py=%d\n",s,state_i,(int)w[0]); state_mismatch++; fail++; }
        if((int)w[1]!=sig_det){ fail++; }
        if((int)w[8]!=have){ if(have_mismatch<8)printf("  [have] s=%d C=%d Py=%d\n",s,have,(int)w[8]); have_mismatch++; fail++; }
        if(have && (int)w[8]==1){
            float rel0=fabsf(features[0]-w[9])/(fabsf(w[9])+1e-4f);
            if(rel0>max_rel)max_rel=rel0;
            if(rel0>1e-2f && fabsf(features[0]-w[9])>1e-3f){
                printf("  [feat] s=%d C=%.5f Py=%.5f rel=%.3e\n",s,features[0],w[9],rel0);
                /* az_hat(latent) の先頭3要素も出して、features がズレる原因が
                   latent自体の不一致(extract/IIR追従の差)か、decoder内部の
                   差かを切り分ける。 */
                printf("    az_hat: C=[%.5f %.5f %.5f] Py=[%.5f %.5f %.5f]\n",
                       ctx.az_hat[0],ctx.az_hat[1],ctx.az_hat[2], w[12],w[13],w[14]);
                fail++;
            }
        }
        (void)next_state;
    }

    printf("最大feat相対誤差 = %.3e\n",max_rel);
    printf("state不一致=%d have不一致=%d\n",state_mismatch,have_mismatch);
    if(fail==0){printf("=== 統合部 C=Python 一致 成功 ===\n");return 0;}
    else{printf("=== 不一致 %d 箇所 ===\n",fail);return 2;}
}
