using System.Collections.ObjectModel;
using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;
using XiaoYu.App.Services;
using XiaoYu.App.ViewModels;
using XiaoYu.Core.Models;

namespace XiaoYu.App.Pages.Chat;

/// <summary>消息列表：每个聊过的人一行，显示最后一条消息和未读数。收到新私信时自动刷新。</summary>
public partial class ChatListViewModel : ViewModelBase
{
    [ObservableProperty] private bool _isEmpty;

    public ObservableCollection<Conversation> Items { get; } = new();

    public override async Task OnNavigatedToAsync()
    {
        Api.MessageReceived -= OnMessageReceived;   // 先取消再订阅，避免返回页面时重复订阅
        Api.MessageReceived += OnMessageReceived;
        await ReloadAsync();
    }

    public override void OnNavigatedFrom() => Api.MessageReceived -= OnMessageReceived;

    private void OnMessageReceived(ChatMessage message) => Ui.Run(() => _ = ReloadAsync());

    private async Task ReloadAsync()
    {
        await RunAsync(async () =>
        {
            var list = await Api.GetConversationsAsync();
            Items.Clear();
            foreach (var c in list) Items.Add(c);
            IsEmpty = Items.Count == 0;
        });
    }

    [RelayCommand]
    private void Open(Conversation? conversation)
    {
        if (conversation != null) Navigator.ToChat(conversation.Peer.UserId, conversation.LastMessage.GoodsId);
    }
}
