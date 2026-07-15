/*---------------------------------------------------------------------------*\

  test_rx_v2_dsp.c

  rade_rx_v2.c の DSP 部(gain/autocorr/detect/idle)が Python
  (gen_rxdsp_ref.py) と数値一致することを検証する。

  入力:
    rxdsp_in.f32   ... 合成 IQ (re,im 交互, complex64 相当)
    rxdsp_ref.f32  ... 各シンボル 8 値 × num_symbols
    rxdsp_meta.txt  … M,Ncp,Ns,Nc,Fs,sym_len,num_symbols,w_first,w_last

  各シンボルについて C 側で
    rrx_compute_gain -> rrx_update_rx_buf -> rrx_compute_autocorr
    -> rrx_detect_signal -> (idle なら) rrx_process_idle
  を回し、[gain, sig_det, sine_det, delta_hat_g, Ry_max, Ry_min,
           snr_est_dB, state] を Python と比較する。

\*---------------------------------------------------------------------------*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "rade_rx_v2.h"

#define NCOL 8   /* 1シンボルあたりの記録値数 */

static int read_meta(const char *path, int *M, int *Ncp, int *Ns, int *Nc,
                     float *Fs, int *sym_len, int *num_symbols,
                     float *w_first, float *w_last)
{
    FILE *f = fopen(path, "r");
    if (!f) { fprintf(stderr, "metaを開けません: %s\n", path); return -1; }
    char line[256];
    while (fgets(line, sizeof(line), f)) {
        char *eq = strchr(line, '=');
        if (!eq) continue;
        *eq = 0;
        const char *key = line;
        float v = (float)atof(eq + 1);
        if      (!strcmp(key,"M"))           *M = (int)v;
        else if (!strcmp(key,"Ncp"))         *Ncp = (int)v;
        else if (!strcmp(key,"Ns"))          *Ns = (int)v;
        else if (!strcmp(key,"Nc"))          *Nc = (int)v;
        else if (!strcmp(key,"Fs"))          *Fs = v;
        else if (!strcmp(key,"sym_len"))     *sym_len = (int)v;
        else if (!strcmp(key,"num_symbols")) *num_symbols = (int)v;
        else if (!strcmp(key,"w_first"))     *w_first = v;
        else if (!strcmp(key,"w_last"))      *w_last = v;
    }
    fclose(f);
    return 0;
}

int main(void)
{
    int M=0,Ncp=0,Ns=0,Nc=0,sym_len=0,num_symbols=0;
    float Fs=0.0f, w_first=0.0f, w_last=0.0f;

    if (read_meta("rxdsp_meta.txt", &M,&Ncp,&Ns,&Nc,&Fs,&sym_len,&num_symbols,
                  &w_first,&w_last)) return 1;

    printf("meta: M=%d Ncp=%d Ns=%d Nc=%d Fs=%.0f sym_len=%d num_symbols=%d\n",
           M,Ncp,Ns,Nc,Fs,sym_len,num_symbols);

    /* 入力 IQ 読み込み */
    int total = num_symbols * sym_len;
    RADE_COMP *iq = (RADE_COMP*)malloc(sizeof(RADE_COMP)*total);
    {
        float *tmp = (float*)malloc(sizeof(float)*total*2);
        FILE *f = fopen("rxdsp_in.f32","rb");
        if (!f) { fprintf(stderr,"rxdsp_in.f32を開けません\n"); return 1; }
        if ((int)fread(tmp, sizeof(float), total*2, f) != total*2) {
            fprintf(stderr,"rxdsp_in.f32: サイズ不足\n"); return 1;
        }
        fclose(f);
        for (int i=0;i<total;i++){ iq[i].real=tmp[2*i]; iq[i].imag=tmp[2*i+1]; }
        free(tmp);
    }

    /* 基準読み込み */
    float *ref = (float*)malloc(sizeof(float)*num_symbols*NCOL);
    {
        FILE *f = fopen("rxdsp_ref.f32","rb");
        if (!f) { fprintf(stderr,"rxdsp_ref.f32を開けません\n"); return 1; }
        if ((int)fread(ref, sizeof(float), num_symbols*NCOL, f) != num_symbols*NCOL) {
            fprintf(stderr,"rxdsp_ref.f32: サイズ不足\n"); return 1;
        }
        fclose(f);
    }

    /* 状態初期化 */
    rade_rx_v2_state st;
    RADE_COMP *rx_buf       = (RADE_COMP*)malloc(sizeof(RADE_COMP)*3*sym_len);
    RADE_COMP *rx_phase_vec = (RADE_COMP*)malloc(sizeof(RADE_COMP)*sym_len);
    RADE_COMP *Ry_norm      = (RADE_COMP*)malloc(sizeof(RADE_COMP)*sym_len);
    RADE_COMP *Ry_smooth    = (RADE_COMP*)malloc(sizeof(RADE_COMP)*sym_len);

    rrx_init(&st, M,Ncp,Ns,Nc,Fs, w_first,w_last, 1 /*agc*/,
             rx_buf, rx_phase_vec, Ry_norm, Ry_smooth);

    int fail = 0;
    float max_rel = 0.0f;
    int nin = sym_len;
    int prx = 0;

    for (int s=0; s<num_symbols; s++) {
        if (prx + nin > total) break;
        st.s += 1;
        const RADE_COMP *chunk = &iq[prx];
        prx += nin;

        float gain = rrx_compute_gain(&st, chunk, nin);
        rrx_update_rx_buf(&st, chunk, nin, gain);
        nin = sym_len;
        rrx_compute_autocorr(&st);
        int sig_det=0, sine_det=0;
        rrx_detect_signal(&st, &sig_det, &sine_det);

        int state_after;
        if (st.state == RRX_STATE_IDLE) {
            st.state = rrx_process_idle(&st, sig_det, sine_det);
        }
        state_after = (st.state == RRX_STATE_IDLE) ? 0 : 1;

        float got[NCOL] = {
            gain, (float)sig_det, (float)sine_det,
            (float)st.delta_hat_g, st.Ry_max, st.Ry_min,
            st.snr_est_dB, (float)state_after
        };
        const float *want = &ref[s*NCOL];

        for (int c=0;c<NCOL;c++){
            float a=got[c], b=want[c];
            float rel = fabsf(a-b)/(fabsf(b)+1e-9f);
            if (rel > max_rel) max_rel = rel;
            /* 判定: フラグ系(1,2,3,7列)は完全一致、連続値は相対誤差 */
            int is_flag = (c==1||c==2||c==3||c==7);
            int bad = is_flag ? (fabsf(a-b) > 0.5f) : (rel > 1e-3f && fabsf(a-b) > 1e-4f);
            if (bad) {
                printf("  [不一致] s=%d col=%d C=%.6f Py=%.6f rel=%.3e\n", s, c, a, b, rel);
                fail++;
            }
        }
    }

    printf("最大相対誤差(連続値含む) = %.3e\n", max_rel);
    if (fail==0) { printf("=== DSP部 C=Python 一致 成功 ===\n"); return 0; }
    else         { printf("=== 不一致 %d 箇所 ===\n", fail); return 2; }
}
