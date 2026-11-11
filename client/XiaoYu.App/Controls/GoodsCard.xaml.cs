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

    private void OnClick(object sender, MouseButtonEventArgs e)
    {
        if (DataContext is GoodsBrief goods) Navigator.ToGoodsDetail(goods.GoodsId);
    }
}
