using System.Collections;
using System.Globalization;
using System.Windows;
using System.Windows.Data;
using System.Windows.Media;
using XiaoYu.Core.Utils;

namespace XiaoYu.App.Converters;

// 转换器：在 XAML 里把数据转成要显示的样子。已经在 Theme.xaml 里注册好了，直接用：
//   Text="{Binding Price, Converter={StaticResource Money}}"

/// <summary>只做单向转换的基类</summary>
public abstract class OneWayConverter : IValueConverter
{
    public abstract object? Convert(object? value);

    public object? Convert(object? value, Type targetType, object? parameter, CultureInfo culture) => Convert(value);

    public object? ConvertBack(object? value, Type targetType, object? parameter, CultureInfo culture) => Binding.DoNothing;

    protected static long ToLong(object? value) => value switch
    {
        long l => l,
        int i => i,
        double d => (long)d,
        _ => 0,
    };
}

public class MoneyConverter : OneWayConverter
{
    public override object Convert(object? value) => Fmt.Money(ToLong(value));
}

public class YuanConverter : OneWayConverter
{
    public override object Convert(object? value) => Fmt.Yuan(ToLong(value));
}

public class TimeAgoConverter : OneWayConverter
{
    public override object Convert(object? value) => Fmt.TimeAgo(ToLong(value));
}

public class DateTimeConverter : OneWayConverter
{
    public override object Convert(object? value) => Fmt.DateTimeText(ToLong(value));
}

public class GoodsStatusTextConverter : OneWayConverter
{
    public override object Convert(object? value) => Fmt.GoodsStatusText(value as string ?? "");
}

public class OrderStatusTextConverter : OneWayConverter
{
    public override object Convert(object? value) => Fmt.OrderStatusText(value as string ?? "");
}

/// <summary>订单状态 → 文字颜色（待发货橙、待收货蓝、已完成绿、已取消灰）</summary>
public class OrderStatusBrushConverter : OneWayConverter
{
    public override object Convert(object? value) => Palette.Strong(value as string ?? "");
}

/// <summary>订单状态 → 浅色底</summary>
public class OrderStatusSoftBrushConverter : OneWayConverter
{
    public override object Convert(object? value) => Palette.Soft(value as string ?? "");
}

internal static class Palette
{
    private static SolidColorBrush B(string hex)
    {
        var b = (SolidColorBrush)new BrushConverter().ConvertFromString(hex)!;
        b.Freeze();
        return b;
    }

    private static readonly Brush Orange = B("#E8710A"), OrangeSoft = B("#FFF1E0");
    private static readonly Brush Blue = B("#1677FF"), BlueSoft = B("#E8F1FF");
    private static readonly Brush Green = B("#1FB26B"), GreenSoft = B("#E5F7EE");
    private static readonly Brush Gray = B("#8A8A8A"), GraySoft = B("#F0F0EE");

    public static Brush Strong(string status) => status switch
    {
        "paid" => Orange,
        "shipped" => Blue,
        "completed" => Green,
        _ => Gray,
    };

    public static Brush Soft(string status) => status switch
    {
        "paid" => OrangeSoft,
        "shipped" => BlueSoft,
        "completed" => GreenSoft,
        _ => GraySoft,
    };
}

public class ConditionTextConverter : OneWayConverter
{
    public override object Convert(object? value) => Fmt.ConditionText((int)ToLong(value));
}

/// <summary>true → Visible，false → Collapsed。Invert="True" 时反过来。</summary>
public class BoolToVisibilityConverter : OneWayConverter
{
    public bool Invert { get; set; }

    public override object Convert(object? value) =>
        (value is true) != Invert ? Visibility.Visible : Visibility.Collapsed;
}

/// <summary>true ↔ false（比如"被禁言时输入框不可用"：IsEnabled="{Binding IsMuted, Converter={StaticResource InvBool}}"）</summary>
public class InverseBoolConverter : OneWayConverter
{
    public override object Convert(object? value) => value is not true;
}

/// <summary>有内容就显示：字符串不为空、数字不为 0、列表不为空、对象不为 null。Invert="True" 时反过来。</summary>
public class NotEmptyToVisibilityConverter : OneWayConverter
{
    public bool Invert { get; set; }

    public override object Convert(object? value)
    {
        bool notEmpty = value switch
        {
            null => false,
            string s => s.Length > 0,
            int i => i != 0,
            long l => l != 0,
            ICollection c => c.Count > 0,
            _ => true,
        };
        return notEmpty != Invert ? Visibility.Visible : Visibility.Collapsed;
    }
}

/// <summary>
/// 值等于 ConverterParameter 时返回 true，用于一组单选按钮绑同一个属性：
///   IsChecked="{Binding Sort, Converter={StaticResource ValueEquals}, ConverterParameter=price_asc}"
/// 选中时会把 ConverterParameter 写回属性（只支持 string 属性）。
/// </summary>
public class ValueEqualsConverter : IValueConverter
{
    public object Convert(object? value, Type targetType, object? parameter, CultureInfo culture) =>
        string.Equals(value?.ToString(), parameter?.ToString(), StringComparison.Ordinal);

    public object? ConvertBack(object? value, Type targetType, object? parameter, CultureInfo culture) =>
        value is true ? parameter?.ToString() : Binding.DoNothing;
}
