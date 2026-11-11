using System.Windows;
using System.Windows.Controls;

namespace XiaoYu.App.Controls;

/// <summary>
/// 商品卡片网格：按可用宽度算出放几列，再把卡片拉宽到正好铺满一行，右边不会空出一块。
/// 窗口越宽列越多；卡片最窄 MinItemWidth。
/// 用法：&lt;ItemsPanelTemplate&gt;&lt;c:CardGrid /&gt;&lt;/ItemsPanelTemplate&gt;
/// </summary>
public class CardGrid : Panel
{
    public static readonly DependencyProperty MinItemWidthProperty = DependencyProperty.Register(nameof(MinItemWidth), typeof(double), typeof(CardGrid),
        new FrameworkPropertyMetadata(200.0, FrameworkPropertyMetadataOptions.AffectsMeasure));
    public static readonly DependencyProperty SpacingProperty = DependencyProperty.Register(nameof(Spacing), typeof(double), typeof(CardGrid),
        new FrameworkPropertyMetadata(16.0, FrameworkPropertyMetadataOptions.AffectsMeasure));
    public static readonly DependencyProperty RowSpacingProperty = DependencyProperty.Register(nameof(RowSpacing), typeof(double), typeof(CardGrid),
        new FrameworkPropertyMetadata(18.0, FrameworkPropertyMetadataOptions.AffectsMeasure));

    public double MinItemWidth { get => (double)GetValue(MinItemWidthProperty); set => SetValue(MinItemWidthProperty, value); }
    public double Spacing { get => (double)GetValue(SpacingProperty); set => SetValue(SpacingProperty, value); }
    public double RowSpacing { get => (double)GetValue(RowSpacingProperty); set => SetValue(RowSpacingProperty, value); }

    private int _columns = 1;
    private double _itemWidth;
    private readonly List<double> _rowHeights = new();

    protected override Size MeasureOverride(Size available)
    {
        double width = double.IsInfinity(available.Width) ? (MinItemWidth + Spacing) * 4 - Spacing : available.Width;
        _columns = Math.Max(1, (int)((width + Spacing) / (MinItemWidth + Spacing)));
        _itemWidth = Math.Max(0, (width - Spacing * (_columns - 1)) / _columns);

        _rowHeights.Clear();
        int i = 0;
        foreach (UIElement child in InternalChildren)
        {
            child.Measure(new Size(_itemWidth, double.PositiveInfinity));
            int row = i / _columns;
            if (row == _rowHeights.Count) _rowHeights.Add(0);
            _rowHeights[row] = Math.Max(_rowHeights[row], child.DesiredSize.Height);
            i++;
        }
        double height = _rowHeights.Sum() + RowSpacing * Math.Max(0, _rowHeights.Count - 1);
        return new Size(width, height);
    }

    protected override Size ArrangeOverride(Size finalSize)
    {
        double y = 0;
        int i = 0;
        foreach (UIElement child in InternalChildren)
        {
            int row = i / _columns, col = i % _columns;
            if (col == 0 && row > 0) y += _rowHeights[row - 1] + RowSpacing;
            double h = row < _rowHeights.Count ? _rowHeights[row] : child.DesiredSize.Height;
            child.Arrange(new Rect(col * (_itemWidth + Spacing), y, _itemWidth, h));
            i++;
        }
        return finalSize;
    }
}
