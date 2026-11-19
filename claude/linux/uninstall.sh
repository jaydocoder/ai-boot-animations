#!/usr/bin/env bash
# 彻底卸载：停常驻、删自启、删程序、删素材、删设置和日志。
# Claude 本身从来没被改过，卸载后就是原装的样子。
set -u

APP_DIR="$HOME/.local/share/ClaudeBootAnimation"
BIN="$HOME/.local/bin/claude-boot-animation"
CFG_DIR="$HOME/.config/ClaudeBootAnimation"
SOCK="${XDG_RUNTIME_DIR:-/tmp}/claude-boot-animation.sock"

# 先让运行中的实例退出（加超时，避免卡住），再兜底杀掉
if [[ -x "$BIN" ]]; then
    timeout 3 "$BIN" --stop >/dev/null 2>&1 || true
fi
sleep 0.5
pkill -x claude-boot-animation >/dev/null 2>&1 || true
sleep 0.3
pkill -9 -x claude-boot-animation >/dev/null 2>&1 || true

rm -f "$HOME/.config/autostart/claude-boot-animation.desktop"
rm -f "$HOME/.local/share/applications/claude-boot-animation.desktop"
rm -f "$BIN"
rm -rf "$APP_DIR"
rm -rf "$CFG_DIR"
rm -f "$SOCK"

# 兼容早期数据落在 app-id 子目录里的情况
rm -rf "$HOME/.local/share/ai-boot-animations"

# 重新生成桌面菜单缓存
command -v update-desktop-database >/dev/null 2>&1 && \
    update-desktop-database "$HOME/.local/share/applications" >/dev/null 2>&1 || true

echo "已卸载干净。"
