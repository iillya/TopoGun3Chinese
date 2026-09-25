#pragma once
#include "hotkey_config.h"
#include <QtCore/QSet>
#include <QtGui/QKeyEvent>
#include <QtGui/QKeySequence>
#include <QtGui/QFont>
#include <QtGui/QFontMetrics>
#include <QtWidgets/QApplication>
#include <QtWidgets/QDialog>
#include <QtWidgets/QLabel>
#include <QtWidgets/QPushButton>
#include <QtWidgets/QVBoxLayout>
#include <QtWidgets/QHBoxLayout>

// Plugin-owned UI; never changes host shortcut settings or scene data.
class ToggleHotkeyDialog final : public QDialog {
    int& currentKey_;
    bool& dialogOpen_;
    const QString configPath_;
    int pendingKey_;
    bool listening_ = false;
    QSet<int> heldKeys_;
    QPushButton* capture_;
    QPushButton* ok_;
    QLabel* error_;
    bool saveKey(int key) {
        QString error;
        if (!ChineseHotkeyConfig::save(configPath_, key, &error)) {
            error_->setText(QStringLiteral("保存失败：") + error);
            error_->show();
            return false;
        }
        currentKey_ = key;
        return true;
    }
    void stopListen() {
        listening_ = false;
        ok_->setEnabled(true);
        capture_->setText(QStringLiteral("当前：") + QKeySequence(pendingKey_).toString(QKeySequence::NativeText));
    }
public:
    ToggleHotkeyDialog(QWidget* parent, int& currentKey, bool& dialogOpen,
                       const QString& configPath, const QWidget* fontReference = nullptr)
        : QDialog(parent), currentKey_(currentKey), dialogOpen_(dialogOpen),
          configPath_(configPath), pendingKey_(currentKey) {
        setWindowTitle(QStringLiteral("中英切换快捷键"));
        setWindowFlag(Qt::WindowContextHelpButtonHint, false);
        setObjectName(QStringLiteral("qt_chinese_hotkey_settings"));
        setAttribute(Qt::WA_DeleteOnClose);
        setWindowModality(Qt::WindowModal);
        // Use the actual host menu font, including TopoGun's own UI scaling.
        // Do not combine point-size and stylesheet pixel-size overrides.
        const QWidget* reference = fontReference ? fontReference : parent;
        setFont(reference ? reference->font() : QApplication::font());
        if (parent) setPalette(parent->palette());
        setStyleSheet(QStringLiteral(
            "QDialog#qt_chinese_hotkey_settings { background: #303030; }"
            "QDialog#qt_chinese_hotkey_settings QLabel#hotkeyError { color: #ffb4a8; }"
            "QDialog#qt_chinese_hotkey_settings QPushButton#hotkeyAccept { border-color: #66aaff; }"));
        const QFontMetrics metrics(font());
        const int margin = qMax(8, metrics.height());
        const int spacing = qMax(4, metrics.height() / 2);
        const int contentWidth = (metrics.horizontalAdvance(
            QStringLiteral("按 Esc 取消录入，点击“确定”保存。")) + metrics.height()) * 115 / 100;
        auto* layout = new QVBoxLayout(this);
        layout->setContentsMargins(margin, margin, margin, margin);
        layout->setSpacing(spacing);
        layout->setSizeConstraint(QLayout::SetFixedSize);
        auto* instruction = new QLabel(QStringLiteral("点击下方按钮，按下新的快捷键。"), this);
        instruction->setObjectName(QStringLiteral("hotkeyInstruction"));
        instruction->setWordWrap(true);
        instruction->setFixedWidth(contentWidth);
        layout->addWidget(instruction);
        capture_ = new QPushButton(this);
        capture_->setObjectName(QStringLiteral("hotkeyCapture"));
        layout->addWidget(capture_);
        auto addHint = [this, layout, contentWidth](const QString& text) {
            auto* label = new QLabel(text, this);
            label->setWordWrap(true);
            label->setFixedWidth(contentWidth);
            layout->addWidget(label);
        };
        addHint(QStringLiteral("按 Esc 取消录入，点击“确定”保存。"));
        // Keep the key symbol; Arial places ASCII tilde near the Latin midline.
        // Only this glyph uses a fallback face; the host font size is inherited.
        addHint(QStringLiteral("采集未翻译文本：Shift + <span style=\"font-family: Arial;\">~</span>"));
        error_ = new QLabel(this);
        error_->setObjectName(QStringLiteral("hotkeyError"));
        error_->setWordWrap(true);
        error_->setFixedWidth(contentWidth);
        layout->addWidget(error_);
        error_->hide();
        auto* row = new QHBoxLayout;
        layout->addLayout(row);
        ok_ = new QPushButton(QStringLiteral("确定"), this);
        auto* reset = new QPushButton(QStringLiteral("重置"), this);
        auto* cancel = new QPushButton(QStringLiteral("取消"), this);
        ok_->setObjectName(QStringLiteral("hotkeyAccept"));
        reset->setObjectName(QStringLiteral("hotkeyReset"));
        cancel->setObjectName(QStringLiteral("hotkeyCancel"));
        row->addWidget(reset);
        row->addStretch();
        row->addWidget(cancel);
        row->addWidget(ok_);
        ok_->setDefault(true);
        reset->setAutoDefault(false);
        cancel->setAutoDefault(false);
        capture_->setAutoDefault(false);
        row->setSpacing(spacing);
        setFocusProxy(ok_);
        QObject::connect(capture_, &QPushButton::clicked, this, [this] {
            listening_ = true;
            error_->clear();
            error_->hide();
            ok_->setEnabled(false);
            capture_->setText(QStringLiteral("等待按键…"));
            capture_->setFocus();
        });
        QObject::connect(ok_, &QPushButton::clicked, this, [this] { accept(); });
        QObject::connect(cancel, &QPushButton::clicked, this, [this] { reject(); });
        QObject::connect(reset, &QPushButton::clicked, this, [this] {
            const int defaultKey = ChineseHotkeyConfig::defaultKey;
            pendingKey_ = defaultKey;
            error_->clear();
            error_->hide();
            stopListen();
        });
        stopListen();
        dialogOpen_ = true;
        QApplication::instance()->installEventFilter(this);
    }
    ~ToggleHotkeyDialog() override {
        QApplication::instance()->removeEventFilter(this);
        dialogOpen_ = false;
    }
protected:
    void accept() override {
        if (listening_) return;
        if (!saveKey(pendingKey_)) return;
        dialogOpen_ = false;
        QDialog::accept();
    }
    void reject() override { stopListen(); dialogOpen_ = false; QDialog::reject(); }
    bool eventFilter(QObject* object, QEvent* event) override {
        if (object == this && event->type() == QEvent::WindowDeactivate) {
            heldKeys_.clear();
            stopListen();
        }
        auto* widget = qobject_cast<QWidget*>(object);
        if (!isVisible() || !widget || widget->window() != this) return false;
        if (event->type() != QEvent::KeyPress && event->type() != QEvent::KeyRelease &&
            event->type() != QEvent::ShortcutOverride) return false;
        auto* key = static_cast<QKeyEvent*>(event);
        if (!listening_ && !heldKeys_.contains(key->key())) return false;
        if (event->type() == QEvent::ShortcutOverride) { event->accept(); return true; }
        if (event->type() == QEvent::KeyRelease) {
            if (!key->isAutoRepeat()) heldKeys_.remove(key->key());
            return true;
        }
        if (key->isAutoRepeat()) return true;
        heldKeys_.insert(key->key());
        if (!listening_) return true;
        if (key->key() == Qt::Key_Escape) { stopListen(); return true; }
        if (key->modifiers() != Qt::NoModifier || !ChineseHotkeyConfig::valid(key->key())) {
            error_->setText(QStringLiteral("请按单个字母、数字、功能键或导航键，不使用组合键。"));
            error_->show();
            return true;
        }
        pendingKey_ = key->key();
        error_->clear();
        error_->hide();
        stopListen();
        return true;
    }
};
