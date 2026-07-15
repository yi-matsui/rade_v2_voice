/*---------------------------------------------------------------------------*\

  test_tx_rx_loopback.c

  rade_v2_voice.dll の TX と RX を直結するループバックテスト。
  speech16k(入力) → rv_tx_push_speech → modem8k(実信号) →
  rv_rx_push_modem → speech16k(出力)
  という往復で、外部の「正解データ」無しに TX/RX 双方の疎通を確認する。

  入力には speech_out.f32(7/12 の rv_rx_push_modem 出力、既に聴感確認済みの
  16kHz音声)を再利用する。TXが正しく変調でき、RXがそれを復調して sync でき、
  音声が出れば、TX/RX 双方が connected な状態として機能している証拠になる。

  使い方:
    test_tx_rx_loopback.exe speech_out.f32 loopback_out.f32

\*---------------------------------------------------------------------------*/

#include <stdio.h>
#include <stdlib.h>

#ifdef _WIN32
#define RV_IMPORT __declspec(dllimport)
#else
#define RV_IMPORT
#endif

/* RX */
RV_IMPORT void*  rv_rx_create(int v2);
RV_IMPORT void   rv_rx_destroy(void* h);
RV_IMPORT void   rv_rx_set_gain(void* h, float gain);
RV_IMPORT int    rv_rx_push_modem(void* h, float* modem8k, int n8, float* speech16kOut, int maxOut);
RV_IMPORT int    rv_rx_synced(void* h);

/* TX */
RV_IMPORT void*  rv_tx_create(int v2);
RV_IMPORT void   rv_tx_destroy(void* h);
RV_IMPORT void   rv_tx_set_gain(void* h, float gain);
RV_IMPORT int    rv_tx_push_speech(void* h, float* speech16k, int n16, float* modem8kOut, int maxOut);
RV_IMPORT int    rv_tx_eoo(void* h, float* modem8kOut, int maxOut);

#define IN_BLOCK   320      /* 16kHz、1回のpush_speechに渡すサンプル数(任意) */
#define MODEM_MAX  4096     /* push_speechが1回で出しうる最大modem8kサンプル数 */
#define SPEECH_MAX 4096     /* push_modemが1回で出しうる最大speech16kサンプル数 */

int main(int argc, char **argv)
{
    FILE *fin, *fout;
    void *tx, *rx;
    float in_buf[IN_BLOCK];
    float modem_buf[MODEM_MAX];
    float speech_buf[SPEECH_MAX];
    size_t nread;
    long total_in = 0, total_modem = 0, total_speech_out = 0;
    int was_synced = 0, sync_transitions = 0, block_count = 0;

    if (argc != 3) {
        fprintf(stderr, "usage: test_tx_rx_loopback <speech16k_in.f32> <speech16k_out.f32>\n");
        return 1;
    }

    fin = fopen(argv[1], "rb");
    if (!fin) { fprintf(stderr, "開けません: %s\n", argv[1]); return 1; }
    fout = fopen(argv[2], "wb");
    if (!fout) { fprintf(stderr, "書けません: %s\n", argv[2]); fclose(fin); return 1; }

    tx = rv_tx_create(1);
    rx = rv_rx_create(1);
    if (!tx || !rx) {
        fprintf(stderr, "rv_tx_create/rv_rx_create 失敗 (tx=%p rx=%p)\n", tx, rx);
        if (tx) rv_tx_destroy(tx);
        if (rx) rv_rx_destroy(rx);
        fclose(fin); fclose(fout);
        return 1;
    }
    rv_tx_set_gain(tx, 1.0f);
    rv_rx_set_gain(rx, 1.0f);

    while ((nread = fread(in_buf, sizeof(float), IN_BLOCK, fin)) == IN_BLOCK) {
        int n_modem, n_speech, synced;

        block_count++;
        total_in += IN_BLOCK;

        /* --- TX: speech16k -> modem8k(実信号) --- */
        n_modem = rv_tx_push_speech(tx, in_buf, IN_BLOCK, modem_buf, MODEM_MAX);
        total_modem += n_modem;

        /* --- RX: modem8k -> speech16k(即座に同じブロックをRXへ) --- */
        if (n_modem > 0) {
            n_speech = rv_rx_push_modem(rx, modem_buf, n_modem, speech_buf, SPEECH_MAX);
            if (n_speech > 0) {
                fwrite(speech_buf, sizeof(float), n_speech, fout);
                total_speech_out += n_speech;
            }
        }

        synced = rv_rx_synced(rx);
        if (synced != was_synced) {
            sync_transitions++;
            fprintf(stderr, "block %d (t=%.2fs @16kHz in): sync %d -> %d\n",
                    block_count, (double)total_in / 16000.0, was_synced, synced);
            was_synced = synced;
        }
    }

    /* --- EOO も送ってみて、RXが受け取れるか確認(送信側の最終送出想定) --- */
    {
        float eoo_modem[4096];
        int n_eoo = rv_tx_eoo(tx, eoo_modem, 4096);
        if (n_eoo > 0) {
            int n_speech = rv_rx_push_modem(rx, eoo_modem, n_eoo, speech_buf, SPEECH_MAX);
            fprintf(stderr, "EOO: modem %d samples -> speech %d samples\n", n_eoo, n_speech);
            if (n_speech > 0) {
                fwrite(speech_buf, sizeof(float), n_speech, fout);
                total_speech_out += n_speech;
            }
        }
    }

    fprintf(stderr, "\n=== 結果 ===\n");
    fprintf(stderr, "入力speechブロック数: %d (%.2fs @16kHz)\n", block_count, (double)total_in / 16000.0);
    fprintf(stderr, "TX生成modemサンプル数: %ld (%.2fs @8kHz)\n", total_modem, (double)total_modem / 8000.0);
    fprintf(stderr, "RX出力speechサンプル数: %ld (%.2fs @16kHz)\n", total_speech_out, (double)total_speech_out / 16000.0);
    fprintf(stderr, "sync遷移回数: %d\n", sync_transitions);
    fprintf(stderr, "最終sync状態: %d\n", rv_rx_synced(rx));

    rv_tx_destroy(tx);
    rv_rx_destroy(rx);
    fclose(fin);
    fclose(fout);
    return 0;
}
