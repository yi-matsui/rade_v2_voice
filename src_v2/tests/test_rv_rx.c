/*---------------------------------------------------------------------------*\

  test_rv_rx.c

  rade_v2_voice.dll の rv_rx_* を直接叩く動作確認ドライバ。
  実信号 f32(8kHz)を rv_rx_push_modem に流し込み、
    - いつ sync するか(rv_rx_synced)
    - speech16k が実際に出るか、その量
  を確認する。DLL は静的リンクせず実行時ロード(GetProcAddress)ではなく、
  ビルド時に rade_v2_voice.lib をリンクする単純な形にする
  (rade_v2_voice.dll のビルドで生成される .lib を使う)。

  使い方:
    test_rv_rx.exe real_en.f32 speech_out.f32

\*---------------------------------------------------------------------------*/

#include <stdio.h>
#include <stdlib.h>

/* rv_* の宣言(rade_voice.dll と同じシグネチャ。ヘッダが無いので直接宣言) */
#ifdef _WIN32
#define RV_IMPORT __declspec(dllimport)
#else
#define RV_IMPORT
#endif

RV_IMPORT void*  rv_rx_create(int v2);
RV_IMPORT void   rv_rx_destroy(void* h);
RV_IMPORT void   rv_rx_set_gain(void* h, float gain);
RV_IMPORT int    rv_rx_push_modem(void* h, float* modem8k, int n8, float* speech16kOut, int maxOut);
RV_IMPORT int    rv_rx_synced(void* h);
RV_IMPORT int    rv_rx_snr_db(void* h);
RV_IMPORT float  rv_rx_freq_offset(void* h);

#define BLOCK_IN   160     /* 8kHz、1ブロックの入力サンプル数(任意) */
#define MAX_OUT    4096    /* 1回のpush_modemで出うる最大speechサンプル数 */

int main(int argc, char **argv)
{
    FILE *fin, *fout;
    void *h;
    float in_buf[BLOCK_IN];
    float out_buf[MAX_OUT];
    size_t nread;
    long total_in = 0, total_out = 0;
    int was_synced = 0;
    int sync_transitions = 0;
    int block_count = 0;

    int v2 = 1;   /* 既定はV2。第3引数で 0 を渡せばV1(2026-07-13追加) */

    if (argc < 3 || argc > 4) {
        fprintf(stderr, "usage: test_rv_rx <real_in.f32> <speech_out.f32> [v2=1|0]\n");
        return 1;
    }
    if (argc == 4) v2 = atoi(argv[3]);

    fin = fopen(argv[1], "rb");
    if (!fin) { fprintf(stderr, "開けません: %s\n", argv[1]); return 1; }
    fout = fopen(argv[2], "wb");
    if (!fout) { fprintf(stderr, "書けません: %s\n", argv[2]); fclose(fin); return 1; }

    h = rv_rx_create(v2);
    if (!h) {
        fprintf(stderr, "rv_rx_create(%d) 失敗(%s)\n", v2, v2 ? "V2" : "V1");
        fclose(fin); fclose(fout);
        return 1;
    }
    fprintf(stderr, "rv_rx_create(%d) 成功(%s)\n", v2, v2 ? "V2" : "V1");
    rv_rx_set_gain(h, 1.0f);

    while ((nread = fread(in_buf, sizeof(float), BLOCK_IN, fin)) == BLOCK_IN) {
        int n_out = rv_rx_push_modem(h, in_buf, BLOCK_IN, out_buf, MAX_OUT);
        int synced = rv_rx_synced(h);

        block_count++;
        total_in += BLOCK_IN;

        if (n_out > 0) {
            fwrite(out_buf, sizeof(float), n_out, fout);
            total_out += n_out;
        }
        if (synced != was_synced) {
            sync_transitions++;
            fprintf(stderr, "block %d (t=%.2fs): sync %d -> %d\n",
                    block_count, (double)total_in / 8000.0, was_synced, synced);
            was_synced = synced;
        }
    }

    fprintf(stderr, "\n=== 結果 ===\n");
    fprintf(stderr, "入力ブロック数: %d (%.2fs @8kHz)\n", block_count, (double)total_in / 8000.0);
    fprintf(stderr, "出力音声サンプル数: %ld (%.2fs @16kHz)\n", total_out, (double)total_out / 16000.0);
    fprintf(stderr, "sync遷移回数: %d\n", sync_transitions);
    fprintf(stderr, "最終sync状態: %d\n", rv_rx_synced(h));
    fprintf(stderr, "snr_db: %d  freq_offset: %.1f (V2は現状未実装のため0のはず)\n",
            rv_rx_snr_db(h), rv_rx_freq_offset(h));

    rv_rx_destroy(h);
    fclose(fin);
    fclose(fout);
    return 0;
}
