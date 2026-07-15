/*---------------------------------------------------------------------------*\

  rade_bpf_v2.h

  受信入力用 複素バンドパスフィルタ(complex BPF)。
  radae/dsp.py の class complex_bpf をメソッド単位で対応させて移植。
  本家 rx2.py は既定でこのBPFを受信ストリーム全体に適用する(--no_bpf で無効化)。

  処理: 中心周波数へミックスダウン -> 実係数対称sinc LPF -> ミックスアップ。
  ブロック呼び出し間で filter memory と mixer phase を引き継ぐため、
  ストリーム全体を一括フィルタする本家と数値等価(gen_bpf_ref.py で検証)。

  注意(本家との位相計算の対応):
  - 本家は phase_vec_exp = exp(-1j*alpha*arange(1,max_len+1)) を事前計算し、
    ブロック先頭位相 phase との積で phase_vec を得る(サンプル毎の累積乗算ではない)。
  - 本実装もこれに合わせ、ブロック内は角度を double で直接計算して
    exp を評価する。累積乗算方式(1サンプル毎に e^-jαを掛け続ける)は
    float では長時間運用で振幅ドリフトを起こすため採用しない。

\*---------------------------------------------------------------------------*/

#ifndef RADE_BPF_V2_H
#define RADE_BPF_V2_H

#include "rade_v2_comp.h"

/* 本家 rx2.py: Ntap=101 固定。将来変更に備え上限として定義 */
#define RADE_BPF_V2_NTAP_MAX   101

/* 1回の process 呼び出しの最大サンプル数。
   V2 の nin は sym_len(160) ± sym_len/4(40) = 最大200。余裕を持って256 */
#define RADE_BPF_V2_MAXLEN     256

typedef struct {
    int    ntap;                                   /* タップ数(奇数、通常101) */
    float  alpha;                                  /* 2*pi*centre/Fs */
    float  h[RADE_BPF_V2_NTAP_MAX];                /* 実係数対称sinc LPF(doubleで計算しfloat格納) */
    RADE_COMP mem[RADE_BPF_V2_NTAP_MAX - 1];       /* ベースバンドフィルタ履歴(Ntap-1) */
    RADE_COMP phase;                               /* ミキサ位相(ブロック間引き継ぎ) */
    /* 作業領域: 履歴 + 1ブロック分のベースバンドサンプル */
    RADE_COMP win[(RADE_BPF_V2_NTAP_MAX - 1) + RADE_BPF_V2_MAXLEN];
} rade_bpf_v2_state;

/* __init__ 対応。ntap は奇数(本家assert相当は呼び出し側責務) */
void rbpf_init(rade_bpf_v2_state *st, int ntap, float fs_hz,
               float bandwidth_hz, float centre_hz);

/* mem/phase を初期状態に戻す(re-sync等でストリームが切れた時用) */
void rbpf_reset(rade_bpf_v2_state *st);

/* bpf() 対応。in の n サンプル(n <= RADE_BPF_V2_MAXLEN)を out へ。
   in と out は同一バッファ可(in-place可) */
void rbpf_process(rade_bpf_v2_state *st, RADE_COMP *out,
                  const RADE_COMP *in, int n);

#endif /* RADE_BPF_V2_H */
