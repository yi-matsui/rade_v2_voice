"""
check_same.py

out.wav と out2.wav が本当に同一ファイルか確認する。
(前回の穴埋め処理修正で「穴埋め0フレーム」だったため、理屈上は
 完全に同一のバイナリになるはず。念のため機械的に確認する)

使い方(csharpフォルダで):
    python check_same.py
"""
import hashlib
import os

def md5(path):
    if not os.path.exists(path):
        return None
    with open(path, "rb") as f:
        return hashlib.md5(f.read()).hexdigest()

def main():
    h1 = md5("out.wav")
    h2 = md5("out2.wav")

    if h1 is None:
        print("out.wav が見つかりません。カレントフォルダを確認してください。")
        return
    if h2 is None:
        print("out2.wav が見つかりません。カレントフォルダを確認してください。")
        return

    print(f"out.wav  の MD5: {h1}")
    print(f"out2.wav の MD5: {h2}")
    print()
    if h1 == h2:
        print("=== 結果: 2つのファイルは完全に同一です ===")
        print("(前回の穴埋め処理は発動しなかった=穴埋め0フレームだったので、")
        print(" これは理論通りの結果です)")
    else:
        print("=== 結果: 2つのファイルは異なります ===")
        print("(理論上は同一のはずなので、これは意外な結果です。")
        print(" 生成手順のどこかで違いが生じている可能性があります)")

if __name__ == "__main__":
    main()
