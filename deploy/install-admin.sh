#!/bin/bash
# 把审核后台（client/XiaoYu.Admin，ASP.NET Core 网页）发布并安装成 systemd 服务。
# 要和服务器装在同一台机器上（先运行过 install-linux.sh）。
#
# 用法（用普通用户运行，需要 .NET 8 SDK；安装那一步会自动用 sudo）：
#   bash deploy/install-admin.sh
# 可选环境变量：
#   XIAOYU_ADMIN_PORT  网页端口，默认 5280（只监听 127.0.0.1，外网通过 Cloudflare 隧道或反向代理访问）
#   XIAOYU_ADMIN_HTTP  设成 1 表示直接用 http 访问（Cookie 不加 Secure）；默认按 https 域名访问
#
# 安装后：
#   程序   /opt/xiaoyu-admin（自带 .NET 运行时，不依赖系统里装的 .NET）
#   网页   http://127.0.0.1:5280
#   日志   journalctl -u xiaoyu-admin -f
#   管理员 xiaoyu_server /etc/xiaoyu/server.conf --set-admin 用户名
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
OUT=/var/tmp/xiaoyu-admin-publish
PREFIX=/opt/xiaoyu-admin
PORT="${XIAOYU_ADMIN_PORT:-5280}"
SECURE=true
[ "${XIAOYU_ADMIN_HTTP:-0}" = "1" ] && SECURE=false

if [ "${1:-}" != "--install" ]; then
  # ---- 第 1 步（普通用户）：发布 ----
  DOTNET="$(command -v dotnet || echo "$HOME/.dotnet/dotnet")"
  [ -x "$DOTNET" ] || { echo "找不到 dotnet（需要 .NET 8 SDK）"; exit 1; }
  case "$(uname -m)" in
    aarch64|arm64) RID=linux-arm64 ;;
    x86_64) RID=linux-x64 ;;
    *) echo "不支持的 CPU：$(uname -m)"; exit 1 ;;
  esac
  echo "==> 发布审核后台（$RID，自带运行时）"
  rm -rf "$OUT"
  "$DOTNET" publish "$ROOT/client/XiaoYu.Admin/XiaoYu.Admin.csproj" -c Release -r "$RID" --self-contained true \
    -o "$OUT" -nologo -v q
  exec sudo XIAOYU_ADMIN_PORT="$PORT" XIAOYU_ADMIN_HTTP="${XIAOYU_ADMIN_HTTP:-0}" bash "$0" --install
fi

# ---- 第 2 步（root）：安装 ----
[ "$(id -u)" -eq 0 ] || { echo "安装需要 root"; exit 1; }
[ -x "$OUT/XiaoYu.Admin" ] || { echo "没找到发布结果 $OUT"; exit 1; }
CONF=/etc/xiaoyu/server.conf
[ -f "$CONF" ] || { echo "没找到 $CONF，请先运行 install-linux.sh 安装服务器"; exit 1; }
SERVER_PORT="$(sed -n 's/^port *= *//p' "$CONF" | head -1)"

echo "==> 安装到 $PREFIX"
rm -rf "$PREFIX.new"
cp -r "$OUT" "$PREFIX.new"
chown -R root:root "$PREFIX.new"
chmod -R go-w "$PREFIX.new"
rm -rf "$PREFIX.old"
[ -d "$PREFIX" ] && mv "$PREFIX" "$PREFIX.old"
mv "$PREFIX.new" "$PREFIX"

echo "==> 安装 systemd 服务（连本机服务器端口 $SERVER_PORT，网页端口 $PORT）"
cat > /etc/systemd/system/xiaoyu-admin.service <<UNITEOF
[Unit]
Description=广理职小鱼 审核后台（网页）
After=network.target xiaoyu.service

[Service]
# 每次启动用一个临时的无权限用户运行；它不碰数据库，只通过 TCP 连服务器
DynamicUser=yes
WorkingDirectory=$PREFIX
ExecStart=$PREFIX/XiaoYu.Admin
Environment=Urls=http://127.0.0.1:$PORT
Environment=XiaoYu__ServerHost=127.0.0.1
Environment=XiaoYu__ServerPort=$SERVER_PORT
Environment=XiaoYu__SecureCookie=$SECURE
Environment=TZ=Asia/Shanghai
Environment=DOTNET_NOLOGO=1
Restart=always
RestartSec=2
NoNewPrivileges=yes
ProtectSystem=strict
ProtectHome=yes
PrivateTmp=yes
PrivateDevices=yes
ProtectKernelTunables=yes
ProtectControlGroups=yes
RestrictAddressFamilies=AF_INET AF_INET6 AF_UNIX

[Install]
WantedBy=multi-user.target
UNITEOF
systemctl daemon-reload
systemctl enable xiaoyu-admin >/dev/null 2>&1
systemctl restart xiaoyu-admin
sleep 3

echo "==> 状态"
systemctl is-active xiaoyu-admin
ss -ltn | grep ":$PORT " || { echo "端口 $PORT 没有在监听，查看日志：journalctl -u xiaoyu-admin -n 30"; exit 1; }
curl -sS "http://127.0.0.1:$PORT/api/me" -o /dev/null -w "    /api/me 返回 %{http_code}（没登录应该是 401）\n" || true
