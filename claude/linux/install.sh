#!/usr/bin/env bash
# 编译 → 装到 ~/.local/bin → 安装动画素材 → 设为登录自启 → 启动
# 不会修改 Claude 的任何文件
set -e
cd "$(dirname "$0")"

INTRO="${1:-../assets/intro.mp4}"
LOOP="${2:-../assets/loop.mp4}"

APP_DIR="$HOME/.local/share/ClaudeBootAnimation"
BIN_DIR="$HOME/.local/bin"
AUTOSTART_DIR="$HOME/.config/autostart"
DESKTOP_DIR="$HOME/.local/share/applications"

./build.sh "$INTRO" "$LOOP"

echo
echo "==> 安装到 $BIN_DIR"
mkdir -p "$BIN_DIR" "$APP_DIR" "$AUTOSTART_DIR" "$DESKTOP_DIR"
install -m 755 build/ClaudeBootAnimation "$BIN_DIR/claude-boot-animation"

echo "==> 安装动画素材到 $APP_DIR"
cp -f "$INTRO" "$APP_DIR/intro.mp4"
cp -f "$LOOP" "$APP_DIR/loop.mp4"

echo "==> 设置登录自启"
cat > "$AUTOSTART_DIR/claude-boot-animation.desktop" <<EOF
[Desktop Entry]
Type=Application
Name=Claude 开机动画
Comment=Claude 桌面端开屏动画与循环背景
Exec=$BIN_DIR/claude-boot-animation
X-GNOME-Autostart-enabled=true
Terminal=false
NoDisplay=false
EOF

echo "==> 添加应用菜单项"
cat > "$DESKTOP_DIR/claude-boot-animation.desktop" <<EOF
[Desktop Entry]
Type=Application
Name=Claude 开机动画
Comment=调整 Claude 开机动画与循环背景
Exec=$BIN_DIR/claude-boot-animation --settings
Icon=preferences-desktop-screensaver
Terminal=false
Categories=Utility;
EOF
command -v update-desktop-database >/dev/null && update-desktop-database "$DESKTOP_DIR" 2>/dev/null || true

echo "==> 启动常驻程序"
# 已在运行就先让它退出，避免两份常驻
"$BIN_DIR/claude-boot-animation" --stop 2>/dev/null || true
sleep 0.3
setsid "$BIN_DIR/claude-boot-animation" --settings </dev/null >/dev/null 2>&1 &
disown 2>/dev/null || true

sleep 1.2
echo "✨ 装好了！"
echo "   • 托盘出现 ✨ 图标，右键可调开关、不透明度、音量"
echo "   • 下次登录会自动启动；也可以从应用菜单打开「Claude 开机动画」"
echo "   • 想马上看效果：./build/ClaudeBootAnimation --preview"
echo
echo "设置文件：~/.config/ClaudeBootAnimation/config.json"
echo "日志：    $APP_DIR/log.txt"
