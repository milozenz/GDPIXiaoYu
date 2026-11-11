using System.IO;
using System.Windows.Media;
using System.Windows.Media.Imaging;
using Microsoft.Win32;

namespace XiaoYu.App.Services;

/// <summary>选图片、压缩图片（上传前用）</summary>
public static class ImageTools
{
    private const int MaxSide = 1280;   // 长边最多 1280 像素
    private const int JpegQuality = 85;

    /// <summary>弹出选择图片的窗口，可以多选。取消返回空数组。</summary>
    public static string[] PickImages(bool multiple = true)
    {
        if (PickHook != null) return PickHook(multiple);
        var dialog = new OpenFileDialog
        {
            Title = "选择图片",
            Filter = "图片|*.jpg;*.jpeg;*.png;*.gif;*.bmp;*.webp",
            Multiselect = multiple,
        };
        return dialog.ShowDialog() == true ? dialog.FileNames : Array.Empty<string>();
    }

    /// <summary>测试用：设置后不弹出选择图片的窗口，直接用它返回的文件。正常运行时为 null。</summary>
    public static Func<bool, string[]>? PickHook { get; set; }

    /// <summary>
    /// 读一张本地图片，缩小到长边 1280 像素以内，转成 JPG。手机拍的照片动辄几 MB，压缩后一般只有一两百 KB。
    /// 用法：
    ///   var bytes = ImageTools.LoadForUpload(path);
    ///   string name = await AppServices.Api.UploadImageAsync(bytes, "jpg");
    /// </summary>
    public static byte[] LoadForUpload(string path)
    {
        using var file = File.OpenRead(path);
        var frame = BitmapDecoder.Create(file, BitmapCreateOptions.PreservePixelFormat, BitmapCacheOption.OnLoad).Frames[0];

        BitmapSource source = frame;
        int longest = Math.Max(frame.PixelWidth, frame.PixelHeight);
        if (longest > MaxSide)
        {
            double scale = (double)MaxSide / longest;
            source = new TransformedBitmap(frame, new ScaleTransform(scale, scale));
        }
        if (source.Format != PixelFormats.Bgr24 && source.Format != PixelFormats.Bgr32)
            source = new FormatConvertedBitmap(source, PixelFormats.Bgr24, null, 0);  // 去掉透明通道，JPG 不支持

        var encoder = new JpegBitmapEncoder { QualityLevel = JpegQuality };
        encoder.Frames.Add(BitmapFrame.Create(source));
        using var output = new MemoryStream();
        encoder.Save(output);
        return output.ToArray();
    }
}
