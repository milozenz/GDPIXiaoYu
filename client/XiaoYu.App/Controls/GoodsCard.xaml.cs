using System.Windows;
using System.Windows.Controls;
using System.Windows.Input;
using XiaoYu.App.Services;
using XiaoYu.Core.Models;

namespace XiaoYu.App.Controls;

public partial class GoodsCard : UserControl
{
    public GoodsCard()
    {
        InitializeComponent();
    }

    // 卡片宽度跟着网格变：封面保持接近正方形，圆角裁剪跟着变宽（只圆上面两个角）
    private void OnSizeChanged(object sender, SizeChangedEventArgs e)
    {
        double w = Math.Max(0, ActualWidth - 2);    // 去掉左右各 1 的边框
        Cover.Height = Math.Round(w * 212 / 214);
        CoverClip.Rect = new System.Windows.Rect(0, 0, w, Cover.Height + 48);
    }

    private void OnClick(object sender, MouseButtonEventArgs e)
    {
        if (DataContext is GoodsBrief goods) Navigator.ToGoodsDetail(goods.GoodsId);
    }
}
