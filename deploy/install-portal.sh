#!/bin/bash
# 安装门户网站（介绍 + 下载客户端）：把 portal/ 和线上版 exe 放到 /var/www/xiaoyu-portal，用 nginx 在本机端口提供。
# 外网通过 Cloudflare 隧道访问（xiaoyu.aaalez.com → http://127.0.0.1:8088）。
#
# 用法：bash deploy/install-portal.sh <线上版 XiaoYu.exe> [版本号，默认 1.0.0]
#   线上版 exe 在 Windows 上编译：dotnet publish client\XiaoYu.App -c Release -r win-x64 -p:Online=true -o out
# 可选环境变量：XIAOYU_PORTAL_PORT  端口，默认 8088（只监听 127.0.0.1）
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
EXE="${1:?用法：bash deploy/install-portal.sh <XiaoYu.exe> [版本号]}"
VERSION="${2:-1.0.0}"
PORT="${XIAOYU_PORTAL_PORT:-8088}"
WEB=/var/www/xiaoyu-portal
FILE="XiaoYu-$VERSION-win-x64.exe"
[ -f "$EXE" ] || { echo "找不到 $EXE"; exit 1; }
command -v nginx >/dev/null || { echo "需要 nginx：sudo apt install nginx"; exit 1; }

echo "==> 准备网页文件"
STAGE="$(mktemp -d)"
trap 'rm -rf "$STAGE"' EXIT
cp -r "$ROOT/portal/." "$STAGE/"
mkdir -p "$STAGE/download"
cp "$EXE" "$STAGE/download/$FILE"
SIZE="$(awk -v b="$(stat -c %s "$EXE")" 'BEGIN { printf "%.0f MB", b / 1048576 }')"
SHA="$(sha256sum "$EXE" | cut -d' ' -f1 | tr 'a-f' 'A-F')"   # 和 Windows 的 Get-FileHash 一样用大写
DATE="$(TZ=Asia/Shanghai date '+%Y-%m-%d')"
sed -i -e "s/{{FILE}}/$FILE/g" -e "s/{{VERSION}}/$VERSION/g" -e "s/{{SIZE}}/$SIZE/g" \
       -e "s/{{SHA256}}/$SHA/g" -e "s/{{DATE}}/$DATE/g" "$STAGE/index.html"
grep -q '{{' "$STAGE/index.html" && { echo "index.html 里还有没填的 {{...}}"; exit 1; }
echo "    $FILE  $SIZE  SHA256 $SHA"

echo "==> 安装到 $WEB"
sudo rm -rf "$WEB.new"
sudo cp -r "$STAGE" "$WEB.new"
sudo chown -R root:root "$WEB.new"
sudo chmod -R u=rwX,go=rX "$WEB.new"
sudo rm -rf "$WEB.old"
[ -d "$WEB" ] && sudo mv "$WEB" "$WEB.old"
sudo mv "$WEB.new" "$WEB"

echo "==> nginx 站点（127.0.0.1:$PORT）"
sudo tee /etc/nginx/sites-available/xiaoyu-portal.conf >/dev/null <<NGINX
# 广理职小鱼 门户网站（由 deploy/install-portal.sh 生成）。只听本机，外网走 Cloudflare 隧道
server {
    listen 127.0.0.1:$PORT;
    server_name _;
    root $WEB;
    index index.html;
    server_tokens off;

    add_header X-Content-Type-Options nosniff always;
    add_header Referrer-Policy no-referrer always;
    add_header Content-Security-Policy "default-src 'self'; img-src 'self'; style-src 'self'; script-src 'none'; frame-ancestors 'none'; base-uri 'none'; form-action 'none'" always;

    location / {
        try_files \$uri \$uri/ =404;
        add_header Cache-Control "no-cache" always;
        add_header X-Content-Type-Options nosniff always;
        add_header Referrer-Policy no-referrer always;
        add_header Content-Security-Policy "default-src 'self'; img-src 'self'; style-src 'self'; script-src 'none'; frame-ancestors 'none'; base-uri 'none'; form-action 'none'" always;
    }
    location /download/ {
        types { application/vnd.microsoft.portable-executable exe; }
        add_header Content-Disposition "attachment" always;
        add_header Cache-Control "public, max-age=86400" always;
        add_header X-Content-Type-Options nosniff always;
    }
}
NGINX
sudo ln -sf /etc/nginx/sites-available/xiaoyu-portal.conf /etc/nginx/sites-enabled/xiaoyu-portal.conf
sudo nginx -t 2>&1 | tail -1
sudo systemctl reload nginx

echo "==> 检查"
curl -sS -o /dev/null -w "    首页 %{http_code}\n" "http://127.0.0.1:$PORT/"
curl -sS -o /dev/null -w "    下载 %{http_code}  %{size_download} 字节\n" "http://127.0.0.1:$PORT/download/$FILE"
