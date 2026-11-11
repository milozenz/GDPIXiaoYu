#!/bin/bash
# 给服务器加 TLS 加密：nginx 在 443 端口负责加密，解密后转给只听本机的 C 服务器（127.0.0.1:9527）。
#
#   客户端 ──TLS（443）──▶ nginx ──明文（本机）──▶ xiaoyu_server 127.0.0.1:9527
#
# 证书用 Let's Encrypt 的免费证书（certbot 通过 80 端口验证域名，到期前自动续期）。
# 客户端用 SslStream 连接，按 Windows 自带的证书信任链校验，不需要额外配置。
#
# 用法：bash deploy/install-tls.sh [域名，默认 server.aaalez.com]
# 前提：已经运行过 install-linux.sh；域名的 A 记录指向这台服务器（不经过代理）；80 和 443 端口对外开放；
#       sudo apt install nginx libnginx-mod-stream certbot
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
DOMAIN="${1:-server.aaalez.com}"
CONF=/etc/xiaoyu/server.conf
INNER_PORT=9527
WEBROOT=/var/www/letsencrypt
CERT_DIR="/etc/letsencrypt/live/$DOMAIN"

command -v certbot >/dev/null || { echo "缺少 certbot：sudo apt install certbot"; exit 1; }
[ -f /etc/nginx/modules-enabled/50-mod-stream.conf ] || { echo "缺少 nginx stream 模块：sudo apt install libnginx-mod-stream"; exit 1; }
sudo test -f "$CONF" || { echo "没找到 $CONF，请先运行 install-linux.sh"; exit 1; }

echo "==> 1. 申请证书（$DOMAIN）"
if sudo test -f "$CERT_DIR/fullchain.pem"; then
  echo "    已有证书，跳过（certbot 会自动续期）"
else
  sudo mkdir -p "$WEBROOT"
  sudo tee /etc/nginx/sites-available/xiaoyu-acme.conf >/dev/null <<NGINX
# Let's Encrypt 验证域名用（由 deploy/install-tls.sh 生成）
server {
    listen 80;
    server_name $DOMAIN;
    location /.well-known/acme-challenge/ { root $WEBROOT; }
    location / { return 404; }
}
NGINX
  sudo ln -sf /etc/nginx/sites-available/xiaoyu-acme.conf /etc/nginx/sites-enabled/xiaoyu-acme.conf
  sudo nginx -t 2>&1 | tail -1
  sudo systemctl reload nginx
  sudo certbot certonly --webroot -w "$WEBROOT" -d "$DOMAIN" --agree-tos --register-unsafely-without-email -n \
    --deploy-hook "systemctl reload nginx" 2>&1 | grep -E "Successfully|Certificate is saved|error|Error" || true
  sudo test -f "$CERT_DIR/fullchain.pem" || { echo "证书申请失败"; exit 1; }
fi

echo "==> 2. 服务器改成只听本机 127.0.0.1:$INNER_PORT"
sudo sed -i -e "s/^bind *=.*/bind = 127.0.0.1/" -e "s/^port *=.*/port = $INNER_PORT/" "$CONF"
sudo bash "$ROOT/deploy/install-linux.sh" "$ROOT/server" | grep -E "==> 状态|active|LISTEN"

echo "==> 3. nginx 在 443 端口做 TLS"
sudo mkdir -p /etc/nginx/streams-enabled
sudo tee /etc/nginx/streams-enabled/xiaoyu-tls.conf >/dev/null <<NGINX
# 广理职小鱼：443 端口 TLS 加密，解密后转给本机服务器（由 deploy/install-tls.sh 生成）
limit_conn_zone \$binary_remote_addr zone=xiaoyu_conn:10m;
log_format xiaoyu_stream '\$time_local \$remote_addr \$status \$bytes_received/\$bytes_sent \${session_time}s \$ssl_protocol';

server {
    listen 443 ssl;
    ssl_certificate     $CERT_DIR/fullchain.pem;
    ssl_certificate_key $CERT_DIR/privkey.pem;
    ssl_protocols       TLSv1.2 TLSv1.3;
    ssl_session_cache   shared:xiaoyu_ssl:10m;
    ssl_handshake_timeout 10s;

    proxy_pass 127.0.0.1:$INNER_PORT;
    proxy_connect_timeout 5s;
    proxy_timeout 10m;            # 客户端每 30 秒心跳一次，真正的掉线由服务器的 idle_timeout 判断
    limit_conn xiaoyu_conn 30;    # 同一个 IP 最多 30 条连接
    access_log /var/log/nginx/xiaoyu-stream.log xiaoyu_stream;
}
NGINX
if ! sudo grep -q "streams-enabled" /etc/nginx/nginx.conf; then
  printf '\n# TCP 转发（广理职小鱼的 TLS，见 /etc/nginx/streams-enabled）\nstream {\n\tinclude /etc/nginx/streams-enabled/*.conf;\n}\n' |
    sudo tee -a /etc/nginx/nginx.conf >/dev/null
fi
sudo nginx -t 2>&1 | tail -1
sudo systemctl reload nginx
sleep 1
ss -ltn | grep -E ":443 " || { echo "443 没有在监听"; exit 1; }

echo "==> 4. 检查：TLS 握手 + 发一个 sys.ping"
python3 - "$DOMAIN" <<'PY'
import json, socket, ssl, struct, sys
host = sys.argv[1]
ctx = ssl.create_default_context()
with socket.create_connection((host, 443), timeout=8) as raw, ctx.wrap_socket(raw, server_hostname=host) as s:
    body = json.dumps({"id": 1, "cmd": "sys.ping", "data": {}}).encode()
    s.sendall(struct.pack(">I", len(body)) + body)
    n = struct.unpack(">I", s.recv(4))[0]
    data = b""
    while len(data) < n:
        data += s.recv(n - len(data))
    print(f"    {s.version()}，证书校验通过，sys.ping 回复：{data.decode()}")
PY
echo "    审核后台连的是服务器端口，记得重新运行 install-admin.sh"
