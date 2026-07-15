/*---------------------------------------------------------------------------*\

  test_eoo_v2.c

  rade_eoo_v2.c(_detect_eoo 移植)が Python(gen_eoo_ref.py)と一致するか検証。

  入力:
    eoo_pend.f32  ... model.pend (re,im 交互, 長さ M)
    eoo_in.f32    ... rx_sym_td 系列 (re,im 交互, num_frames*M)
    eoo_ref.f32   ... [eoo_corr, eoo_smooth, detected] × num_frames
    eoo_meta.txt   … M, Ncp, num_frames

\*---------------------------------------------------------------------------*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "rade_eoo_v2.h"

#define NCOL 3

static int read_meta(const char *path, int *M, int *Ncp, int *num_frames)
{
    FILE *f = fopen(path, "r");
    if (!f) { fprintf(stderr, "metaを開けません: %s\n", path); return -1; }
    char line[256];
    while (fgets(line, sizeof(line), f)) {
        char *eq = strchr(line, '='); if (!eq) continue; *eq = 0;
        float v = (float)atof(eq+1);
        if      (!strcmp(line,"M"))          *M = (int)v;
        else if (!strcmp(line,"Ncp"))        *Ncp = (int)v;
        else if (!strcmp(line,"num_frames")) *num_frames = (int)v;
    }
    fclose(f);
    return 0;
}

static float* read_f32(const char *path, int n)
{
    float *buf = (float*)malloc(sizeof(float)*n);
    FILE *f = fopen(path, "rb");
    if (!f) { fprintf(stderr,"開けません: %s\n", path); free(buf); return NULL; }
    if ((int)fread(buf, sizeof(float), n, f) != n) {
        fprintf(stderr,"%s: サイズ不足\n", path); fclose(f); free(buf); return NULL;
    }
    fclose(f);
    return buf;
}

int main(void)
{
    int M=0, Ncp=0, num_frames=0;
    if (read_meta("eoo_meta.txt", &M, &Ncp, &num_frames)) return 1;
    printf("meta: M=%d Ncp=%d num_frames=%d\n", M, Ncp, num_frames);

    /* pend 読み込み(RADE_COMP 配列に) */
    float *pend_il = read_f32("eoo_pend.f32", M*2);
    if (!pend_il) return 1;
    RADE_COMP *pend = (RADE_COMP*)malloc(sizeof(RADE_COMP)*M);
    for (int i=0;i<M;i++){ pend[i].real=pend_il[2*i]; pend[i].imag=pend_il[2*i+1]; }

    /* 入力系列 */
    float *in_il = read_f32("eoo_in.f32", num_frames*M*2);
    if (!in_il) return 1;

    /* 基準 */
    float *ref = read_f32("eoo_ref.f32", num_frames*NCOL);
    if (!ref) return 1;

    /* init */
    rade_eoo_v2_state st;
    int rc = reoo_init(&st, M, Ncp, pend);
    if (rc != 0) { fprintf(stderr,"reoo_init 失敗 rc=%d\n", rc); return 1; }

    RADE_COMP *rx = (RADE_COMP*)malloc(sizeof(RADE_COMP)*M);
    int fail = 0;
    float max_rel = 0.0f;

    for (int f=0; f<num_frames; f++){
        const float *fin = &in_il[f*M*2];
        for (int i=0;i<M;i++){ rx[i].real=fin[2*i]; rx[i].imag=fin[2*i+1]; }

        int det = reoo_detect(&st, rx);

        float got[NCOL] = { st.eoo_corr, st.eoo_smooth, (float)det };
        const float *want = &ref[f*NCOL];

        for (int c=0;c<NCOL;c++){
            float a=got[c], b=want[c];
            float rel = fabsf(a-b)/(fabsf(b)+1e-9f);
            if (rel > max_rel) max_rel = rel;
            int is_flag = (c==2);
            int bad = is_flag ? (fabsf(a-b) > 0.5f) : (rel > 1e-3f && fabsf(a-b) > 1e-5f);
            if (bad){
                printf("  [不一致] f=%d col=%d C=%.6f Py=%.6f rel=%.3e\n", f, c, a, b, rel);
                fail++;
            }
        }
        printf("  f=%2d corr=%.5f smooth=%.5f det=%d\n", f, st.eoo_corr, st.eoo_smooth, det);
    }

    reoo_free(&st);
    printf("最大相対誤差 = %.3e\n", max_rel);
    if (fail==0){ printf("=== EOO検出 C=Python 一致 成功 ===\n"); return 0; }
    else        { printf("=== 不一致 %d 箇所 ===\n", fail); return 2; }
}
