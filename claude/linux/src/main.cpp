/*
 * Claude 开机动画 · Linux 版（GNOME / Wayland 优先）
 *
 * 原理：在屏幕上盖一个无边框、置顶、可透明的浮层窗口，用 libmpv 播放开屏动画；
 *      到设定时间点后浮层变为半透明循环背景并开启点击穿透，鼠标可直接穿透到下方应用。
 *      全程不修改 Claude 的任何文件（它装在 /usr/lib/claude-desktop，本来也不可写）。
 *
 * Copyright (C) 2026 wynneceles
 * Linux port: 2026 jaydocoder
 * 本程序以 GNU GPL v3 发布，见仓库根目录 LICENSE。
 * This program is free software under the GNU General Public License v3.
 */

#include <QApplication>
#include <QWidget>
#include <QPainter>
#include <QScreen>
#include <QTimer>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QDir>
#include <QStandardPaths>
#include <QSystemTrayIcon>
#include <QLocalServer>
#include <QLocalSocket>
#include <QDateTime>
#include <QCommandLineParser>
#include <QScrollArea>
#include <QVBoxLayout>
#include <QMenu>
#include <QAction>
#include <QOpenGLWidget>
#include <QOpenGLFunctions>
#include <QOpenGLContext>
#include <QOpenGLShaderProgram>
#include <QWidgetAction>
#include <QGraphicsOpacityEffect>
#include <QOpenGLPaintDevice>
#include <QPainterPath>
#include <QSurfaceFormat>
#include <QOpenGLFramebufferObject>
#include <QOpenGLTextureBlitter>
#include <memory>
#include <QProcess>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QLabel>
#include <QSlider>
#include <QCheckBox>
#include <QPushButton>
#include <QDialog>
#include <QGroupBox>
#include <QMessageBox>
#include <QPixmap>
#include <QIcon>
#include <QDebug>

#include <mpv/client.h>
#include <mpv/render_gl.h>

#include <functional>
#include <cmath>
#include <cstdio>

// ---------------------------------------------------------------------------
// 常量
// ---------------------------------------------------------------------------

static const char *kAppName = "ClaudeBootAnimation";
static const char *kAppTitle = "Claude 开机动画";

// 所有运行数据统一放 ~/.local/share/ClaudeBootAnimation（素材、日志都在这）
static QString appDataDir() {
    QString base = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation);
    if (base.isEmpty()) base = QDir::homePath() + "/.local/share";
    return base + "/ClaudeBootAnimation";
}

// ---------------------------------------------------------------------------
// 日志
// ---------------------------------------------------------------------------

static QFile *g_logFile = nullptr;
static int g_paintCount = 0;   // 诊断用：paintGL 调用计数
static QString logPath() {
    return appDataDir() + "/log.txt";
}

static void logInit() {
    QDir().mkpath(appDataDir());
    g_logFile = new QFile(logPath());
    if (!g_logFile->open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
        delete g_logFile;
        g_logFile = nullptr;
    }
    // 日志超过 1MB 时截断，避免无限增长
    if (g_logFile && g_logFile->size() > 1024 * 1024) {
        g_logFile->close();
        g_logFile->remove();
        g_logFile->open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text);
    }
}

static void logMsg(const QString &msg) {
    const QString line = QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss.zzz") + " " + msg;
    if (!g_logFile) return;
    g_logFile->write((line + "\n").toUtf8());
    g_logFile->flush();
}

// ---------------------------------------------------------------------------
// 配置
// ---------------------------------------------------------------------------

struct Subtitle {
    QString text;
    double start = 0;
    double end = 0;
};

struct Config {
    bool playIntro = true;          // 打开 Claude 时播放开屏动画
    bool loopBackground = true;     // 开屏后保留半透明循环背景
    double fadeStartSeconds = 10.767;   // 成片里进入循环帧的时间点
    double fadeDurationSeconds = 1.2;   // 从不透明淡到半透明用多久
    double residualAlpha = 0.25;        // 循环背景不透明度
    double introVolume = 1.0;           // 开屏音量
    bool showSubtitles = true;
    double subtitleFadeSeconds = 0.5;
    double subtitleWidthRatio = 0.6;
    // Linux 版新增
    QString overlayMode = "fullscreen";  // fullscreen = 铺满屏幕；center = 屏幕居中
    double overlayWidthRatio = 0.62;    // 居中模式下浮层宽度占屏幕比例
    double overlayHeightRatio = 0.72;   // 居中模式下浮层高度占屏幕比例
    double cornerRadius = 12;           // 浮层圆角

    QVector<Subtitle> subtitles = {
        {QStringLiteral("Under the moonlight... the blade awakens."), 4.087, 7.189},
        {QStringLiteral("I am Claude. Ask me anything."), 8.281, 11.3},
    };

    // 设置文件默认放 ~/.config/ClaudeBootAnimation/config.json
    static QString dir() {
        QString base = QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation);
        if (base.isEmpty()) base = QDir::homePath() + "/.config";
        return base + "/ClaudeBootAnimation";
    }
    static QString file() { return dir() + "/config.json"; }

    static Config load() {
        Config c;
        QFile f(file());
        if (f.open(QIODevice::ReadOnly)) {
            const QJsonObject o = QJsonDocument::fromJson(f.readAll()).object();
            f.close();
            // 老版本设置文件缺字段时用默认值补齐，不会整个作废
            auto boolean = [&](const char *k, bool def) {
                return o.contains(k) && o.value(k).isBool() ? o.value(k).toBool() : def;
            };
            auto number = [&](const char *k, double def) {
                return o.contains(k) && o.value(k).isDouble() ? o.value(k).toDouble() : def;
            };
            auto text = [&](const char *k, const QString &def) {
                return o.contains(k) && o.value(k).isString() ? o.value(k).toString() : def;
            };
            // 兼容早期只有 enabled 一个开关的设置文件
            const bool legacy = o.value("enabled").isBool() ? o.value("enabled").toBool() : c.playIntro;
            c.playIntro = boolean("playIntro", legacy);
            c.loopBackground = boolean("loopBackground", c.loopBackground);
            c.fadeStartSeconds = number("fadeStartSeconds", c.fadeStartSeconds);
            c.fadeDurationSeconds = number("fadeDurationSeconds", c.fadeDurationSeconds);
            c.residualAlpha = number("residualAlpha", c.residualAlpha);
            c.introVolume = number("introVolume", c.introVolume);
            c.showSubtitles = boolean("showSubtitles", c.showSubtitles);
            c.subtitleFadeSeconds = number("subtitleFadeSeconds", c.subtitleFadeSeconds);
            c.subtitleWidthRatio = number("subtitleWidthRatio", c.subtitleWidthRatio);
            c.overlayMode = text("overlayMode", c.overlayMode);
            c.overlayWidthRatio = number("overlayWidthRatio", c.overlayWidthRatio);
            c.overlayHeightRatio = number("overlayHeightRatio", c.overlayHeightRatio);
            c.cornerRadius = number("cornerRadius", c.cornerRadius);
            if (o.value("subtitles").isArray()) {
                QVector<Subtitle> subs;
                for (const QJsonValue &v : o.value("subtitles").toArray()) {
                    const QJsonObject s = v.toObject();
                    if (!s.contains("text")) continue;
                    subs.append({s.value("text").toString(),
                                 s.value("start").toDouble(),
                                 s.value("end").toDouble()});
                }
                if (!subs.isEmpty()) c.subtitles = subs;
            }
        }
        c.save();   // 补齐新字段
        return c;
    }

    void save() const {
        QDir().mkpath(dir());
        QJsonObject o;
        o["playIntro"] = playIntro;
        o["loopBackground"] = loopBackground;
        o["fadeStartSeconds"] = fadeStartSeconds;
        o["fadeDurationSeconds"] = fadeDurationSeconds;
        o["residualAlpha"] = residualAlpha;
        o["introVolume"] = introVolume;
        o["showSubtitles"] = showSubtitles;
        o["subtitleFadeSeconds"] = subtitleFadeSeconds;
        o["subtitleWidthRatio"] = subtitleWidthRatio;
        o["overlayMode"] = overlayMode;
        o["overlayWidthRatio"] = overlayWidthRatio;
        o["overlayHeightRatio"] = overlayHeightRatio;
        o["cornerRadius"] = cornerRadius;
        QJsonArray subs;
        for (const Subtitle &s : subtitles) {
            QJsonObject so;
            so["text"] = s.text;
            so["start"] = s.start;
            so["end"] = s.end;
            subs.append(so);
        }
        o["subtitles"] = subs;
        QFile f(file());
        if (f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            f.write(QJsonDocument(o).toJson(QJsonDocument::Indented));
            f.close();
        }
    }
};

// ---------------------------------------------------------------------------
// mpv 封装
// ---------------------------------------------------------------------------

// 关键：libmpv 依赖 GLib 主循环，必须在 QApplication 构造之前 mpv_create()，
// 否则会在 Qt 进程里返回空句柄。这里保存提前创建的句柄。
static mpv_handle *g_earlyMpv = nullptr;

class MpvPlayer : public QObject {
public:
    mpv_handle *mpv = nullptr;
    QTimer *pump = nullptr;

    std::function<void(double)> onTime;
    std::function<void()> onEndFile;
    std::function<void(const QString &)> onError;

    bool ok() const { return mpv != nullptr; }

    bool init(const QString &introPath, const QString &loopPath) {
        // 复用 main 里提前创建好的句柄（没有则兜底再建一次）
        mpv = g_earlyMpv ? g_earlyMpv : mpv_create();
        g_earlyMpv = nullptr;
        if (!mpv) {
            logMsg(QStringLiteral("mpv_create 返回空，无法播放"));
            return false;
        }
        // 把 mpv 渲染到我们提供的 FBO 里，浮层才能做到透明 + 圆角
        mpv_set_option_string(mpv, "vo", "libmpv");
        // 不强制 gpu-api：Qt 的 QOpenGLWidget 在 Wayland 下给的可能正是 GLES/EGL，
        // 硬指定桌面 GL 会让上下文不匹配 —— 渲染返回成功但屏幕全黑。
        mpv_set_option_string(mpv, "hwdec", "auto-safe");
        mpv_set_option_string(mpv, "keep-open", "yes");
        mpv_set_option_string(mpv, "idle", "yes");
        mpv_set_option_string(mpv, "audio-display", "no");
        mpv_set_option_string(mpv, "osc", "no");
        mpv_set_option_string(mpv, "input-default-bindings", "no");
        mpv_set_option_string(mpv, "input-vo-keyboard", "no");
        mpv_set_option_string(mpv, "terminal", "no");
        mpv_set_option_string(mpv, "msg-level", "all=error");
        mpv_set_option_string(mpv, "loop-playlist", "inf");
        mpv_set_option_string(mpv, "video-unscaled", "no");
        mpv_set_option_string(mpv, "panscan", "0.0");

        const int rc = mpv_initialize(mpv);
        if (rc < 0) {
            logMsg(QStringLiteral("mpv_initialize 失败：%1 (%2)").arg(rc).arg(mpv_error_string(rc)));
            mpv_terminate_destroy(mpv);
            mpv = nullptr;
            return false;
        }

        // 播放列表：先开屏，后循环背景。loop-playlist=inf 让循环段永远重复
        const QByteArray intro = QFile::encodeName(introPath);
        const QByteArray loop = QFile::encodeName(loopPath);
        const char *cmdIntro[] = {"loadfile", intro.constData(), "replace", nullptr};
        const int r1 = mpv_command(mpv, cmdIntro);
        const char *cmdLoop[] = {"loadfile", loop.constData(), "append", nullptr};
        const int r2 = mpv_command(mpv, cmdLoop);
        logMsg(QStringLiteral("loadfile 返回 %1 / %2").arg(r1).arg(r2));
        if (r1 < 0) {
            logMsg(QStringLiteral("开屏 loadfile 失败：%1").arg(mpv_error_string(r1)));
            mpv_terminate_destroy(mpv);
            mpv = nullptr;
            return false;
        }
        pump = new QTimer(this);
        pump->setInterval(16);
        QObject::connect(pump, &QTimer::timeout, this, [this] { drainEvents(); });
        pump->start();
        return true;
    }

    void drainEvents() {
        if (!mpv) return;
        while (true) {
            mpv_event *e = mpv_wait_event(mpv, 0);
            if (!e || e->event_id == MPV_EVENT_NONE) break;
            switch (e->event_id) {
            case MPV_EVENT_END_FILE: {
                auto *ef = static_cast<mpv_event_end_file *>(e->data);
                // 正常播完（EOF）不报错；只有真正的失败才提示
                if (ef && ef->reason == MPV_END_FILE_REASON_ERROR && onError) {
                    onError(QString::fromUtf8(mpv_error_string(ef->error)));
                }
                if (onEndFile) onEndFile();
                break;
            }
            case MPV_EVENT_PROPERTY_CHANGE: {
                auto *p = static_cast<mpv_event_property *>(e->data);
                if (p && p->name && qstrcmp(p->name, "time-pos") == 0 &&
                    p->format == MPV_FORMAT_DOUBLE && onTime) {
                    onTime(*static_cast<double *>(p->data));
                }
                break;
            }
            default:
                break;
            }
        }
    }

    void observeTimePos() {
        if (!mpv) return;
        mpv_observe_property(mpv, 1, "time-pos", MPV_FORMAT_DOUBLE);
    }

    void setVolume(double v) {
        if (!mpv) return;
        double vol = v < 0 ? 0 : (v > 1 ? 1 : v);
        mpv_set_property(mpv, "volume", MPV_FORMAT_DOUBLE, &vol);
    }

    void setMute(bool m) {
        if (!mpv) return;
        mpv_set_property_string(mpv, "mute", m ? "yes" : "no");
    }

    /// 跳到播放列表第 index 项（0 = 开屏，1 = 循环背景）
    void playIndex(int index) {
        if (!mpv) return;
        char idx[16];
        snprintf(idx, sizeof(idx), "%d", index);
        const char *cmd[] = {"playlist-play-index", idx, nullptr};
        mpv_command(mpv, cmd);
    }

    void seek(double seconds) {
        if (!mpv) return;
        char buf[32];
        snprintf(buf, sizeof(buf), "%.3f", seconds);
        const char *cmd[] = {"seek", buf, "absolute", nullptr};
        mpv_command(mpv, cmd);
    }

    void setPause(bool p) {
        if (!mpv) return;
        mpv_set_property_string(mpv, "pause", p ? "yes" : "no");
    }

    void destroy() {
        if (pump) { pump->stop(); }
        if (mpv) {
            mpv_terminate_destroy(mpv);
            mpv = nullptr;
        }
    }
};

// ---------------------------------------------------------------------------
// 视频渲染控件：把 mpv 的帧画进自己的 FBO，交给 Qt 合成
// ---------------------------------------------------------------------------

class MpvWidget : public QOpenGLWidget, protected QOpenGLFunctions {
public:
    explicit MpvWidget(QWidget *parent = nullptr) : QOpenGLWidget(parent) {
        setAttribute(Qt::WA_TransparentForMouseEvents);
        setAttribute(Qt::WA_NoSystemBackground);
    }

    ~MpvWidget() override {
        if (renderCtx) {
            makeCurrent();
            mpv_render_context_free(renderCtx);
            renderCtx = nullptr;
            doneCurrent();
        }
    }

    MpvPlayer *player = nullptr;
    bool firstFrameShown = false;
    double overlayAlpha = 1.0;   // 浮层整体不透明度（循环背景约 0.25）
    QString subtitle;            // 当前字幕文字
    double subtitleAlpha = 0.0;  // 字幕透明度
    double subtitleWidthRatio = 0.6;
    std::function<void()> onFirstFrame;

protected:
    void initializeGL() override {
        initializeOpenGLFunctions();
        if (!player || !player->mpv) return;
        logMsg(QStringLiteral("initializeGL 开始"));

        mpv_opengl_init_params gl_init{};
        gl_init.get_proc_address = [](void *ctx, const char *name) -> void * {
            auto *w = static_cast<MpvWidget *>(ctx);
            if (!w) return nullptr;
            auto *c = w->context();
            if (!c) return nullptr;
            return reinterpret_cast<void *>(c->getProcAddress(QByteArray(name)));
        };
        gl_init.get_proc_address_ctx = this;

        int advanced = 1;
        mpv_render_param params[] = {
            {MPV_RENDER_PARAM_API_TYPE, const_cast<char *>(MPV_RENDER_API_TYPE_OPENGL)},
            {MPV_RENDER_PARAM_OPENGL_INIT_PARAMS, &gl_init},
            {MPV_RENDER_PARAM_ADVANCED_CONTROL, &advanced},
            {MPV_RENDER_PARAM_INVALID, nullptr},
        };
        const int rc = mpv_render_context_create(&renderCtx, player->mpv, params);
        if (rc < 0) {
            logMsg(QStringLiteral("mpv 渲染上下文创建失败：%1").arg(mpv_error_string(rc)));
            renderCtx = nullptr;
            return;
        }
        logMsg(QStringLiteral("mpv 渲染上下文创建成功"));
        mpv_render_context_set_update_callback(renderCtx, [](void *ctx) {
            auto *w = static_cast<MpvWidget *>(ctx);
            QMetaObject::invokeMethod(w, [w] { w->update(); }, Qt::QueuedConnection);
        }, this);
    }

    void paintGL() override {
        if (!renderCtx) {
            logMsg(QStringLiteral("paintGL 时渲染上下文未就绪"));
            return;
        }
        // 先把当前 FBO 清成透明，再让 mpv 覆盖上去
        glClearColor(0, 0, 0, 0);
        glClear(GL_COLOR_BUFFER_BIT);

        GLint fbo = 0;
        glGetIntegerv(GL_FRAMEBUFFER_BINDING, &fbo);
        int w = static_cast<int>(width() * devicePixelRatioF());
        int h = static_cast<int>(height() * devicePixelRatioF());
        if (w <= 0 || h <= 0) {
            logMsg(QStringLiteral("paintGL 尺寸非法 %1x%2").arg(w).arg(h));
            return;
        }

        // 半透明实现：mpv 先画到自己的 FBO 纹理，再按 overlayAlpha 用
        // QOpenGLTextureBlitter 合成到窗口帧缓冲。
        // 原因：Wayland 不支持 setWindowOpacity，QGraphicsOpacityEffect 对 GL 控件也无效。
        if (!videoFbo || videoFbo->width() != w || videoFbo->height() != h) {
            videoFbo.reset(new QOpenGLFramebufferObject(w, h, QOpenGLFramebufferObject::CombinedDepthStencil));
            blitter.reset(new QOpenGLTextureBlitter());
            blitter->create();
        }
        mpv_opengl_fbo mpfbo{static_cast<int>(videoFbo->handle()), w, h, 0};
        // 注意：这里不翻转（flipY=0），方向统一交给下面的 blitter 处理，
        // 两处都翻转会导致画面上下颠倒。
        int flipY = 0;
        mpv_render_param params[] = {
            {MPV_RENDER_PARAM_OPENGL_FBO, &mpfbo},
            {MPV_RENDER_PARAM_FLIP_Y, &flipY},
            {MPV_RENDER_PARAM_INVALID, nullptr},
        };
        const int rc = mpv_render_context_render(renderCtx, params);
        Q_UNUSED(fbo);

        // 合成到窗口帧缓冲，按 overlayAlpha 决定不透明度
        if (blitter && videoFbo) {
            glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(fbo));
            glViewport(0, 0, w, h);
            glEnable(GL_BLEND);
            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
            blitter->bind();
            blitter->setOpacity(static_cast<float>(overlayAlpha));
            QMatrix4x4 target;
            target.setToIdentity();
            blitter->blit(videoFbo->texture(), target, QOpenGLTextureBlitter::OriginTopLeft);
            blitter->release();
            glDisable(GL_BLEND);
        }
        if (rc < 0 && g_paintCount < 3) {
            // 只在失败时记日志，避免持续刷盘
            logMsg(QStringLiteral("mpv 渲染失败：%1").arg(mpv_error_string(rc)));
            g_paintCount++;
        }
        // 字幕叠加：必须把 QPainter 绑定到当前上下文和同一个 FBO，
        // 否则 QPainter 会切换 framebuffer，把刚画好的视频内容冲掉（表现为黑屏）。
        if (!subtitle.isEmpty() && subtitleAlpha > 0.001) {
            QOpenGLPaintDevice device(w, h);
            QPainter painter(&device);
            painter.scale(static_cast<qreal>(w) / width(), static_cast<qreal>(h) / height());
            painter.setRenderHint(QPainter::Antialiasing, true);
            painter.setRenderHint(QPainter::TextAntialiasing, true);
            const double size = std::max(18.0, std::min(width() * subtitleWidthRatio / 15.5, height() * 0.09));
            QFont f = painter.font();
            f.setPointSizeF(size);
            f.setItalic(true);
            f.setFamily(QStringLiteral("Georgia"));
            painter.setFont(f);
            painter.setOpacity(subtitleAlpha);
            QFontMetricsF fm(f);
            const double tw = fm.horizontalAdvance(subtitle);
            const QPointF baseline((width() - tw) / 2.0,
                                   height() * 0.85 - size * 0.15 + fm.ascent() / 2.5);
            QPainterPath path;
            path.addText(baseline, f, subtitle);
            QPen outline(QColor(0, 0, 0, 220));
            outline.setWidthF(std::max(3.0, size * 0.10));
            outline.setJoinStyle(Qt::RoundJoin);
            painter.strokePath(path, outline);
            painter.fillPath(path, Qt::white);
            painter.end();
        }

        if (!firstFrameShown) {
            firstFrameShown = true;
            if (onFirstFrame) onFirstFrame();
        }
    }

private:
    mpv_render_context *renderCtx = nullptr;
    std::unique_ptr<QOpenGLFramebufferObject> videoFbo;
    std::unique_ptr<QOpenGLTextureBlitter> blitter;
};

// ---------------------------------------------------------------------------
// 浮层窗口
// ---------------------------------------------------------------------------

class OverlayWindow : public QWidget {
public:
    explicit OverlayWindow(QWidget *parent = nullptr) : QWidget(parent) {
        setWindowFlags(Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint | Qt::Tool);
        // 注意：这里不能设 WA_TranslucentBackground。
        // 在 GNOME Wayland 下，QOpenGLWidget 作为带透明属性的 QWidget 的子控件时，
        // paintGL 可能不被调用、GL 内容也不会被合成，表现为整屏黑。
        // 改为让视频控件自己作为顶层窗口（见 video），由它负责绘制。
        setAttribute(Qt::WA_ShowWithoutActivating);
        video = new MpvWidget();
        video->setWindowFlags(Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint | Qt::Tool);
        video->setAttribute(Qt::WA_ShowWithoutActivating);
        video->setAttribute(Qt::WA_TransparentForMouseEvents);
    }

    MpvWidget *video = nullptr;
    QGraphicsOpacityEffect *videoOpacity = nullptr;
    double radius = 12;
    bool clickThrough = false;
    double bgAlpha = 1.0;
    QString subtitle;
    double subtitleAlpha = 0.0;
    double subtitleWidthRatio = 0.6;

    void setClickThrough(bool on) {
        clickThrough = on;
        // Wayland 下让输入区域为空 = 鼠标穿透到下方应用
        video->setAttribute(Qt::WA_TransparentForMouseEvents, true);
        setAttribute(Qt::WA_TransparentForMouseEvents, on);
        update();
    }

    void setBgAlpha(double a) {
        bgAlpha = std::max(0.0, std::min(1.0, a));
        // Wayland 不支持 setWindowOpacity，改用 QGraphicsOpacityEffect 作用于视频控件
        video->overlayAlpha = bgAlpha;
        if (!videoOpacity) {
            videoOpacity = new QGraphicsOpacityEffect(video);
            video->setGraphicsEffect(videoOpacity);
        }
        videoOpacity->setOpacity(bgAlpha);
        update();
    }

    void setSubtitle(const QString &text, double alpha) {
        subtitle = text;
        subtitleAlpha = alpha;
        // 字幕由视频控件在 GL 之后叠加绘制（容器窗口不显示）
        video->subtitle = text;
        video->subtitleAlpha = alpha;
        video->subtitleWidthRatio = subtitleWidthRatio;
        video->update();
    }

    void setGeometryAll(const QRect &geo) {
        video->setGeometry(geo);
        video->resize(geo.size());
    }

    void showAll() {
        // 只显示视频窗口。容器窗口不显示，否则屏幕上会多出空白窗口。
        video->show();
        video->raise();
    }

    void hideAll() {
        video->hide();
    }

    void updateGeometryFollow(const QRect &geo) {
        if (video->geometry() != geo) {
            video->setGeometry(geo);
            video->resize(geo.size());
        }
    }

    void paintEvent(QPaintEvent *) override {
        // 父窗口本身只画字幕（半透明叠加），不参与视频合成
        if (subtitle.isEmpty() || subtitleAlpha <= 0.001) return;
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);
        p.setRenderHint(QPainter::TextAntialiasing, true);
        drawSubtitles(p);
    }

private:
    void drawSubtitles(QPainter &p) {
        if (subtitle.isEmpty() || subtitleAlpha <= 0.001) return;
        // 字号按窗口宽度算；窗口特别扁时再按高度限一下
        double size = std::max(18.0, std::min(width() * subtitleWidthRatio / 15.5, height() * 0.09));
        QFont f = p.font();
        f.setPointSizeF(size);
        f.setItalic(true);
        f.setFamily(QStringLiteral("Georgia"));
        p.setFont(f);
        p.setOpacity(subtitleAlpha);
        QFontMetricsF fm(f);
        const double tw = fm.horizontalAdvance(subtitle);
        const QPointF baseline((width() - tw) / 2.0,
                               height() * 0.85 - size * 0.15 + fm.ascent() / 2.5);
        QPainterPath path;
        path.addText(baseline, f, subtitle);
        // 先描黑边再填白：亮背景上也看得清
        QPen outline(QColor(0, 0, 0, 220));
        outline.setWidthF(std::max(3.0, size * 0.10));
        outline.setJoinStyle(Qt::RoundJoin);
        p.strokePath(path, outline);
        p.fillPath(path, Qt::white);
    }
};
// 设置窗口
// ---------------------------------------------------------------------------

class SettingsDialog : public QDialog {
public:
    SettingsDialog(std::function<const Config &()> getCfg,
                   std::function<void(std::function<void(Config &)>)> apply)
        : cfgGetter(std::move(getCfg)), applyChange(std::move(apply)) {
        setWindowTitle(QString::fromUtf8(kAppTitle));
        setMinimumWidth(420);

        auto *root = new QVBoxLayout(this);

        auto *when = new QGroupBox(QStringLiteral("打开 Claude 时"), this);
        auto *whenLay = new QVBoxLayout(when);
        ckIntro = new QCheckBox(QStringLiteral("播放开屏动画"), when);
        ckLoop = new QCheckBox(QStringLiteral("保留循环背景"), when);
        whenLay->addWidget(ckIntro);
        whenLay->addWidget(ckLoop);
        root->addWidget(when);

        auto *look = new QGroupBox(QStringLiteral("外观"), this);
        auto *grid = new QGridLayout(look);
        int row = 0;
        slAlpha = addSlider(grid, row, QStringLiteral("背景不透明度"), 0, 0.8);
        slVolume = addSlider(grid, row, QStringLiteral("开屏音量"), 0, 1.0);
        slWidth = addSlider(grid, row, QStringLiteral("浮层宽度"), 0.3, 1.0);
        slHeight = addSlider(grid, row, QStringLiteral("浮层高度"), 0.3, 1.0);
        // 全屏模式下宽高无意义，直接禁用并说明
        fullscreenHint = new QLabel(QStringLiteral("当前为全屏模式，宽度/高度不生效。\n"
                                                   "想改成居中浮层：在设置文件里把 overlayMode 改成 \"center\"。"),
                                    this);
        fullscreenHint->setWordWrap(true);
        fullscreenHint->setStyleSheet("color: palette(mid); font-size: 11px;");
        grid->addWidget(fullscreenHint, row, 0, 1, 3);
        row++;
        root->addWidget(look);

        auto *nowBox = new QGroupBox(QStringLiteral("现在，在屏幕上"), this);
        auto *nowLay = new QVBoxLayout(nowBox);
        btnPreview = new QPushButton(QStringLiteral("播放一次开屏动画"), nowBox);
        btnLoopOnly = new QPushButton(QStringLiteral("开启循环背景"), nowBox);
        btnStop = new QPushButton(QStringLiteral("关掉动画 / 背景"), nowBox);
        nowLay->addWidget(btnPreview);
        nowLay->addWidget(btnLoopOnly);
        nowLay->addWidget(btnStop);
        root->addWidget(nowBox);

        auto *paths = new QLabel(
            QStringLiteral("设置文件：%1\n日志：%2\n\nLinux 版说明：GNOME Wayland 下程序拿不到其它应用窗口的位置，\n所以浮层是屏幕居中 / 铺满全屏，而不是精确贴住 Claude 窗口。")
                .arg(Config::file(), logPath()),
            this);
        paths->setWordWrap(true);
        paths->setStyleSheet("color: palette(mid); font-size: 11px;");
        root->addWidget(paths);

        auto *btns = new QHBoxLayout();
        auto *openCfg = new QPushButton(QStringLiteral("打开设置文件"), this);
        auto *close = new QPushButton(QStringLiteral("关闭"), this);
        btns->addWidget(openCfg);
        btns->addStretch();
        btns->addWidget(close);
        root->addLayout(btns);

        connect(close, &QPushButton::clicked, this, &QDialog::close);
        connect(openCfg, &QPushButton::clicked, this, [] {
            QProcess::startDetached("xdg-open", {Config::file()});
        });
        refresh();
    }

    QPushButton *btnPreview = nullptr;
    QPushButton *btnLoopOnly = nullptr;
    QPushButton *btnStop = nullptr;

    void refresh() {
        const Config &c = cfgGetter();
        ckIntro->setChecked(c.playIntro);
        ckLoop->setChecked(c.loopBackground);
        slAlpha->setValue(static_cast<int>(c.residualAlpha * 100));
        slVolume->setValue(static_cast<int>(c.introVolume * 100));
        slWidth->setValue(static_cast<int>(c.overlayWidthRatio * 100));
        slHeight->setValue(static_cast<int>(c.overlayHeightRatio * 100));
        // 全屏模式：宽度/高度滑块无意义，禁用它并显示提示
        const bool full = (c.overlayMode == "fullscreen");
        slWidth->setEnabled(!full);
        slHeight->setEnabled(!full);
        fullscreenHint->setVisible(full);
    }

private:
    std::function<const Config &()> cfgGetter;
    std::function<void(std::function<void(Config &)>)> applyChange;
    QCheckBox *ckIntro = nullptr;
    QCheckBox *ckLoop = nullptr;
    QSlider *slAlpha = nullptr;
    QSlider *slVolume = nullptr;
    QSlider *slWidth = nullptr;
    QSlider *slHeight = nullptr;
    QLabel *fullscreenHint = nullptr;

    QSlider *addSlider(QGridLayout *grid, int &row, const QString &title, double lo, double hi) {
        auto *label = new QLabel(title, this);
        auto *slider = new QSlider(Qt::Horizontal, this);
        slider->setRange(static_cast<int>(lo * 100), static_cast<int>(hi * 100));
        auto *value = new QLabel(this);
        value->setMinimumWidth(46);
        value->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        grid->addWidget(label, row, 0);
        grid->addWidget(slider, row, 1);
        grid->addWidget(value, row, 2);
        row++;

        auto updateLabel = [slider, value] {
            value->setText(QString::number(slider->value()) + "%");
        };
        updateLabel();

        connect(slider, &QSlider::valueChanged, this, [this, updateLabel, title](int v) {
            updateLabel();
            const double d = v / 100.0;
            applyChange([title, d](Config &c) {
                if (title == QStringLiteral("背景不透明度")) c.residualAlpha = d;
                else if (title == QStringLiteral("开屏音量")) c.introVolume = d;
                else if (title == QStringLiteral("浮层宽度")) c.overlayWidthRatio = d;
                else if (title == QStringLiteral("浮层高度")) c.overlayHeightRatio = d;
            });
        });
        return slider;
    }
};

// ---------------------------------------------------------------------------
// 主控：托盘、配置、播放流程
// ---------------------------------------------------------------------------

enum class Phase { Idle, Intro, Revealed, Ending };

class Controller : public QObject {
public:
    explicit Controller(QObject *parent = nullptr) : QObject(parent) {}

    Config config = Config::load();
    OverlayWindow *overlay = nullptr;
    MpvPlayer player;
    Phase phase = Phase::Idle;
    QString introPath, loopPath;

    double loopStartSeconds = 0;   // 开屏视频总长 = 循环起点
    bool inLoopAudio = false;
    QTimer *followTimer = nullptr;
    QTimer *volumeRamp = nullptr;
    bool exitWhenDone = false;

    std::function<void()> onFinished;

    // ---- 路径 ----

    static QString dataDir() { return appDataDir(); }
    bool resolveMedia() {
        const QString dir = dataDir();
        introPath = dir + "/intro.mp4";
        loopPath = dir + "/loop.mp4";
        return QFile::exists(introPath) && QFile::exists(loopPath);
    }

    void probeIntroDuration() {
        QProcess ff;
        ff.start("ffprobe", {"-v", "error", "-show_entries", "format=duration",
                             "-of", "default=nw=1:nk=1", introPath});
        if (ff.waitForFinished(4000)) {
            bool ok = false;
            const double d = QString::fromUtf8(ff.readAllStandardOutput()).trimmed().toDouble(&ok);
            if (ok && d > 0.2) loopStartSeconds = d;
        }
        // ffprobe 不可用时用配置里的 fadeStartSeconds 兜底
        if (loopStartSeconds <= 0) loopStartSeconds = config.fadeStartSeconds;
        logMsg(QStringLiteral("开屏时长 %1 秒").arg(loopStartSeconds));
    }

    // ---- 浮层几何 ----

    QRect overlayGeometry() {
        QScreen *screen = QGuiApplication::primaryScreen();
        const QRect g = screen ? screen->geometry() : QRect(0, 0, 1920, 1080);
        if (config.overlayMode == "fullscreen") return g;
        const int w = static_cast<int>(g.width() * config.overlayWidthRatio);
        const int h = static_cast<int>(g.height() * config.overlayHeightRatio);
        return QRect(g.x() + (g.width() - w) / 2, g.y() + (g.height() - h) / 2, w, h);
    }

    // ---- 播放 ----

    bool start(bool withIntro, bool exitWhenDoneFlag) {
        if (phase != Phase::Idle) teardown();
        if (!resolveMedia()) {
            logMsg(QStringLiteral("找不到动画素材：%1").arg(dataDir()));
            return false;
        }
        probeIntroDuration();
        exitWhenDone = exitWhenDoneFlag;

        overlay = new OverlayWindow();
        overlay->radius = config.cornerRadius;
        overlay->subtitleWidthRatio = config.subtitleWidthRatio;
        const QRect geo = overlayGeometry();
        // 视频控件是独立顶层窗口，几何直接给全屏矩形
        overlay->setGeometryAll(geo);
        overlay->video->onFirstFrame = [this] {
            logMsg(QStringLiteral("第一帧出画面"));
        };
        logMsg(QStringLiteral("浮层几何 %1x%2 @%3,%4")
                   .arg(geo.width()).arg(geo.height()).arg(geo.x()).arg(geo.y()));

        if (!player.init(introPath, loopPath)) {
            logMsg(QStringLiteral("mpv 初始化失败"));
            delete overlay;
            overlay = nullptr;
            return false;
        }
        player.setVolume(withIntro ? config.introVolume : 0);
        overlay->video->player = &player;
        overlay->showAll();

        player.onTime = [this](double t) { tickTime(t); };
        player.observeTimePos();

        if (!withIntro) {
            phase = Phase::Revealed;
            inLoopAudio = true;
            player.playIndex(1);        // 直接进循环背景
            player.setVolume(0);
            overlay->setClickThrough(true);
            overlay->setBgAlpha(0);
            fadeTo(config.residualAlpha, config.fadeDurationSeconds);
        } else {
            phase = Phase::Intro;
            overlay->setClickThrough(false);   // 开屏阶段拦住点击
            overlay->setBgAlpha(1.0);
            fadeTo(1.0, 0.001);
        }
        logMsg(withIntro ? QStringLiteral("开始播放开屏动画") : QStringLiteral("直接开启循环背景"));

        followTimer = new QTimer(this);
        QObject::connect(followTimer, &QTimer::timeout, this, [this] { followMode(); });
        followTimer->start(500);
        return true;
    }

    /// 居中浮层没有窗口可跟随，这里只做“素材可用性/屏幕变化”的兜底重建
    void followMode() {
        if (phase == Phase::Idle || !overlay) return;
        const QRect want = overlayGeometry();
        if (overlay->geometry() != want) overlay->setGeometry(want);
    }

    void tickTime(double t) {
        if (phase == Phase::Idle) return;
        // 字幕
        if (config.showSubtitles && !config.subtitles.isEmpty() &&
            (phase == Phase::Intro || phase == Phase::Revealed)) {
            const Subtitle *hit = nullptr;
            for (const Subtitle &s : config.subtitles) {
                if (t >= s.start && t <= s.end) { hit = &s; break; }
            }
            if (hit) {
                const double fade = std::max(0.01, config.subtitleFadeSeconds);
                const double a = std::min({1.0, (t - hit->start) / fade, (hit->end - t) / fade});
                overlay->setSubtitle(hit->text, std::max(0.0, a));
            } else {
                overlay->setSubtitle(QString(), 0);
            }
        }
        // 到时间点：进入循环帧，透出界面
        if (phase == Phase::Intro && t >= config.fadeStartSeconds) {
            reveal(false);
        }
        // 开屏音轨放完、进入循环段：静音
        if (!inLoopAudio && t >= loopStartSeconds - 0.05) {
            inLoopAudio = true;
            rampVolume(0, 1.5);
        }
    }

    void reveal(bool quick) {
        if (phase != Phase::Intro) return;
        phase = Phase::Revealed;
        overlay->setClickThrough(true);   // 透出后点击直接落到下方应用
        overlay->setSubtitle(QString(), 0);
        if (!config.loopBackground) {
            logMsg(QStringLiteral("进入循环帧，循环背景已关，淡出"));
            finish(false);
            return;
        }
        logMsg(QStringLiteral("进入循环帧，透出界面"));
        fadeTo(config.residualAlpha, quick ? 0.9 : config.fadeDurationSeconds);
    }

    void fadeTo(double target, double seconds) {
        const double from = overlay->bgAlpha;
        const int steps = std::max(1, static_cast<int>(seconds * 60));
        int i = 0;
        auto *t = new QTimer(this);
        QObject::connect(t, &QTimer::timeout, this, [this, t, from, target, steps, i]() mutable {
            i++;
            const double k = static_cast<double>(i) / steps;
            if (!overlay) { t->stop(); t->deleteLater(); return; }
            overlay->setBgAlpha(from + (target - from) * k);
            if (i >= steps) { t->stop(); t->deleteLater(); }
        });
        t->start(1000 / 60);
    }

    void rampVolume(double target, double seconds) {
        const double from = config.introVolume;
        const int steps = std::max(1, static_cast<int>(seconds * 30));
        int i = 0;
        if (volumeRamp) { volumeRamp->stop(); volumeRamp->deleteLater(); }
        volumeRamp = new QTimer(this);
        QObject::connect(volumeRamp, &QTimer::timeout, this, [this, from, target, steps, i]() mutable {
            i++;
            const double k = static_cast<double>(i) / steps;
            player.setVolume(from + (target - from) * k);
            if (i >= steps) { volumeRamp->stop(); }
        });
        volumeRamp->start(1000 / 30);
    }

    void applyConfig(const Config &c) {
        config = c;
        if (phase == Phase::Revealed && overlay) overlay->setBgAlpha(c.residualAlpha);
        if (phase == Phase::Intro) player.setVolume(inLoopAudio ? 0 : c.introVolume);
    }

    void finish(bool fast) {
        if (phase == Phase::Idle || phase == Phase::Ending) return;
        phase = Phase::Ending;
        logMsg(fast ? QStringLiteral("关闭（跳过）") : QStringLiteral("淡出"));
        const double t0 = overlay ? overlay->bgAlpha : 0;
        Q_UNUSED(t0);
        rampVolume(0, fast ? 0.4 : 1.2);
        const int d = static_cast<int>((fast ? 0.4 : 1.2) * 1000);
        QTimer::singleShot(d, this, [this] { teardown(); });
    }

    void teardown() {
        if (phase == Phase::Idle) return;
        phase = Phase::Idle;
        if (followTimer) { followTimer->stop(); followTimer->deleteLater(); followTimer = nullptr; }
        if (volumeRamp) { volumeRamp->stop(); volumeRamp->deleteLater(); volumeRamp = nullptr; }
        player.onTime = nullptr;
        player.destroy();
        if (overlay) { overlay->hideAll(); overlay->deleteLater(); overlay = nullptr; }
        logMsg(QStringLiteral("播放结束"));
        const bool wasExit = exitWhenDone;
        exitWhenDone = false;
        if (onFinished) onFinished();
        if (wasExit) QTimer::singleShot(200, qApp, &QApplication::quit);
    }
};

// ---------------------------------------------------------------------------
// 入口
// ---------------------------------------------------------------------------

static void installAutostart(bool enable) {
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation)
                        + "/autostart";
    const QString path = dir + "/claude-boot-animation.desktop";
    if (!enable) { QFile::remove(path); return; }
    QDir().mkpath(dir);
    const QString exe = QCoreApplication::applicationFilePath();
    QFile f(path);
    if (f.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
        f.write(QStringLiteral(
                    "[Desktop Entry]\nType=Application\nName=Claude 开机动画\n"
                    "Comment=Claude 桌面端开屏动画与循环背景\nExec=%1\n"
                    "X-GNOME-Autostart-enabled=true\nTerminal=false\nNoDisplay=false\n")
                    .arg(exe).toUtf8());
        f.close();
    }
}

static QIcon makeTrayIcon() {
    QPixmap pm(64, 64);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(255, 214, 102));
    // 简单的四角星 ✨
    QPainterPath star;
    auto pt = [](double deg, double r) {
        const double a = deg * M_PI / 180.0;
        return QPointF(32 + r * std::cos(a), 32 + r * std::sin(a));
    };
    star.moveTo(pt(-90, 30));
    for (int i = 1; i < 8; i++) {
        star.lineTo(pt(-90 + i * 45, (i % 2) ? 9 : 30));
    }
    star.closeSubpath();
    p.drawPath(star);
    return QIcon(pm);
}

int main(int argc, char **argv) {
    // 必须在 QApplication 之前：优先原生 Wayland，浮层透明与点击穿透在 Wayland 下表现最好
    if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM")) {
        qputenv("QT_QPA_PLATFORM", "wayland;xcb");
    }
    // 关键顺序：先建 mpv 句柄，再建 QApplication。
    // libmpv 依赖 GLib 主循环，反过来（Qt 先起）mpv_create() 会返回空。
    g_earlyMpv = mpv_create();

    QApplication app(argc, argv);
    QApplication::setApplicationName(QString::fromUtf8(kAppName));
    QApplication::setApplicationDisplayName(QString::fromUtf8(kAppTitle));
    QApplication::setDesktopFileName("claude-boot-animation");
    QApplication::setQuitOnLastWindowClosed(false);

    logInit();

    QCommandLineParser parser;
    parser.setApplicationDescription(QString::fromUtf8(kAppTitle));
    parser.addHelpOption();
    QCommandLineOption optPreview("preview", QStringLiteral("在当前屏幕上预览一次开屏动画后退出"));
    QCommandLineOption optLoop("loop-only", QStringLiteral("只开循环背景"));
    QCommandLineOption optSettings("settings", QStringLiteral("启动后弹出设置窗口"));
    QCommandLineOption optStop("stop", QStringLiteral("关掉正在播放的动画"));
    QCommandLineOption optTestSkip("test-skip", QStringLiteral("N 秒后模拟跳过"), "seconds");
    QCommandLineOption optUninstall("uninstall", QStringLiteral("卸载"));
    parser.addOption(optPreview);
    parser.addOption(optLoop);
    parser.addOption(optSettings);
    parser.addOption(optStop);
    parser.addOption(optTestSkip);
    parser.addOption(optUninstall);
    parser.process(app);

    // ---- 卸载 ----
    if (parser.isSet(optUninstall)) {
        installAutostart(false);
        QDir(Config::dir()).removeRecursively();
        QDir(QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + "/ClaudeBootAnimation")
            .removeRecursively();
        const QString home = QDir::homePath();
        QDir(home + "/.local/bin").remove("claude-boot-animation");
        QDir(home + "/.local/share/applications").remove("claude-boot-animation.desktop");
        QFile::remove(logPath());
        QMessageBox::information(nullptr, QString::fromUtf8(kAppTitle),
                                 QStringLiteral("已卸载。Claude 本身从未被修改。"));
        return 0;
    }

    // ---- 单实例：用本地 socket 把参数转给已在运行的实例 ----
    // socket 名字必须固定（不能用 qHash，跨进程不稳定），放运行时目录
    QString runtimeDir = qEnvironmentVariable("XDG_RUNTIME_DIR");
    if (runtimeDir.isEmpty()) runtimeDir = QDir::tempPath();
    const QString sockName = runtimeDir + "/claude-boot-animation.sock";
    bool isPrimary = true;
    {
        QLocalSocket probe;
        probe.connectToServer(sockName);
        if (probe.waitForConnected(200)) {
            isPrimary = false;
            const QString arg = parser.isSet(optStop) ? "stop"
                                : parser.isSet(optPreview) ? "preview"
                                : parser.isSet(optLoop) ? "loop"
                                : "settings";
            probe.write(arg.toUtf8() + "\n");
            probe.flush();
            probe.waitForBytesWritten(300);
            probe.disconnectFromServer();
        }
    }
    if (!isPrimary) return 0;

    Controller ctrl;
    bool ok = ctrl.resolveMedia();
    if (!ok) {
        // 素材缺失时如果是常驻启动，仍然留在托盘里给提示
        logMsg(QStringLiteral("素材缺失：%1").arg(Controller::dataDir()));
    }

    QLocalServer server;
    QLocalServer::removeServer(sockName);
    server.listen(sockName);

    // ---- 预览 / 循环预览：播完就退出 ----
    if (parser.isSet(optPreview) || parser.isSet(optLoop)) {
        if (!ok) return 1;
        ctrl.onFinished = [] {};
        ctrl.start(!parser.isSet(optLoop), true);
        if (parser.isSet(optTestSkip)) {
            const double sec = parser.value(optTestSkip).toDouble();
            QTimer::singleShot(static_cast<int>(sec * 1000), [&ctrl] { ctrl.reveal(true); });
        }
        return app.exec();
    }

    // ---- 常驻：托盘 + 菜单 ----
    auto *tray = new QSystemTrayIcon(makeTrayIcon());
    tray->setToolTip(QString::fromUtf8(kAppTitle));

    auto *menu = new QMenu();

    auto *h1 = menu->addAction(QStringLiteral("打开 Claude 时"));
    h1->setEnabled(false);
    auto *actIntro = menu->addAction(QStringLiteral("播放开屏动画"));
    actIntro->setCheckable(true);
    auto *actLoop = menu->addAction(QStringLiteral("保留循环背景"));
    actLoop->setCheckable(true);
    menu->addSeparator();

    auto *alphaAct = menu->addAction(QString());
    alphaAct->setEnabled(false);
    auto *alphaBox = new QWidgetAction(menu);
    auto *alphaLay = new QVBoxLayout();
    alphaLay->setContentsMargins(14, 2, 14, 2);
    auto *alphaTop = new QHBoxLayout();
    auto *alphaLabel = new QLabel(QStringLiteral("背景不透明度"));
    auto *alphaVal = new QLabel();
    alphaTop->addWidget(alphaLabel);
    alphaTop->addStretch();
    alphaTop->addWidget(alphaVal);
    auto *alphaSlider = new QSlider(Qt::Horizontal);
    alphaSlider->setRange(0, 80);
    alphaSlider->setMinimumWidth(220);
    alphaLay->addLayout(alphaTop);
    alphaLay->addWidget(alphaSlider);
    auto *alphaHolder = new QWidget();
    alphaHolder->setLayout(alphaLay);
    alphaBox->setDefaultWidget(alphaHolder);
    menu->addAction(alphaBox);

    auto *volBox = new QWidgetAction(menu);
    auto *volLay = new QVBoxLayout();
    volLay->setContentsMargins(14, 2, 14, 2);
    auto *volTop = new QHBoxLayout();
    auto *volLabel = new QLabel(QStringLiteral("开屏音量"));
    auto *volVal = new QLabel();
    volTop->addWidget(volLabel);
    volTop->addStretch();
    volTop->addWidget(volVal);
    auto *volSlider = new QSlider(Qt::Horizontal);
    volSlider->setRange(0, 100);
    volSlider->setMinimumWidth(220);
    volLay->addLayout(volTop);
    volLay->addWidget(volSlider);
    auto *volHolder = new QWidget();
    volHolder->setLayout(volLay);
    volBox->setDefaultWidget(volHolder);
    menu->addAction(volBox);

    menu->addSeparator();
    auto *h2 = menu->addAction(QStringLiteral("现在，在屏幕上"));
    h2->setEnabled(false);
    auto *actPreview = menu->addAction(QStringLiteral("播放一次开屏动画"));
    auto *actLoopOnly = menu->addAction(QStringLiteral("开启循环背景"));
    auto *actStop = menu->addAction(QStringLiteral("关掉动画 / 背景"));
    menu->addSeparator();
    auto *actSettings = menu->addAction(QStringLiteral("设置窗口…"));
    auto *actOpenCfg = menu->addAction(QStringLiteral("打开设置文件…"));
    menu->addSeparator();
    auto *actUninstall = menu->addAction(QStringLiteral("卸载 Claude 开机动画…"));

    auto refresh = [&] {
        actIntro->setChecked(ctrl.config.playIntro);
        actLoop->setChecked(ctrl.config.loopBackground);
        actStop->setEnabled(ctrl.phase != Phase::Idle);
        alphaSlider->blockSignals(true);
        alphaSlider->setValue(static_cast<int>(ctrl.config.residualAlpha * 100));
        alphaSlider->blockSignals(false);
        alphaVal->setText(QString::number(alphaSlider->value()) + "%");
        volSlider->blockSignals(true);
        volSlider->setValue(static_cast<int>(ctrl.config.introVolume * 100));
        volSlider->blockSignals(false);
        volVal->setText(QString::number(volSlider->value()) + "%");
    };

    auto change = [&](std::function<void(Config &)> fn) {
        fn(ctrl.config);
        ctrl.config.save();
        ctrl.applyConfig(ctrl.config);
        refresh();
    };

    QObject::connect(actIntro, &QAction::triggered, [&](bool on) {
        change([on](Config &c) { c.playIntro = on; });
    });
    QObject::connect(actLoop, &QAction::triggered, [&](bool on) {
        change([on](Config &c) { c.loopBackground = on; });
    });
    QObject::connect(alphaSlider, &QSlider::valueChanged, [&](int v) {
        alphaVal->setText(QString::number(v) + "%");
        change([v](Config &c) { c.residualAlpha = v / 100.0; });
    });
    QObject::connect(volSlider, &QSlider::valueChanged, [&](int v) {
        volVal->setText(QString::number(v) + "%");
        change([v](Config &c) { c.introVolume = v / 100.0; });
    });

    SettingsDialog *dlg = nullptr;
    auto openSettings = [&] {
        if (!dlg) {
            dlg = new SettingsDialog([&]() -> const Config & { return ctrl.config; }, change);
            QObject::connect(dlg->btnPreview, &QPushButton::clicked, [&] {
                ctrl.teardown();
                ctrl.start(true, false);
                refresh();
            });
            QObject::connect(dlg->btnLoopOnly, &QPushButton::clicked, [&] {
                ctrl.teardown();
                ctrl.start(false, false);
                refresh();
            });
            QObject::connect(dlg->btnStop, &QPushButton::clicked, [&] {
                ctrl.finish(true);
                refresh();
            });
        }
        dlg->refresh();
        dlg->show();
        dlg->raise();
        dlg->activateWindow();
    };

    QObject::connect(actPreview, &QAction::triggered, [&] {
        ctrl.teardown();
        ctrl.start(true, false);
        refresh();
    });
    QObject::connect(actLoopOnly, &QAction::triggered, [&] {
        ctrl.teardown();
        ctrl.start(false, false);
        refresh();
    });
    QObject::connect(actStop, &QAction::triggered, [&] {
        ctrl.finish(true);
        refresh();
    });
    QObject::connect(actSettings, &QAction::triggered, openSettings);
    QObject::connect(actOpenCfg, &QAction::triggered, [] {
        QProcess::startDetached("xdg-open", {Config::file()});
    });
    QObject::connect(actUninstall, &QAction::triggered, [&] {
        auto r = QMessageBox::question(nullptr, QStringLiteral("卸载 Claude 开机动画"),
                                       QStringLiteral("会删除开机自启、动画素材、设置和日志。\n"
                                                      "Claude 本身从未被修改，卸载后就是原装的样子。\n\n确定卸载吗？"),
                                       QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
        if (r != QMessageBox::Yes) return;
        ctrl.teardown();
        installAutostart(false);
        QProcess::startDetached(QCoreApplication::applicationFilePath(), {"--uninstall"});
        qApp->quit();
    });

    tray->setContextMenu(menu);
    QObject::connect(tray, &QSystemTrayIcon::activated, [&](QSystemTrayIcon::ActivationReason reason) {
        if (reason == QSystemTrayIcon::Trigger) openSettings();
    });
    tray->show();

    QObject::connect(&server, &QLocalServer::newConnection, [&] {
        QLocalSocket *c = server.nextPendingConnection();
        if (!c) return;
        QObject::connect(c, &QLocalSocket::readyRead, [&, c] {
            const QString cmd = QString::fromUtf8(c->readAll()).trimmed();
            if (cmd == "stop") ctrl.finish(true);
            else if (cmd == "preview") { ctrl.teardown(); ctrl.start(true, false); }
            else if (cmd == "loop") { ctrl.teardown(); ctrl.start(false, false); }
            else openSettings();
            refresh();
            c->disconnectFromServer();
        });
    });

    // 托盘不可用（没装 StatusNotifier 宿主）时至少把设置窗口弹出来
    if (!QSystemTrayIcon::isSystemTrayAvailable()) {
        logMsg(QStringLiteral("系统托盘不可用，直接显示设置窗口"));
        openSettings();
    }

    ctrl.onFinished = [&] { refresh(); };
    ctrl.config.save();
    refresh();
    logMsg(QStringLiteral("常驻启动，开屏=%1 循环背景=%2 模式=%3")
               .arg(ctrl.config.playIntro)
               .arg(ctrl.config.loopBackground)
               .arg(ctrl.config.overlayMode));

    if (parser.isSet(optSettings) || ok == false) openSettings();

    return app.exec();
}
