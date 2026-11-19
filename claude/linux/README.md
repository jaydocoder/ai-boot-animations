# Claude 开机动画 · Linux 版

> 属于 [ai-boot-animations](../../README.md) 合集。macOS 版见 [../README.md](../README.md)，Windows 版见 [../windows/](../windows/)。

给 Linux 上的 Claude 桌面端加一段开机动画：打开 Claude 时先放一段持剑少女的开屏，再变成一层半透明的动态背景。**不修改 Claude 的任何文件**，卸载后什么都不留。

- 🎬 **开屏动画**：金色线稿 → 上色 → 睁眼、剑光、火凤、烟花，带音效和英文花体字幕
- 🌙 **循环背景**：开屏结束后半透明循环播放，点击直接穿透到下层应用，不影响使用
- ✨ **托盘随手调**：开屏开关、循环背景开关、背景不透明度、开屏音量，拖动滑块实时生效
- 🧼 **不改 Claude 本体**：独立程序，Claude 自动更新、签名、权限都不受影响

> 非官方同人项目，与 Anthropic 无关。Claude 是 Anthropic 的商标。

## 与 macOS / Windows 版的区别

macOS 版会用 `CGWindowListCopyWindowInfo` 读出 Claude 主窗口的位置，把浮层**精确贴在 Claude 窗口上方**。

Linux 版做不到这一点，原因是 Wayland 的安全模型：**普通程序无法获取其它应用窗口的位置和大小**（X11 时代可以用 `xdotool`/`wmctrl`，Wayland 下原生窗口在合成器里，外部进程看不到）。你的 Claude 正是用 `--ozone-platform=wayland` 原生运行的，不在 XWayland 里，`wmctrl -l` 里根本不会出现它的窗口。

所以 Linux 版改为**铺满全屏的全局浮层**（也可切回屏幕居中）：

| 项目 | macOS 版 | Linux 版 |
|---|---|---|
| 浮层位置 | 精确贴合 Claude 窗口，跟着移动缩放 | 铺满全屏（可选居中，尺寸可调） |
| 触发时机 | 检测 Claude 启动后播放 | **登录后常驻**，可随时手动播放/关闭 |
| 点击穿透 | 支持 | 支持（Wayland 下用空输入区域实现） |
| 开屏音量 | 支持 | 支持 |
| 字幕 | 支持 | 支持 |

如果你希望**精确贴合窗口**，只有一条路：让 Claude 改回 X11 运行（`--ozone-platform=x11`），这样 `wmctrl` 就能读到窗口位置。但那样 Claude 在 HiDPI 屏幕上会变模糊，所以 Linux 版默认不这么做。

## 安装

### 依赖

```bash
# Debian / Ubuntu
sudo apt install cmake g++ qt6-base-dev libmpv-dev ffmpeg

# Fedora
sudo dnf install cmake gcc-c++ qt6-qtbase-devel mpv-libs-devel ffmpeg

# Arch
sudo pacman -S cmake qt6-base mpv ffmpeg
```

需要 Qt 6.2 或更新，以及 libmpv 2.x。已在 **Ubuntu 26.04 / GNOME 50 / Wayland** 上验证。

### 从源码安装

```bash
git clone https://github.com/jaydocoder/ai-boot-animations.git
cd ai-boot-animations/claude/linux
./install.sh
```

装好后：

- 程序在 `~/.local/bin/claude-boot-animation`
- 动画素材在 `~/.local/share/ClaudeBootAnimation/`
- 登录自启：`~/.config/autostart/claude-boot-animation.desktop`
- 应用菜单里出现「Claude 开机动画」

登录后托盘出现 ✨ 图标，点一下弹出设置窗口，右键是完整菜单。

### 只编译不安装

```bash
./build.sh                      # 用仓库自带动画
./build.sh 开屏.mp4 循环.mp4     # 换成自己的动画
./build/ClaudeBootAnimation --preview   # 预览一次
```

## 托盘 ✨ 能干什么

**打开 Claude 时**：播放开屏动画 / 保留循环背景，两个开关自由组合：

| 播放开屏动画 | 保留循环背景 | 效果 |
|:---:|:---:|---|
| ✅ | ✅ | 默认：先放开屏，再淡成半透明动态背景 |
| ✅ | ❌ | 只放开屏，结束就消失 |
| ❌ | ✅ | 启动直接有动态背景 |
| ❌ | ❌ | 原样启动，什么都不放 |

**两个滑块**：背景不透明度（0–80%）、开屏音量（0–100%）。拖动实时生效，正在播放的动画立刻跟着变。

**现在，在屏幕上**：播放一次开屏动画 / 开启循环背景 / 关掉动画。不用重启任何东西。

**其他**：设置窗口…、打开设置文件…、卸载 Claude 开机动画…

## 设置文件

`~/.config/ClaudeBootAnimation/config.json`，改完保存，下次播放生效：

| 键 | 默认 | 含义 |
|---|---|---|
| `playIntro` / `loopBackground` | true / true | 同菜单里的两个开关 |
| `residualAlpha` | 0.25 | 循环背景不透明度 |
| `introVolume` | 1.0 | 开屏音量（循环背景是静音的） |
| `fadeStartSeconds` | 10.767 | 开屏里开始透出界面的时间点 |
| `fadeDurationSeconds` | 1.2 | 透出界面的渐变时长 |
| `showSubtitles` | true | 是否显示字幕 |
| `subtitles` | 两句 | 字幕文字和起止秒数 |
| `subtitleFadeSeconds` | 0.5 | 字幕渐显渐隐时长 |
| `subtitleWidthRatio` | 0.6 | 字幕大小 |
| `overlayMode` | `"fullscreen"` | `fullscreen` 铺满全屏 / `center` 居中浮层 |
| `overlayWidthRatio` | 0.62 | 仅居中模式有效：宽度占屏幕比例 |
| `overlayHeightRatio` | 0.72 | 仅居中模式有效：高度占屏幕比例 |
| `cornerRadius` | 12 | 浮层圆角 |

## 命令行参数

```bash
claude-boot-animation              # 常驻，托盘图标
claude-boot-animation --settings   # 常驻并弹出设置窗口
claude-boot-animation --preview    # 预览一次开屏，播完退出
claude-boot-animation --loop-only  # 只放循环背景（Ctrl+C 退出）
claude-boot-animation --stop       # 通知已在运行的实例关掉动画
claude-boot-animation --test-skip 3  # 3 秒后模拟点「跳过」
claude-boot-animation --uninstall  # 卸载
```

## 换成自己的动画

```bash
./install.sh 你的开屏.mp4 你的循环.mp4
```

要求：

- 开屏的最后一帧能接上循环的第一帧，循环首尾相接
- 推荐 H.264 或 HEVC；音轨放在开屏文件里，循环文件不用带声音（带了也会被静音）
- 改了开屏长度，记得在设置文件里调 `fadeStartSeconds` 和字幕时间

## 卸载

```bash
./uninstall.sh
```

或者托盘 ✨ 右键 →「卸载 Claude 开机动画…」。会删除程序、素材、登录自启、设置和日志。Claude 本身从来没被改过。

## 原理与实现要点

- Qt6 建一个无边框、置顶、`WA_TranslucentBackground` 的浮层窗口，铺满设定区域
- 用 **libmpv** 的 render API 把视频帧渲染进 Qt 的 FBO（`QOpenGLWidget`），这样浮层才能做到真正的透明 + 圆角
- mpv 播放列表：先 `intro.mp4`，再 `loop.mp4`，并设 `loop-playlist=inf` 让循环段无限重复
- 进入循环帧后把窗口设为 `WA_TransparentForMouseEvents`，Wayland 下会得到空输入区域，鼠标事件直接落到下层应用
- 到 `fadeStartSeconds` 用定时器把整体不透明度渐变到 `residualAlpha`

### 三个 Linux 特有的坑（踩过了，写在这里省得再踩）

**1. `mpv_create()` 必须在 `QApplication` 构造之前调用**

libmpv 内部依赖 GLib 主循环。如果在 Qt 已经接管主线程之后再 `mpv_create()`，会稳定返回空句柄，表现为「播放器初始化失败」但看不出原因。解决办法是在 `main()` 最开头、`QApplication app(...)` **之前**建好 mpv 句柄，之后交给播放器复用。

**2. 单实例 socket 名不能带哈希**

一开始用 `qHash(路径)` 拼 socket 名，但 Qt 的字符串哈希在每次进程启动时随机化，两次运行算出不同名字，导致 `removeServer()` 清不掉旧 socket、新实例误判「已有实例在跑」而直接退出。改成固定名字后就正常了。

**3. mpv 不要强制 `gpu-api=opengl`，且不要嵌在带 `WA_TranslucentBackground` 的父窗口里**

这是最折腾的一个，症状是「整屏黑屏、什么都看不到」：

- `mpv_create()` 必须在 `QApplication` 之前（第 1 条）。这一点满足后，`initializeGL` 会正常执行、`mpv_render_context_create` 返回成功、`paintGL` 里的 `mpv_render_context_render` 也返回 0（success）——**一路绿灯，但屏幕就是黑的**。
- 原因有两层：
  1. 显式设 `gpu-api=opengl`（桌面 GL）会和 Qt 在 Wayland 下拿到的 GLES/EGL 上下文不匹配。去掉这个选项，让 mpv 自己适配。
  2. `QOpenGLWidget` 不能作为普通透明 `QWidget` 的子控件——`paintGL` 甚至不会被调用。改为让视频控件自己作为顶层无边框窗口。
- 半透明不能用 `setWindowOpacity`（Wayland 不支持）也不能用 `QGraphicsOpacityEffect`（对 GL 控件无效）。正确做法：mpv 渲染到独立的 `QOpenGLFramebufferObject`，再用 `QOpenGLTextureBlitter` 带 opacity 合成到屏幕。
- 叠加字幕时不要用 `QPainter(this)`，它会切换 framebuffer 把刚渲染的视频冲掉（表现为“有字幕就黑屏”）。要用 `QOpenGLPaintDevice` 绑定当前上下文。
- 方向问题：mpv 的 `FLIP_Y` 和 blitter 的 origin 会叠加翻转，两处只能有一处生效，否则画面上下颠倒。

## 授权

- 代码：[GPL-3.0](../../LICENSE)：可以自由使用、学习和修改；发布改过的版本时必须同样以 GPL-3.0 开源，并保留原作者署名
- 动画素材：[CC BY-NC-ND 4.0](../assets/LICENSE.md)，可以安装、分享，不能商用、不能改后再发布
- 音效：Freesound CC0 素材，见 [../CREDITS.md](../CREDITS.md)

---

**English (short):** A Linux port of the Claude boot animation. A borderless, always-on-top Qt6 overlay renders `intro.mp4` then fades into a translucent, click-through looping background, using libmpv into a Qt FBO so the overlay stays genuinely transparent. Instead of hugging the Claude window (impossible on GNOME Wayland, where apps cannot query other windows' geometry), the overlay is screen-centred with adjustable size. Install with `./install.sh`; uninstall with `./uninstall.sh`. Requires Qt 6.2+ and libmpv. Verified on Ubuntu 26.04 / GNOME 50 / Wayland. Code is GPL-3.0; animation assets are CC BY-NC-ND 4.0.
