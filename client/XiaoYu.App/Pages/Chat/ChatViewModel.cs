using System.Collections.ObjectModel;
using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;
using XiaoYu.App.Services;
using XiaoYu.App.ViewModels;
using XiaoYu.Core.Api;
using XiaoYu.Core.Models;
using XiaoYu.Core.Utils;

namespace XiaoYu.App.Pages.Chat;

/// <summary>
/// 聊天里的一条消息（带"是不是我发的"和"要不要显示时间"）。
/// 内容安全相关：被屏蔽的消息灰色显示；对方发来的、命中二级词的消息下面显示黄色提醒条和"举报"。
/// </summary>
public partial class ChatItem : ObservableObject
{
    public ChatMessage Message { get; init; } = new();
    public bool IsMine { get; init; }
    public bool ShowTime { get; set; }
    public string TimeText => Fmt.DateTimeText(Message.CreatedAt);

    [ObservableProperty]
    [NotifyPropertyChangedFor(nameof(ShowWarn), nameof(CanReport))]
    private bool _isMasked;

    [ObservableProperty]
    [NotifyPropertyChangedFor(nameof(CanReport))]
    private bool _reported;

    [ObservableProperty] private string _content = "";

    public static ChatItem From(ChatMessage m, bool showTime) => new()
    {
        Message = m,
        IsMine = m.FromId == AppServices.MyId,
        ShowTime = showTime,
        Content = m.Content,
        IsMasked = m.Flag == MessageFlag.Masked,
    };

    /// <summary>对方发来的、命中二级词、还没被屏蔽的消息要显示提醒条</summary>
    public bool ShowWarn => !IsMine && !IsMasked && Message.Flag == MessageFlag.Warn;

    public string WarnText => Message.FlagKind == "threat"
        ? "对方的话里可能有威胁、骚扰的内容。觉得有问题？"
        : "对方提到了站外交易 / 转账，平台外交易没有保障。觉得有问题？";

    /// <summary>对方发来的、没被屏蔽、我还没举报过的消息可以举报</summary>
    public bool CanReport => !IsMine && !IsMasked && !Reported;

    public void SetMasked(bool masked)
    {
        IsMasked = masked;
        Content = masked ? "该消息因违规已被屏蔽" : Message.Content;
    }
}

/// <summary>
/// 和某个人聊天：进入时读最近 30 条并标已读；收到推送实时追加；回车发送。
/// goodsId 不为 0 表示从某件商品点进来的，顶部显示这件商品，发的消息也会带上它。
/// </summary>
public partial class ChatViewModel : ViewModelBase
{
    private const int PageSize = 30;
    private readonly long _peerId;
    private long _goodsId;

    [ObservableProperty] private UserProfile? _peer;
    [ObservableProperty] private GoodsBrief? _goods;
    [ObservableProperty] private string _draft = "";
    [ObservableProperty] private bool _hasOlder;
    [ObservableProperty] private bool _isMuted;
    [ObservableProperty] private string _muteText = "";

    public ObservableCollection<ChatItem> Items { get; } = new();

    /// <summary>通知页面把消息列表滚到最底</summary>
    public event Action? ScrollToEndRequested;

    public ChatViewModel(long peerId, long goodsId)
    {
        _peerId = peerId;
        _goodsId = goodsId;
    }

    public override async Task OnNavigatedToAsync()
    {
        Api.MessageReceived -= OnMessageReceived;
        Api.MessageReceived += OnMessageReceived;
        Api.MessageMasked -= OnMessageMasked;
        Api.MessageMasked += OnMessageMasked;
        AppServices.CurrentUserChanged -= UpdateMute;
        AppServices.CurrentUserChanged += UpdateMute;
        UpdateMute();
        await RunAsync(async () =>
        {
            Peer = await Api.GetUserAsync(_peerId);
            var history = await Api.GetChatHistoryAsync(_peerId, 0, PageSize);
            Items.Clear();
            foreach (var m in history) Append(m);
            HasOlder = history.Count >= PageSize;
            if (_goodsId == 0) _goodsId = history.LastOrDefault(m => m.GoodsId > 0)?.GoodsId ?? 0;
            await LoadGoodsAsync();
            await MarkReadAsync();
            ScrollToEndRequested?.Invoke();
        });
    }

    public override void OnNavigatedFrom()
    {
        Api.MessageReceived -= OnMessageReceived;
        Api.MessageMasked -= OnMessageMasked;
        AppServices.CurrentUserChanged -= UpdateMute;
    }

    private void UpdateMute() => Ui.Run(() =>
    {
        long until = AppServices.CurrentUser?.MutedUntil ?? 0;
        IsMuted = Fmt.MuteLeft(until) != "";
        MuteText = IsMuted ? $"你因违规被禁言，还剩 {Fmt.MuteLeft(until)}，期间不能发消息" : "";
    });

    private void OnMessageMasked(MessageMasked e) => Ui.Run(() =>
    {
        foreach (var item in Items)
            if (item.Message.MessageId == e.MessageId) item.SetMasked(e.Masked);
    });

    private async Task LoadGoodsAsync()
    {
        if (_goodsId <= 0) { Goods = null; return; }
        try
        {
            var d = await Api.GetGoodsDetailAsync(_goodsId);
            Goods = d;
        }
        catch (ApiException) { Goods = null; }
    }

    private async Task MarkReadAsync()
    {
        try
        {
            await Api.MarkReadAsync(_peerId);
            AppServices.NotifyUnreadChanged();
        }
        catch (ApiException) { }
    }

    private void Append(ChatMessage m)
    {
        var last = Items.LastOrDefault();
        Items.Add(ChatItem.From(m, last == null || m.CreatedAt - last.Message.CreatedAt > 300));
    }

    private void OnMessageReceived(ChatMessage m) => Ui.Run(async () =>
    {
        if (m.FromId != _peerId) return;
        Append(m);
        ScrollToEndRequested?.Invoke();
        await MarkReadAsync();
    });

    [RelayCommand]
    private async Task SendAsync()
    {
        string text = Draft.Trim();
        if (text.Length == 0) return;
        if (text.EnumerateRunes().Count() > 500) { ErrorMessage = "一条消息最多 500 个字"; return; }
        Draft = "";
        bool ok = await RunAsync(async () =>
        {
            var sent = await Api.SendMessageAsync(_peerId, text, _goodsId);
            Append(sent);
            ScrollToEndRequested?.Invoke();
        });
        if (!ok)
        {
            Draft = text;     // 发失败了，把内容还给输入框
            if (ErrorMessage.Contains("禁言")) await AppServices.RefreshMeAsync();
        }
    }

    [RelayCommand]
    private async Task ReportAsync(ChatItem? item)
    {
        if (item == null || !item.CanReport) return;
        string preview = item.Content.Length > 40 ? item.Content[..40] + "…" : item.Content;
        var input = Ui.Prompt("举报这条消息", $"「{preview}」\n提交后 AI 会先判断，拿不准的交给管理员人工处理。",
            "说说为什么举报（必填，最多 200 字）", ReportTypes.All.Select(t => t.Label).ToArray());
        if (input == null) return;
        var (choice, reason) = input.Value;
        if (await RunAsync(() => Api.ReportMessageAsync(item.Message.MessageId, ReportTypes.All[choice].Code, reason)))
        {
            item.Reported = true;
            Ui.Toast("举报已提交，正在审核");
        }
    }

    [RelayCommand]
    private async Task AppealMuteAsync()
    {
        var input = Ui.Prompt("申诉禁言", "说明一下情况，管理员会人工处理。", "申诉理由（必填，最多 200 字）");
        if (input == null) return;
        if (await RunAsync(() => Api.AppealAsync("mute", input.Value.Text)))
            Ui.Toast("申诉已提交，请等待管理员处理");
    }

    [RelayCommand]
    private async Task LoadOlderAsync()
    {
        var first = Items.FirstOrDefault();
        if (first == null) return;
        await RunAsync(async () =>
        {
            var older = await Api.GetChatHistoryAsync(_peerId, first.Message.MessageId, PageSize);
            HasOlder = older.Count >= PageSize;
            for (int i = 0; i < older.Count; i++)
                Items.Insert(i, ChatItem.From(older[i], i == 0 || older[i].CreatedAt - older[i - 1].CreatedAt > 300));
        });
    }

    [RelayCommand]
    private void OpenPeer() => Navigator.ToUserProfile(_peerId);

    [RelayCommand]
    private void OpenGoods()
    {
        if (Goods != null) Navigator.ToGoodsDetail(Goods.GoodsId);
    }
}
