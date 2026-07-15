/*---------------------------------------------------------------------------*\

  test_bpf_v2.c

  rade_bpf_v2 の検証。gen_bpf_ref.py が生成した bpf_in.f32 / bpf_ref.f32 を
  読み込み、C側は160サンプル(V2のsym_len)ずつのブロック処理で流して、
  「一括フィルタしたPython基準」と数値一致することを確認する。
  ブロック分割でも一致すれば、mem/phase の状態引き継ぎの正しさも同時に
  検証できる(本家rx2.pyは一括、実運用のCはブロック処理のため)。

  判定: 倍精度位相基準(bpf_ref64.f32)に対して最大相対誤差 < 1e-4 で成功。
  参考: 本家そのまま基準(bpf_ref.f32)との差も表示する。こちらは本家自身の
        complex64位相テーブル量子化により、ストリーム末尾で最大~1.4e-3程度
        の差が出るのが正常(C側がより正確な側)。詳細は gen_bpf_ref.py 冒頭。

\*---------------------------------------------------------------------------*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "rade_bpf_v2.h"

#define BLOCK 160   /* V2 sym_len と同じ粒度で流す */

static int read_meta(const char *path, int *N, int *Ntap,
                     float *Fs, float *bw, float *fc)
{
    char key[64];
    double val;
    FILE *f = fopen(path, "r");
    if (!f) return -1;
    while (fscanf(f, "%63s %lf", key, &val) == 2) {
        if (!strcmp(key, "N"))         *N    = (int)val;
        if (!strcmp(key, "Ntap"))      *Ntap = (int)val;
        if (!strcmp(key, "Fs"))        *Fs   = (float)val;
        if (!strcmp(key, "bandwidth")) *bw   = (float)val;
        if (!strcmp(key, "centre"))    *fc   = (float)val;
    }
    fclose(f);
    return 0;
}

int main(void)
{
    int N = 0, Ntap = 0;
    float Fs = 0.0f, bw = 0.0f, fc = 0.0f;
    RADE_COMP *in, *out, *ref, *ref64;
    rade_bpf_v2_state st;
    FILE *f;
    int i, pos;
    double max_rel = 0.0, max_rel64 = 0.0, ref_rms = 0.0;
    int max_i = 0, max_i64 = 0;

    if (read_meta("bpf_meta.txt", &N, &Ntap, &Fs, &bw, &fc)) {
        printf("bpf_meta.txt が読めない\n");
        return 1;
    }
    printf("N=%d Ntap=%d Fs=%.1f bw=%.3f centre=%.3f\n", N, Ntap, Fs, bw, fc);

    in  = (RADE_COMP *)malloc(sizeof(RADE_COMP) * (size_t)N);
    out = (RADE_COMP *)malloc(sizeof(RADE_COMP) * (size_t)N);
    ref = (RADE_COMP *)malloc(sizeof(RADE_COMP) * (size_t)N);
    ref64 = (RADE_COMP *)malloc(sizeof(RADE_COMP) * (size_t)N);
    if (!in || !out || !ref || !ref64) { printf("malloc失敗\n"); return 1; }

    f = fopen("bpf_in.f32", "rb");
    if (!f || fread(in, sizeof(RADE_COMP), (size_t)N, f) != (size_t)N) {
        printf("bpf_in.f32 が読めない\n"); return 1;
    }
    fclose(f);
    f = fopen("bpf_ref.f32", "rb");
    if (!f || fread(ref, sizeof(RADE_COMP), (size_t)N, f) != (size_t)N) {
        printf("bpf_ref.f32 が読めない\n"); return 1;
    }
    fclose(f);
    f = fopen("bpf_ref64.f32", "rb");
    if (!f || fread(ref64, sizeof(RADE_COMP), (size_t)N, f) != (size_t)N) {
        printf("bpf_ref64.f32 が読めない\n"); return 1;
    }
    fclose(f);

    rbpf_init(&st, Ntap, Fs, bw, fc);

    /* ブロック処理(実運用と同じ形態) */
    for (pos = 0; pos < N; pos += BLOCK) {
        int n = (N - pos < BLOCK) ? (N - pos) : BLOCK;
        rbpf_process(&st, &out[pos], &in[pos], n);
    }

    /* 相対誤差(基準RMSに対する複素差の絶対値) */
    for (i = 0; i < N; i++)
        ref_rms += (double)ref[i].real * ref[i].real
                 + (double)ref[i].imag * ref[i].imag;
    ref_rms = sqrt(ref_rms / (double)N);

    for (i = 0; i < N; i++) {
        double dr, di, d;
        dr = (double)out[i].real - ref[i].real;
        di = (double)out[i].imag - ref[i].imag;
        d  = sqrt(dr * dr + di * di) / (ref_rms + 1e-12);
        if (d > max_rel) { max_rel = d; max_i = i; }
        dr = (double)out[i].real - ref64[i].real;
        di = (double)out[i].imag - ref64[i].imag;
        d  = sqrt(dr * dr + di * di) / (ref_rms + 1e-12);
        if (d > max_rel64) { max_rel64 = d; max_i64 = i; }
    }

    printf("倍精度位相基準との最大相対誤差=%.3e (i=%d)  <- 判定対象\n", max_rel64, max_i64);
    printf("本家c64基準との最大相対誤差  =%.3e (i=%d)  <- 参考(~1.4e-3は正常)\n", max_rel, max_i);
    if (max_rel64 < 1e-4) {
        printf("=== bpf_v2 一致 成功 ===\n");
        free(in); free(out); free(ref); free(ref64);
        return 0;
    }
    printf("=== bpf_v2 不一致 ===\n");
    free(in); free(out); free(ref); free(ref64);
    return 1;
}
