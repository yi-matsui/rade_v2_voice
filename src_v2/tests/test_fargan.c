/*---------------------------------------------------------------------------*\

  test_fargan.c

  rade_v2_decode_file が吐く features(36要素/フレーム)を FARGAN で
  16kHz 音声に合成する独立ドライバ。lpcnet_demo -fargan-synthesis の
  FARGAN 呼び出し部(183-210行)を忠実に移植したもの。

  目的: FARGAN 呼び出しが lpcnet_demo と完全に等価であることを確認する。
  検証: 出力を lpcnet_demo の出力(out_nolp.raw)と fc /b してバイト一致
        すれば、この呼び出し方が正しいと確定。
        → 将来の rade_v2_voice.dll の音声化部の土台になる。

  重み: weights_blob.bin(= model19_check3.bin, 2.45MB)を実行フォルダに置く。

  ★priming の作法(lpcnet_demo 191-195行を忠実に再現):
    - in_features は 5*NB_TOTAL_FEATURES の配列
    - 先頭5フレームを読む。各フレーム NB_TOTAL_FEATURES(36)個 fread するが、
      書き込み位置は NB_FEATURES(20)刻み。よって各フレームの後半16個は
      次フレーム先頭16個に上書きされ、最終的に「先頭20要素×5フレーム」が
      20刻みで詰まる(auxdata/パディングは捨てられる)。
    - この5フレームはファイルから消費される。合成ループは6フレーム目から。
    - 出力フレーム数 = 入力フレーム数 - 5。
      (検証: feat_nolp 4956フレーム → out_nolp 4951フレーム、差ちょうど5)

  features の並び(rade_v2 出力、36要素/フレーム):
    [0..17] ケプストラム, [18]ピッチ, [19]相関, [20]auxdata, [21..35]パディング
  FARGAN には先頭 NB_FEATURES(=NB_BANDS+2=20)個だけ渡す。

  使い方:
    copy ..\bin\model19_check3.bin weights_blob.bin
    test_fargan.exe feat_nolp.f32 test_fargan_out.raw
    fc /b test_fargan_out.raw out_nolp.raw   -> 相違なし が成功

\*---------------------------------------------------------------------------*/

#include <math.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
/* NB_FEATURES / NB_TOTAL_FEATURES は lpcnet.h、OPUS_COPY/MIN32/MAX32 は
   os_support.h / arch.h から来る。lpcnet_demo.c と同じ include 群を揃える。 */
#include "arch.h"
#include "lpcnet.h"
#include "freq.h"
#include "os_support.h"
#include "fargan.h"

/* weights_blob 読み込み(lpcnet_demo の load_blob fopen 版) */
static unsigned char *load_blob(const char *filename, int *len)
{
    FILE *file;
    unsigned char *data;
    file = fopen(filename, "rb");
    if (!file) { *len = 0; return NULL; }
    fseek(file, 0, SEEK_END);
    *len = (int)ftell(file);
    fseek(file, 0, SEEK_SET);
    if (*len <= 0) { fclose(file); *len = 0; return NULL; }
    data = (unsigned char *)malloc(*len);
    if (!data) { fclose(file); *len = 0; return NULL; }
    *len = (int)fread(data, 1, *len, file);
    fclose(file);
    return data;
}

int main(int argc, char **argv)
{
    FILE *fin, *fout;
    FARGANState fargan;
    size_t ret, i;
    int nframes = 0;
    /* lpcnet_demo と同じ: priming 用に 5 フレーム保持できる配列 */
    float in_features[5 * NB_TOTAL_FEATURES];
    float features[NB_FEATURES];
    float fpcm[FARGAN_FRAME_SIZE];
    opus_int16 pcm[FARGAN_FRAME_SIZE];
    float zeros[FARGAN_CONT_SAMPLES] = {0};

    if (argc != 3) {
        fprintf(stderr, "usage: test_fargan <features36.f32> <out16k.raw>\n");
        fprintf(stderr, "  weights_blob.bin (= model19_check3.bin) を同フォルダに置くこと\n");
        return 1;
    }

    /* FARGAN 重みは opus.lib 埋め込み(fargan_data.c)を使うため、
       weights_blob.bin の読み込みは不要。 */
    (void)load_blob;  /* 未使用警告の抑制(関数は将来の外部重み用に残す) */

    /* ★重要: lpcnet_demo は USE_WEIGHTS_FILE 未定義でビルドされており、
       fargan_load_model を呼んでいない(189行が #ifdef で除外)。
       FARGAN の重みは opus.lib に静的リンクされた埋め込み配列(fargan_data.c)
       を使う。したがってここでも load_model は呼ばず、fargan_init だけで
       埋め込み重みが有効になる。weights_blob.bin は不要。
       → 将来 rade_v2_voice.dll も opus.lib リンクだけで FARGAN が使え、
         重みの外部ファイル配布が不要(DLL1個で完結)。 */
    fargan_init(&fargan);

    fin = fopen(argv[1], "rb");
    if (!fin) { fprintf(stderr, "開けません: %s\n", argv[1]); return 1; }
    fout = fopen(argv[2], "wb");
    if (!fout) { fprintf(stderr, "書けません: %s\n", argv[2]); fclose(fin); return 1; }

    /* --- priming: 先頭5フレームで cont(lpcnet_demo 191-195行を忠実に) ---
       各フレーム NB_TOTAL_FEATURES 読むが書き込みは NB_FEATURES 刻み。 */
    for (i = 0; i < 5; i++) {
        ret = fread(&in_features[i * NB_FEATURES], sizeof(in_features[0]),
                    NB_TOTAL_FEATURES, fin);
        if (ret != NB_TOTAL_FEATURES) {
            fprintf(stderr, "エラー: priming に必要な5フレームが読めない(入力が短すぎる)\n");
            fclose(fin); fclose(fout); return 1;
        }
    }
    fargan_cont(&fargan, zeros, in_features);

    /* --- 合成ループ(lpcnet_demo 197-209行、6フレーム目以降) --- */
    while (1) {
        ret = fread(in_features, sizeof(features[0]), NB_TOTAL_FEATURES, fin);
        if (feof(fin) || ret != NB_TOTAL_FEATURES) break;
        OPUS_COPY(features, in_features, NB_FEATURES);   /* 先頭 NB_FEATURES だけ */
        fargan_synthesize(&fargan, fpcm, features);
        for (i = 0; i < FARGAN_FRAME_SIZE; i++)
            pcm[i] = (opus_int16)floor(.5 + MIN32(32767, MAX32(-32767, 32768.f * fpcm[i])));
        fwrite(pcm, sizeof(pcm[0]), FARGAN_FRAME_SIZE, fout);
        nframes++;
    }

    fprintf(stderr, "合成: %d フレーム (%.2f 秒 @16kHz)  ※priming 5フレーム消費\n",
            nframes, (double)nframes * FARGAN_FRAME_SIZE / 16000.0);

    fclose(fin);
    fclose(fout);
    return 0;
}
