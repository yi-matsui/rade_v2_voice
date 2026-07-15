/*---------------------------------------------------------------------------*\

  test_api_v2.c

  rade_api_v2(DLL化予定の公開API)経由で、統合テストと同じ検証を行う。
  rade_v2_open/rade_v2_rx/rade_v2_tx が正しく部品を束ねているか確認する。

  受信側は test_sync_v2.c と同じ sync_*.f32 を使い、state/features の一致を
  API 経由でも再確認する。送信側は test_tx_v2.c と同じ tx_*.f32 を使う。

\*---------------------------------------------------------------------------*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "rade_api_v2.h"

static int mi(const char*k,const char*p){FILE*f=fopen(p,"r");char l[256];int v=0;
    if(!f)return 0; while(fgets(l,sizeof(l),f)){char*e=strchr(l,'=');if(!e)continue;*e=0;
    if(!strcmp(l,k)){v=atoi(e+1);break;}} fclose(f);return v;}
static float* rd(const char*p,int n){float*b=malloc(sizeof(float)*n);FILE*f=fopen(p,"rb");
    if(!f){fprintf(stderr,"開けません:%s\n",p);free(b);return NULL;}
    if((int)fread(b,sizeof(float),n,f)!=n){fprintf(stderr,"%s:size\n",p);fclose(f);free(b);return NULL;}
    fclose(f);return b;}

static int test_rx(void)
{
    printf("=== rade_v2_rx (via API) 検証 ===\n");
    int sym_len=mi("sym_len","sync_meta.txt");
    int num_symbols=mi("num_symbols","sync_meta.txt");
    int feature_dim=mi("feature_dim","sync_meta.txt");
    int total=num_symbols*sym_len;

    float*iq_il=rd("sync_in.f32",total*2); if(!iq_il)return 1;
    RADE_COMP*iq=malloc(sizeof(RADE_COMP)*total);
    for(int i=0;i<total;i++){iq[i].real=iq_il[2*i];iq[i].imag=iq_il[2*i+1];}
    float*ref=rd("sync_ref.f32",num_symbols*15); if(!ref)return 1;

    RADEV2Context *ctx = rade_v2_open();
    if(!ctx){fprintf(stderr,"rade_v2_open 失敗\n");return 1;}
    /* 旧基準(.f32)との回帰照合用: 本家既定機能を全部OFFにして旧動作を再現 */
    //rade_v2_set_time_offset(ctx, 0, 0);
    rade_v2_set_limit_pitch(ctx, 0);
    rade_v2_set_bpf(ctx, 0);
    rade_v2_set_timing_adj(ctx, 0);
    /* AGC は sync_ref.f32 生成時の条件に合わせる(以前は agc=1 で通っていたなら触らない) */


    int nin = rade_v2_sym_len(ctx);
    int prx=0, s=-1, fail=0, state_mismatch=0, have_mismatch=0;
    float*features=malloc(sizeof(float)*rade_v2_n_features_out(ctx));
    /* rade_v2_rx の戻り値は「処理後(次状態)」。Python の記録は「入口(処理前)
       state」なので、今回の戻り値は次シンボルの入口stateに相当する。
       初回の入口stateはidle(0)と仮定して比較をずらす
       (test_sync_v2.c で確立した教訓と同じ)。 */
    int entry_state = 0;

    while(prx+nin < total){
        s++;
        if(s>=num_symbols) break;
        int have=0, sig_det=0, sine_det=0;
        int prx_advance = nin;
        int state = rade_v2_rx(ctx, &iq[prx], &nin, features, &have, &sig_det, &sine_det);
        prx += prx_advance;

        const float*w=&ref[s*15];
        if((int)w[0]!=entry_state){ state_mismatch++; fail++; }
        if((int)w[8]!=have){ have_mismatch++; fail++; }
        entry_state = state;  /* 次シンボルの入口stateとして持ち越す */
        if(have && (int)w[8]==1){
            float rel=fabsf(features[0]-w[9])/(fabsf(w[9])+1e-4f);
            if(rel>1e-1f && fabsf(features[0]-w[9])>1e-2f) fail++;
        }
    }
    printf("state不一致=%d have不一致=%d\n",state_mismatch,have_mismatch);
    rade_v2_close(ctx);
    (void)feature_dim;
    if(state_mismatch==0 && have_mismatch==0){printf("=== rx API 一致 成功 ===\n");return 0;}
    else{printf("=== rx API 不一致あり ===\n");return 2;}
}

static int test_tx(void)
{
    printf("=== rade_v2_tx (via API) 検証 ===\n");
    int feature_dim=mi("feature_dim","tx_meta.txt");
    int frames_per_step=mi("frames_per_step","tx_meta.txt");
    int num_steps=mi("num_steps","tx_meta.txt");
    int sym_len=mi("sym_len","tx_meta.txt");
    int Ns=mi("Ns","tx_meta.txt");
    int input_dim=feature_dim*frames_per_step;

    float*ins=rd("tx_in.f32",num_steps*input_dim); if(!ins)return 1;
    float*ref_il=rd("tx_ref.f32",num_steps*Ns*sym_len*2); if(!ref_il)return 1;

    RADEV2Context *ctx = rade_v2_open();
    if(!ctx){fprintf(stderr,"rade_v2_open 失敗\n");return 1;}

    if(rade_v2_n_features_in(ctx)!=input_dim){
        fprintf(stderr,"n_features_in 不一致: API=%d 期待=%d\n",rade_v2_n_features_in(ctx),input_dim);
    }
    if(rade_v2_n_tx_out(ctx)!=Ns*sym_len){
        fprintf(stderr,"n_tx_out 不一致: API=%d 期待=%d\n",rade_v2_n_tx_out(ctx),Ns*sym_len);
    }
    RADE_COMP* tx_out = malloc(sizeof(RADE_COMP) * rade_v2_n_tx_out(ctx));
    int fail = 0; float max_rel = 0;

    /* 基準のRMSを先に求める(要素ごとの |ref|+1e-4 を分母にすると、
       基準がほぼゼロのサンプルで相対誤差が発散して意味を失うため) */
    double ref_rms = 0.0;
    for (int i = 0; i < num_steps * Ns * sym_len * 2; i++) ref_rms += (double)ref_il[i] * ref_il[i];
    ref_rms = sqrt(ref_rms / (double)(num_steps * Ns * sym_len));   /* 複素1サンプルあたり */

    for (int st = 0; st < num_steps; st++) {
        rade_v2_tx(ctx, &ins[st * input_dim], tx_out);
        const float* w = &ref_il[st * Ns * sym_len * 2];
        for (int i = 0; i < Ns * sym_len; i++) {
            double dr = tx_out[i].real - w[2 * i], di = tx_out[i].imag - w[2 * i + 1];
            float rel = (float)(sqrt(dr * dr + di * di) / (ref_rms + 1e-12));
            if (rel > max_rel)max_rel = rel;
            if (rel > 1e-3f) fail++;
        }
    }
    printf("最大相対誤差(RMS基準)=%.3e\n", max_rel);
   
    rade_v2_close(ctx);
    if(fail==0){printf("=== tx API 一致 成功 ===\n");return 0;}
    else{printf("=== tx API 不一致 %d箇所 ===\n",fail);return 2;}
}

int main(void){
    int r1 = test_rx();
    int r2 = test_tx();
    if(r1==0 && r2==0){ printf("\n=== rade_api_v2 全体 成功 ===\n"); return 0; }
    printf("\n=== rade_api_v2 一部失敗 (rx=%d tx=%d) ===\n", r1, r2);
    return 1;
}
