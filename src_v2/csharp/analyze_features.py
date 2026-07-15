"""
analyze_features.py

features_hat36.f32(復調された特徴量, 36要素/フレーム, 10ms/フレーム)を
解析し、時間経過に伴う特徴量の変動(特にピッチ関連)を数値で確認する。

「0〜7.4秒でかさつき、それ以降安定」という聴覚的な観察が、実際に
特徴量データの乱れとして現れているかを客観的に見るのが目的。

LPCNet標準の36要素レイアウト(前提):
    0-17: ケプストラム係数(スペクトル包絡, 声色を決める)
    18  : ピッチ周期
    19  : ピッチ相関(0に近いほど無声音的、1に近いほど有声音的)
    20-35: LPC係数(帯域拡張用、今回は元音声から流用)

使い方(csharpフォルダで):
    python analyze_features.py features_hat36.f32
"""
import sys
import numpy as np

NB_TOTAL_FEATURES = 36
FRAME_MS = 10  # 1フレーム=10ms

def main():
    path = sys.argv[1] if len(sys.argv) > 1 else "features_hat36.f32"
    raw = np.fromfile(path, dtype=np.float32)
    n_frames = len(raw) // NB_TOTAL_FEATURES
    feat = raw[:n_frames*NB_TOTAL_FEATURES].reshape(n_frames, NB_TOTAL_FEATURES)

    print(f"入力: {path}")
    print(f"フレーム数: {n_frames} ({n_frames*FRAME_MS/1000:.2f}秒)")
    print()

    pitch_period = feat[:, 18]
    pitch_corr   = feat[:, 19]
    cepstrum_energy = np.sqrt(np.sum(feat[:, 0:18]**2, axis=1))  # ケプストラム全体のエネルギー(概形の目安)

    # --- 1. フレームごとの変化量(前フレームとの差)を見る ---
    # ピッチ周期が急にジャンプする箇所や、ケプストラムエネルギーが
    # 急変する箇所は、decoderの出力が不安定になっている可能性がある。
    pitch_period_delta = np.abs(np.diff(pitch_period))
    cepstrum_delta = np.abs(np.diff(cepstrum_energy))

    # --- 2. 0.5秒ごとの区間で、変化量の平均を集計 ---
    # これで「時間帯によって不安定さが違うか」を大まかに見る。
    bucket_frames = 50  # 50フレーム=0.5秒
    n_buckets = (n_frames - 1) // bucket_frames + 1
    print("時間帯別のフレーム間変化量(大きいほど不安定):")
    print(f"{'時間帯(秒)':>12} {'ピッチ周期変化(平均)':>18} {'ケプストラムエネルギー変化(平均)':>28}")
    for b in range(n_buckets):
        s = b * bucket_frames
        e = min(s + bucket_frames, len(pitch_period_delta))
        if s >= len(pitch_period_delta):
            break
        t_start = s * FRAME_MS / 1000
        t_end = e * FRAME_MS / 1000
        pp_avg = np.mean(pitch_period_delta[s:e]) if e > s else 0
        ce_avg = np.mean(cepstrum_delta[s:e]) if e > s else 0
        marker = "  <<<" if pp_avg > np.mean(pitch_period_delta) * 1.5 else ""
        print(f"{t_start:5.1f}-{t_end:5.1f} {pp_avg:18.3f} {ce_avg:28.3f}{marker}")

    print()
    print(f"全体平均: ピッチ周期変化={np.mean(pitch_period_delta):.3f}, "
          f"ケプストラムエネルギー変化={np.mean(cepstrum_delta):.3f}")
    print()

    # --- 3. 7.4秒(=740番目のフレーム)前後で平均を比較 ---
    split_frame = int(7.4 * 1000 / FRAME_MS)
    if split_frame < n_frames:
        before = pitch_period_delta[:split_frame] if split_frame < len(pitch_period_delta) else pitch_period_delta
        after = pitch_period_delta[split_frame:] if split_frame < len(pitch_period_delta) else np.array([0])
        ce_before = cepstrum_delta[:split_frame] if split_frame < len(cepstrum_delta) else cepstrum_delta
        ce_after = cepstrum_delta[split_frame:] if split_frame < len(cepstrum_delta) else np.array([0])

        print(f"=== 7.4秒(フレーム{split_frame})で前後比較 ===")
        print(f"0-7.4秒:    ピッチ周期変化平均={np.mean(before):.3f}  ケプストラムエネルギー変化平均={np.mean(ce_before):.3f}")
        print(f"7.4秒以降:  ピッチ周期変化平均={np.mean(after):.3f}  ケプストラムエネルギー変化平均={np.mean(ce_after):.3f}")
        ratio_pp = np.mean(before) / (np.mean(after) + 1e-6)
        ratio_ce = np.mean(ce_before) / (np.mean(ce_after) + 1e-6)
        print(f"比率(0-7.4秒 / 7.4秒以降): ピッチ={ratio_pp:.2f}倍  ケプストラム={ratio_ce:.2f}倍")
        print()
        if ratio_pp > 1.3 or ratio_ce > 1.3:
            print(">> 0-7.4秒の方が明確に不安定(数値でも裏付けられる可能性が高い)")
        elif ratio_pp < 0.77 or ratio_ce < 0.77:
            print(">> むしろ7.4秒以降の方が不安定(聴覚印象と食い違う可能性)")
        else:
            print(">> 前後で大きな差は見られない(聴覚的な印象と一致しない可能性)")

    # --- 4. 異常に大きいピッチジャンプの箇所を列挙(上位10件) ---
    print()
    print("ピッチ周期の変化が特に大きいフレーム(上位10件、時刻順ではなく大きさ順):")
    top_idx = np.argsort(pitch_period_delta)[::-1][:10]
    for idx in sorted(top_idx):
        t = idx * FRAME_MS / 1000
        print(f"  フレーム{idx:4d} ({t:5.2f}秒): 変化量={pitch_period_delta[idx]:.3f} "
              f"(値: {pitch_period[idx]:.2f} -> {pitch_period[idx+1]:.2f})")

if __name__ == "__main__":
    main()
