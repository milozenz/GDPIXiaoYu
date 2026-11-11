using System.IO;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Media.Imaging;

namespace XiaoYu.App.Services;

/// <summary>
/// 图片缓存：同一张图只从服务器下载一次。
/// 一般不用直接调它，在 XAML 里用 RemoteImage 就行：
///   &lt;Image s:RemoteImage.Source="{Binding Cover}" Stretch="UniformToFill" /&gt;
/// </summary>
public static class ImageCache
{
    private static readonly Dictionary<string, Task<BitmapImage?>> Cache = new();

    public static Task<BitmapImage?> GetAsync(string name)
    {
        lock (Cache)
        {
            if (!Cache.TryGetValue(name, out var task)) Cache[name] = task = LoadAsync(name);
            return task;
        }
    }

    /// <summary>
    /// 刚上传的图片：本地已经有字节了，直接放进缓存，界面马上能显示，不用再从服务器下载一遍。
    /// </summary>
    public static void Put(string name, byte[] bytes)
    {
        lock (Cache) Cache[name] = FromBytesAsync(name, bytes);
    }

    private static async Task<BitmapImage?> FromBytesAsync(string name, byte[] bytes)
    {
        try { return await Task.Run(() => Decode(bytes)); }
        catch (Exception)
        {
            lock (Cache) Cache.Remove(name);  // 解不开就算了，下次照常从服务器下载
            return null;
        }
    }

    private static async Task<BitmapImage?> LoadAsync(string name)
    {
        try
        {
            var bytes = await AppServices.Api.GetImageAsync(name);
            return await Task.Run(() => Decode(bytes));
        }
        catch (Exception)
        {
            lock (Cache) Cache.Remove(name);  // 失败了下次再试
            return null;
        }
    }

    /// <summary>把图片字节转成 WPF 能显示的图片</summary>
    public static BitmapImage Decode(byte[] bytes, int decodeWidth = 0)
    {
        var bmp = new BitmapImage();
        using var ms = new MemoryStream(bytes);
        bmp.BeginInit();
        bmp.CacheOption = BitmapCacheOption.OnLoad;
        bmp.StreamSource = ms;
        if (decodeWidth > 0) bmp.DecodePixelWidth = decodeWidth;
        bmp.EndInit();
        bmp.Freeze();  // 冻结后才能在别的线程创建、在界面线程使用
        return bmp;
    }
}

/// <summary>
/// 让 Image 控件直接显示服务器上的图片（传图片名就行）：
///   xmlns:s="clr-namespace:XiaoYu.App.Services"
///   &lt;Image s:RemoteImage.Source="{Binding Cover}" /&gt;
/// </summary>
public static class RemoteImage
{
    public static readonly DependencyProperty SourceProperty = DependencyProperty.RegisterAttached(
        "Source", typeof(string), typeof(RemoteImage), new PropertyMetadata(null, OnSourceChanged));

    public static string? GetSource(DependencyObject obj) => (string?)obj.GetValue(SourceProperty);

    public static void SetSource(DependencyObject obj, string? value) => obj.SetValue(SourceProperty, value);

    private static async void OnSourceChanged(DependencyObject d, DependencyPropertyChangedEventArgs e)
    {
        if (d is not Image image) return;
        var name = e.NewValue as string;
        image.Source = null;
        if (string.IsNullOrEmpty(name)) return;
        var bmp = await ImageCache.GetAsync(name);
        if (GetSource(image) == name) image.Source = bmp;  // 列表滚动复用控件时，防止旧图盖住新图
    }
}
