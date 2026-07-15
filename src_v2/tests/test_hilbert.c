/*---------------------------------------------------------------------------*\

  test_hilbert.c

  実信号(float f32)を 257タップ複素ヒルベルトフィルタで解析信号(複素IQ)に
  変換する単体テスト。ch.c(David Rowe)の 317-346行のヒルベルト変換部を
  忠実に移植したもの。将来 rade_v2_voice.dll の rv_rx_push_modem [1]段
  (実信号8k → 複素IQ)の実装リファレンスになる。

  ch.c との違い:
    - 入力を short(int16) ではなく float(f32) にした(量子化を避けクリーンに
      検証。rx_en.f32 の実部をそのまま入れられる)。gain は 1.0 固定。
    - チャネルシミュレート(ノイズ/フェージング/SSBフィルタ)は含まない。
      ヒルベルト変換の一段のみ。

  ch.c の構造(そのまま踏襲):
    htbuf[HT_N+BUF_N]: 前半HT_Nが過去履歴、後半BUF_Nが新規ブロック。
    新サンプルを j=HT_N から詰め、ch_in[i] = Σ htbuf[j-k]*ht_coeff[k]。
    ブロック後、末尾HT_Nサンプルを先頭へ繰り越し(ストリーム状態保持)。

  注意(ch.c コメント 324-325行より):
    real/imag フィルタは両方ユニティゲインなので、出力 ch_in は入力の
    2倍のパワーを持つ。復調側 AGC で吸収される想定。

  群遅延: (HT_N-1)/2 = 128 サンプル。出力先頭は128サンプル遅れる。

  使い方:
    test_hilbert.exe <real_in.f32> <iq_out.f32>
  検証フロー:
    1. rx_en.f32 の実部を real_en.f32 に取り出す(別スクリプト)
    2. test_hilbert.exe real_en.f32 iq_hb.f32
    3. rade_v2_decode_file iq_hb.f32 ... で sync するか確認
       → sync すれば「実信号→自作ヒルベルト→復調可能IQ」が成立、[1]確定

\*---------------------------------------------------------------------------*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ht_coeff.h は COMP 型(comp.h 由来)を「使うだけ」で自分では定義しない。
   したがって ht_coeff.h を include する前に COMP を定義しておく必要がある。
   (comp.h を include してもよいが、依存を減らすため自前定義にする) */
typedef struct { float real; float imag; } COMP;

#include "ht_coeff.h"   /* HT_N=257, COMP ht_coeff[HT_N]。上の COMP に依存 */

#define BUF_N 160       /* ch.c と同じブロック単位(RADE 1フレーム @8kHz) */

int main(int argc, char **argv)
{
    FILE *fin, *fout;
    float buf[BUF_N];
    float htbuf[HT_N + BUF_N];
    COMP  ch_in[BUF_N];
    int i, j, k;
    long total_in = 0, total_out = 0;
    size_t nread;

    if (argc != 3) {
        fprintf(stderr, "usage: test_hilbert <real_in.f32> <iq_out.f32>\n");
        return 1;
    }
    fin = fopen(argv[1], "rb");
    if (!fin) { fprintf(stderr, "開けません: %s\n", argv[1]); return 1; }
    fout = fopen(argv[2], "wb");
    if (!fout) { fprintf(stderr, "書けません: %s\n", argv[2]); fclose(fin); return 1; }

    /* ヒルベルトの状態バッファをゼロ初期化(ch.c 298-300行) */
    for (i = 0; i < HT_N; i++) htbuf[i] = 0.0f;

    /* sanity: ht_coeff の要素数が HT_N と一致(ch.c 297行) */
    /* (配列サイズはリンク時に決まるので実行時 assert は省略) */

    /* BUF_N サンプル単位でブロック処理(ch.c 317-346行) */
    while ((nread = fread(buf, sizeof(float), BUF_N, fin)) == BUF_N) {
        /* 新ブロックを htbuf[HT_N..] に詰めつつ畳み込み */
        for (i = 0, j = HT_N; i < BUF_N; i++, j++) {
            htbuf[j] = buf[i];          /* gain=1.0 */
            ch_in[i].real = 0.0f;
            ch_in[i].imag = 0.0f;
            for (k = 0; k < HT_N; k++) {
                ch_in[i].real += htbuf[j - k] * ht_coeff[k].real;
                ch_in[i].imag += htbuf[j - k] * ht_coeff[k].imag;
            }
        }
        /* 状態繰り越し: 末尾 HT_N サンプルを先頭へ(ch.c 345-346行) */
        for (i = 0; i < HT_N; i++)
            htbuf[i] = htbuf[i + BUF_N];

        /* 複素IQ を re,im,re,im... で書き出す */
        fwrite(ch_in, sizeof(COMP), BUF_N, fout);
        total_in += BUF_N;
        total_out += BUF_N;
    }

    /* 端数(BUF_N 未満)は ch.c 同様に捨てる(RADE は BUF_N 単位) */
    if (nread > 0)
        fprintf(stderr, "注意: 末尾 %zu サンプルは端数のため処理しませんでした\n", nread);

    fprintf(stderr, "入力 %ld サンプル → 出力 %ld 複素サンプル (%.2f 秒 @8kHz)\n",
            total_in, total_out, (double)total_out / 8000.0);
    fprintf(stderr, "群遅延 %d サンプル(出力先頭がその分遅れる)\n", (HT_N - 1) / 2);

    fclose(fin);
    fclose(fout);
    return 0;
}
