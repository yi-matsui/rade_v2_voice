/*---------------------------------------------------------------------------*\

  rade_v2_decode_file.c

  rx_en.f32(interleaved IQ)を rade_api_v2 の受信チェーンに流し、
  decoder 出力 features を 36次元/フレーム で書き出すオフラインドライバ。
  Python 側(rx2.py / run_rx2_det.py)の出力と直接比較するために使う。

  出力形式: 1フレーム = float32 × 36
    [0..20]  : decoder が出す feature_dim=21 要素
    [21..35] : ゼロパディング(lpcnet_demo / plot_pitch_diff.py が期待する形式)
  1回の rade_v2_rx() が has_features=1 を返すと frames_per_step=4 フレーム分
  出力される(84要素 = 4 × 21)。

  条件合わせ用のスイッチ(Python 側の引数と対応):
    --no-bpf          <-> rx2.py --no_bpf
    --no-limit-pitch  <-> rx2.py --nolimit_pitch
    --no-timing-adj   <-> rx2.py --timing_adj_at 999999
    --no-agc          <-> rx2.py で --agc を付けない(rx2.py既定はAGC OFF)
    --legacy-toff     <-> time_offset=0, correct_time_offset=0 (旧動作)
  既定は C の既定(= 本家 rx2.py 既定)= BPF ON / limit_pitch ON /
  timing_adj ON / AGC ON / time_offset -16,-8。

  使い方:
    rade_v2_decode_file.exe rx_en.f32 features_hat36_en.f32 [オプション]

  Step5 初回(Python 側を --no_bpf --nolimit_pitch --timing_adj_at 999999
  で走らせた場合)の対応コマンド:
    rade_v2_decode_file.exe rx_en.f32 features_hat36_en.f32 ^
        --no-bpf --no-limit-pitch --no-timing-adj

\*---------------------------------------------------------------------------*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "rade_api_v2.h"

#define FEATURE_DIM      21
#define FRAMES_PER_STEP  4
#define OUT_DIM          36   /* 21 + 15 ゼロパディング */

int main(int argc, char **argv)
{
    const char *in_path = NULL, *out_path = NULL;
    int use_bpf = 1, use_limit_pitch = 1, use_timing_adj = 1, use_agc = 1;
    int legacy_toff = 0;
    int i;

    FILE *fin, *fout;
    long fsize;
    int total_samples;
    float *iq_il = NULL;
    RADE_COMP *iq = NULL;
    RADEV2Context *ctx = NULL;
    float *features = NULL;
    float out_frame[OUT_DIM];

    int prx = 0, nin, sym_count = 0, frames_written = 0;
    int sync_syms = 0, first_sync_sym = -1, first_feat_sym = -1;
    int prev_state = 0, idle_transitions = 0;

    /* --- 引数解析 --- */
    for (i = 1; i < argc; i++) {
        if      (!strcmp(argv[i], "--no-bpf"))         use_bpf = 0;
        else if (!strcmp(argv[i], "--no-limit-pitch")) use_limit_pitch = 0;
        else if (!strcmp(argv[i], "--no-timing-adj"))  use_timing_adj = 0;
        else if (!strcmp(argv[i], "--no-agc"))         use_agc = 0;
        else if (!strcmp(argv[i], "--legacy-toff"))    legacy_toff = 1;
        else if (argv[i][0] == '-') {
            fprintf(stderr, "不明なオプション: %s\n", argv[i]);
            return 1;
        }
        else if (!in_path)  in_path  = argv[i];
        else if (!out_path) out_path = argv[i];
    }
    if (!in_path || !out_path) {
        fprintf(stderr,
            "usage: rade_v2_decode_file <rx.f32> <features_hat36.f32> [options]\n"
            "  --no-bpf --no-limit-pitch --no-timing-adj --no-agc --legacy-toff\n");
        return 1;
    }

    /* --- IQ 読み込み(interleaved float32: re,im,re,im,...) --- */
    fin = fopen(in_path, "rb");
    if (!fin) { fprintf(stderr, "開けません: %s\n", in_path); return 1; }
    fseek(fin, 0, SEEK_END); fsize = ftell(fin); fseek(fin, 0, SEEK_SET);
    total_samples = (int)(fsize / (long)(2 * sizeof(float)));
    if (total_samples <= 0) { fprintf(stderr, "入力が空\n"); fclose(fin); return 1; }

    iq_il = (float*)malloc(sizeof(float) * 2 * (size_t)total_samples);
    iq    = (RADE_COMP*)malloc(sizeof(RADE_COMP) * (size_t)total_samples);
    if (!iq_il || !iq) { fprintf(stderr, "malloc失敗\n"); fclose(fin); return 1; }
    if ((int)fread(iq_il, sizeof(float) * 2, (size_t)total_samples, fin) != total_samples) {
        fprintf(stderr, "読み込み失敗\n"); fclose(fin); return 1;
    }
    fclose(fin);
    for (i = 0; i < total_samples; i++) {
        iq[i].real = iq_il[2*i];
        iq[i].imag = iq_il[2*i + 1];
    }
    printf("入力: %s  %d サンプル (%.2f 秒 @8kHz)\n",
           in_path, total_samples, (double)total_samples / 8000.0);

    /* --- コンテキスト生成 + 条件設定 --- */
    ctx = rade_v2_open();
    if (!ctx) { fprintf(stderr, "rade_v2_open 失敗\n"); return 1; }

    rade_v2_set_bpf(ctx, use_bpf);
    rade_v2_set_limit_pitch(ctx, use_limit_pitch);
    rade_v2_set_timing_adj(ctx, use_timing_adj);
    rade_v2_set_agc(ctx, use_agc);
    if (legacy_toff) rade_v2_set_time_offset(ctx, 0, 0);

    printf("設定: bpf=%d limit_pitch=%d timing_adj=%d agc=%d time_offset=%s\n",
           use_bpf, use_limit_pitch, use_timing_adj, use_agc,
           legacy_toff ? "0,0 (legacy)" : "-16,-8 (本家既定)");

    features = (float*)malloc(sizeof(float) * (size_t)rade_v2_n_features_out(ctx));
    if (!features) { fprintf(stderr, "malloc失敗\n"); return 1; }

    fout = fopen(out_path, "wb");
    if (!fout) { fprintf(stderr, "書けません: %s\n", out_path); return 1; }

    /* --- 受信ループ --- */
    nin = rade_v2_sym_len(ctx);
    memset(out_frame, 0, sizeof(out_frame));   /* [21..35] は常に 0 */

    while (prx + nin <= total_samples) {
        int have = 0, sig_det = 0, sine_det = 0;
        int advance = nin;
        int state = rade_v2_rx(ctx, &iq[prx], &nin, features,
                               &have, &sig_det, &sine_det);
        prx += advance;
        sym_count++;

        if (state) {
            sync_syms++;
            if (first_sync_sym < 0) first_sync_sym = sym_count - 1;
        } else if (prev_state) {
            idle_transitions++;   /* sync -> idle への落ち(EOO 検出など) */
        }
        prev_state = state;

        if (have) {
            int f, k;
            if (first_feat_sym < 0) first_feat_sym = sym_count - 1;
            for (f = 0; f < FRAMES_PER_STEP; f++) {
                for (k = 0; k < FEATURE_DIM; k++)
                    out_frame[k] = features[f * FEATURE_DIM + k];
                fwrite(out_frame, sizeof(float), OUT_DIM, fout);
                frames_written++;
            }
        }
    }
    fclose(fout);

    printf("処理シンボル数: %d  sync状態: %d  sync->idle遷移: %d\n",
           sym_count, sync_syms, idle_transitions);
    printf("最初にsync入り: s=%d  最初のfeatures: s=%d\n",
           first_sync_sym, first_feat_sym);
    printf("出力: %s  %d フレーム (%.2f 秒 @100fps)\n",
           out_path, frames_written, (double)frames_written / 100.0);

    rade_v2_close(ctx);
    free(features); free(iq); free(iq_il);
    return 0;
}
