using System.ComponentModel;
using System.Windows;
using System.Windows.Media.Animation;
using XiaoYu.App.Services;

namespace XiaoYu.App.Shell;

public partial class MainWindow : Window
{
    private readonly MainViewModel _vm = new();

    public MainWindow()
    {
        InitializeComponent();
        DataContext = _vm;
        _vm.LoggedOut += () =>
        {
            new LoginWindow().Show();
            Close();
        };
        _vm.PropertyChanged += OnVmPropertyChanged;
        Navigator.Init(PageHost);
        Navigator.ToHome();
    }

    // 提示条：有文字就淡入上滑，没文字就淡出
    private void OnVmPropertyChanged(object? sender, PropertyChangedEventArgs e)
    {
        if (e.PropertyName != nameof(MainViewModel.ToastText)) return;
        string text = _vm.ToastText;
        if (text.Length > 0)
        {
            ToastLabel.Text = text;
            Toast.Visibility = Visibility.Visible;
            var show = new DoubleAnimation(1, TimeSpan.FromMilliseconds(220));
            Toast.BeginAnimation(OpacityProperty, show);
            ToastShift.BeginAnimation(System.Windows.Media.TranslateTransform.YProperty,
                new DoubleAnimation(16, 0, TimeSpan.FromMilliseconds(260)) { EasingFunction = new CubicEase { EasingMode = EasingMode.EaseOut } });
        }
        else
        {
            var hide = new DoubleAnimation(0, TimeSpan.FromMilliseconds(260));
            hide.Completed += (_, _) => { if (_vm.ToastText.Length == 0) Toast.Visibility = Visibility.Collapsed; };
            Toast.BeginAnimation(OpacityProperty, hide);
        }
    }

    protected override void OnClosing(CancelEventArgs e)
    {
        _vm.PropertyChanged -= OnVmPropertyChanged;
        _vm.Detach();
        base.OnClosing(e);
    }
}
