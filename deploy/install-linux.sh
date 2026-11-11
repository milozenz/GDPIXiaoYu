#!/bin/bash
# 把广理职小鱼服务器编译并安装成 systemd 服务（Ubuntu / WSL / VPS 通用，需要 root）。
#
# 用法：sudo bash deploy/install-linux.sh [server 源码目录]
# 可选环境变量：
#   XIAOYU_BIND  监听地址，默认 127.0.0.1（只允许本机）；要让局域网/外网连就设成 0.0.0.0
#   XIAOYU_PORT  端口，默认 9527（小于 1024 的端口，比如 443，也可以，服务会自动获得绑定权限）
#
# AI 审核和邮件验证码要用的密钥放在 /etc/xiaoyu/secrets.env（root 只读，不进代码仓库），例如：
#   OPENROUTER_API_KEY=sk-or-...
#   TYPESAFE_API_KEY=...
#   XIAOYU_SMTP_PASS=邮箱授权码
#
# 安装后：
#   程序   /opt/xiaoyu/bin/xiaoyu_server
#   配置   /etc/xiaoyu/server.conf（已存在就不覆盖）
#   密钥   /etc/xiaoyu/secrets.env（可选）
#   数据   /var/lib/xiaoyu（数据库、图片）
#   日志   journalctl -u xiaoyu -f
#   管理   systemctl status|restart|stop xiaoyu
set -euo pipefail

SRC="${1:-$(cd "$(dirname "$0")/.." && pwd)/server}"
BIND="${XIAOYU_BIND:-127.0.0.1}"
PORT="${XIAOYU_PORT:-9527}"
PREFIX=/opt/xiaoyu
DATA=/var/lib/xiaoyu
CONF=/etc/xiaoyu/server.conf
BUILD=/var/tmp/xiaoyu-build

[ "$(id -u)" -eq 0 ] || { echo "需要 root 权限：sudo bash $0"; exit 1; }
command -v cmake >/dev/null && command -v gcc >/dev/null || { echo "缺少编译环境：apt install build-essential cmake"; exit 1; }
[ -f /usr/include/curl/curl.h ] || [ -n "$(ls /usr/include/*/curl/curl.h 2>/dev/null)" ] ||
  echo "提醒：没装 libcurl 开发包（apt install libcurl4-openssl-dev），AI 审核和邮件验证码会不可用"
[ -f "$SRC/CMakeLists.txt" ] || { echo "找不到服务器源码：$SRC"; exit 1; }

echo "==> 编译（Release）"
cmake -S "$SRC" -B "$BUILD" -DCMAKE_BUILD_TYPE=Release >/dev/null
cmake --build "$BUILD" -j"$(nproc)" --target xiaoyu_server 2>&1 | tail -3

echo "==> 创建专用用户和目录"
id xiaoyu >/dev/null 2>&1 || useradd --system --home-dir "$DATA" --shell /usr/sbin/nologin xiaoyu
install -d -o xiaoyu -g xiaoyu -m 750 "$DATA"
install -d -m 755 "$PREFIX/bin" /etc/xiaoyu
install -m 755 "$BUILD/xiaoyu_server" "$PREFIX/bin/xiaoyu_server"

echo "==> 配置 $CONF"
if [ ! -f "$CONF" ]; then
  cat > "$CONF" <<CONFEOF
# 广理职小鱼服务器配置（由 install-linux.sh 生成，改完后 systemctl restart xiaoyu）
bind = $BIND
port = $PORT
db_path = $DATA/xiaoyu.db
data_dir = $DATA
# 日志留空 = 输出到 systemd 日志（journalctl -u xiaoyu）
log_file =
max_frame_mb = 8
idle_timeout = 120
# 注册必须验证邮箱（1 = 必须）
require_email = 1
# AI 审核（Jev 决策模型）：按顺序试，前一个失败换下一个；密钥从环境变量读（见 secrets.env）
jev_model = jev-1.14
jev_source = https://openrouter.ai/api/v1/systemone OPENROUTER_API_KEY
# jev_source = https://api.typesafe.ai/v1/systemone TYPESAFE_API_KEY
# 验证码邮件（不配 = 开发模式，验证码写进日志并返回给客户端）
# smtp_url = smtps://smtp.qq.com:465
# smtp_user = 发件邮箱@qq.com
# smtp_pass_env = XIAOYU_SMTP_PASS
CONFEOF
else
  echo "    已存在，保留不动"
fi

echo "==> 安装 systemd 服务"
# 小于 1024 的端口要给程序"绑定低端口"的权限（只给这一项，不用 root 运行）
CAPS=""
if [ "$(sed -n 's/^port *= *//p' "$CONF" | head -1)" -lt 1024 ] 2>/dev/null; then
  CAPS="AmbientCapabilities=CAP_NET_BIND_SERVICE
CapabilityBoundingSet=CAP_NET_BIND_SERVICE"
fi
cat > /etc/systemd/system/xiaoyu.service <<UNITEOF
[Unit]
Description=广理职小鱼 校园二手交易服务器
After=network.target

[Service]
User=xiaoyu
Group=xiaoyu
WorkingDirectory=$DATA
ExecStart=$PREFIX/bin/xiaoyu_server $CONF
# 密钥文件（可选，"-" 表示没有也不报错）；日志用北京时间
EnvironmentFile=-/etc/xiaoyu/secrets.env
Environment=TZ=Asia/Shanghai
$CAPS
Restart=always
RestartSec=2
# 安全加固：不能提权、只读系统目录、只能写数据目录
NoNewPrivileges=yes
ProtectSystem=strict
ProtectHome=yes
PrivateTmp=yes
ReadWritePaths=$DATA

[Install]
WantedBy=multi-user.target
UNITEOF
systemctl daemon-reload
systemctl enable xiaoyu >/dev/null 2>&1
systemctl restart xiaoyu
sleep 1.5

echo "==> 状态"
systemctl is-active xiaoyu
PORT_NOW="$(sed -n 's/^port *= *//p' "$CONF" | head -1)"
ss -ltn | grep ":$PORT_NOW " || { echo "端口 $PORT_NOW 没有在监听，查看日志：journalctl -u xiaoyu -n 30"; exit 1; }
journalctl -u xiaoyu -n 4 --no-pager | sed 's/^/    /'
