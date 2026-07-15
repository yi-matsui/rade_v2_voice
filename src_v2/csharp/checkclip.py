import numpy as np

def check_clip(path):
    d = np.fromfile(path, dtype=np.int16)
    print(f"--- {path} ---")
    print(f"サンプル数: {len(d)}")
    print(f"最大絶対値: {np.abs(d).max()} / 32767")
    n_clip = np.sum(np.abs(d) >= 32760)
    print(f"クリップ相当サンプル(|x|>=32760): {n_clip} ({100*n_clip/len(d):.3f}%)")
    # ヒストグラム的に、振幅帯ごとの分布も見ておく
    for th in [32760, 30000, 25000, 20000]:
        n = np.sum(np.abs(d) >= th)
        print(f"  |x|>={th}: {n} 個 ({100*n/len(d):.3f}%)")
    print()

# C#/DLL版
check_clip("out.s16")

# Python版のs16があれば、ファイル名を合わせてコメントアウトを外して実行
# check_clip("out_python.s16")
