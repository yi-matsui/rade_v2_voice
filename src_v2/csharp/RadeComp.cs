using System.Runtime.InteropServices;

namespace RadeV2
{
    /// <summary>
    /// RADE V2 の複素数型。C 側の RADE_COMP { float real; float imag; } と
    /// 完全に同一メモリレイアウト(8バイト、real,imag の順)。
    /// P/Invoke でこの構造体の配列をそのままマーシャリングできる。
    /// </summary>
    [StructLayout(LayoutKind.Sequential)]
    public struct RadeComp
    {
        public float Real;
        public float Imag;

        public RadeComp(float real, float imag)
        {
            Real = real;
            Imag = imag;
        }

        public override string ToString() => $"({Real:F5}, {Imag:F5})";
    }
}
