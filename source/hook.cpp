// ============================================================================
//  TopoGun Modeler Chinese Localizer - Translation Engine (Hook DLL)
// ============================================================================
//  Architecture mirrors the Substance 3D Painter/Designer localizer, adapted
//  to run standalone because TopoGun exposes no Python plug-in API:
//
//  Dictionaries  sp-translation-v1 JSON in <plug-in>/translations/*_zh.json,
//                loaded sorted & merged (later files win).
//  Lookup        1. global exact  2. fuzzy match.
//  DisplayLayer  QProxyStyle (drawItemText / itemTextRect) for every style-drawn
//                control (buttons, checkboxes, menus, group-box titles, tab
//                bars, combo buttons, headers). Uses only Qt's public virtual
//                API -- no mangled symbols, no hand-written x64 sret ABI -- so
//                it follows the Qt 5 public style API.
//  ItemViewLayer QStyledItemDelegate::displayText for list/tree/table.
//  PaintLayer    QPainter::drawText IAT hook for QWidget painting and QStyle
//                offscreen UI caches. Unscoped image/asset/export painting is
//                left in the source language.
//  MeasureLayer  QFontMetrics/QFontMetricsF IAT hooks inside Qt5Widgets only.
//                Widget layout can measure the translated width while host
//                model/export code continues to receive source-text metrics.
//  Data model    Never rewritten: no setText/setTabText/setItemText mutations.
//  Controls      F3 enable/disable; Shift+` dumps untranslated text to a JSON.
//  API           C exports so a future settings UI / updater can version it.
//
//  Delivery      launcher.cpp starts TopoGun.exe SUSPENDED, injects
//                this DLL, then resumes. Nothing on disk is modified.
// ============================================================================

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "hook_lifecycle.h"
#include "ui_paint_boundary.h"
#include "version.h"
#include "qt_compatibility.h"
#include "hotkey_settings.h"
#include <shlobj.h>
#include <tlhelp32.h>
#include <string>
#include <vector>
#include <map>
#include <unordered_map>
#include <algorithm>
#include <cctype>
#include <utility>
#include <functional>
#include <atomic>

#include <QtCore/QCoreApplication>
#include <QtCore/QCache>
#include <QtCore/QMetaObject>
#include <QtCore/QString>
#include <QtCore/QVariant>
#include <QtCore/QLocale>
#include <QtCore/QTimer>
#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QJsonParseError>
#include <QtCore/QPointer>
#include <QtCore/QModelIndex>
#include <QtCore/QAbstractItemModel>
#include <QtWidgets/QAction>
#include <QtGui/QPainter>
#include <QtGui/QImage>
#include <QtGui/QPixmap>
#include <QtGui/QColor>
#include <QtGui/QFontMetrics>
#include <QtGui/QTextOption>
#include <QtGui/QKeyEvent>
#include <QtGui/QMouseEvent>
#include <QtGui/QCloseEvent>
#include <QtGui/QKeySequence>
#include <QtWidgets/QApplication>
#include <QtWidgets/QAbstractItemView>
#include <QtWidgets/QStyledItemDelegate>
#include <QtWidgets/QProxyStyle>
#include <QtWidgets/QStyleFactory>
#include <QtWidgets/QStyleOption>
#include <QtWidgets/QLabel>
#include <QtWidgets/QAbstractButton>
#include <QtWidgets/QMenu>
#include <QtWidgets/QMenuBar>
#include <QtWidgets/QMessageBox>
#include <QtWidgets/QTabBar>
#include <QtWidgets/QLineEdit>
#include <QtWidgets/QComboBox>
#include <QtWidgets/QToolBar>
#include <QtWidgets/QHBoxLayout>
#include <QtWidgets/QDialog>
#include <QtWidgets/QPushButton>
#include <QtWidgets/QVBoxLayout>
#include <QtGui/QDesktopServices>
#include <QtCore/QUrl>

// ---------------------------------------------------------------------------
// Version / identity
// ---------------------------------------------------------------------------
extern "C" __declspec(dllexport) int __cdecl tg_localizer_api_version() { return 1; }
extern "C" __declspec(dllexport) const char* __cdecl tg_localizer_version() {
    return TOPOGUN_CHINESE_VERSION;
}

static const wchar_t* kGuiModule = L"Qt5Gui.dll";
static const wchar_t* kCoreModule = L"Qt5Core.dll";
static const wchar_t* kWidgetsModule = L"Qt5Widgets.dll";
static const wchar_t* kHookDllPrefix = L"TopoGunChineseHook";

// ---------------------------------------------------------------------------
// Global state
// ---------------------------------------------------------------------------
static std::atomic<bool> g_enabled{true};
static int       g_toggleKey        = Qt::Key_F3;    // 中/英 toggle hotkey (default F3)
static bool      g_hotkeyDlgOpen    = false;         // while the rebind dialog is up
static QString   g_plugInDir;
static HMODULE   g_selfModule       = nullptr;
static bool      g_hooksInstalled   = false;

static std::unordered_map<std::string, std::string> g_translations;       // global
static std::unordered_map<std::string, std::string> g_foldedTranslations; // fuzzy index
// Doubled, lower-cased dictionary keys sorted for O(log N) elided-text lookups.
static std::vector<std::pair<std::string, std::string>> g_sortedKeys;     // lowercased -> original
// Cache complete results, including misses, in Qt's native string format.
// Cost includes UTF-16 payload plus an allowance for entry metadata. QCache
// evicts least recently used entries; dynamic scene text cannot grow it forever.
static constexpr int kLookupCacheCost = 1024 * 1024;
static constexpr int kElidedCacheCost = 256 * 1024;
static constexpr int kCacheEntryOverhead = 128;
static constexpr int kMaxCachedTextLength = 1024;
static std::atomic<unsigned> g_dictionaryGeneration{0};
struct TranslationCache {
    unsigned generation = 0;
    QCache<QString, QString> lookup{kLookupCacheCost};
    QCache<QString, QString> elided{kElidedCacheCost};
};
static TranslationCache& translationCache() {
    static thread_local TranslationCache cache;
    const unsigned generation = g_dictionaryGeneration.load(std::memory_order_acquire);
    if (cache.generation != generation) {
        cache.lookup.clear();
        cache.elided.clear();
        cache.generation = generation;
    }
    return cache;
}
static void cacheResult(QCache<QString, QString>& cache,
                        const QString& source, const QString& result) {
    if (source.size() > kMaxCachedTextLength || result.size() > kMaxCachedTextLength) return;
    // Own compact strings: a small QString may share a much larger allocation
    // after chopping/slicing, or may reference caller-owned fromRawData memory.
    QString key(source.constData(), source.size());
    auto* value = new QString(result.constData(), result.size());
    cache.insert(key, value, (source.size() + result.size()) * 2 + kCacheEntryOverhead);
}

static QString normalizeForMatch(QString text);
static bool containsCjk(const QString& text);
static void installAuthorLink();
static void repaintAll();
static void refreshLayouts();
static bool replaceImports(HMODULE module, void* original, void* replacement);
static bool replaceImportByName(HMODULE module, const char* name, void* replacement,
                                void** outOriginal);
static void captureUntranslated();

// ---------------------------------------------------------------------------
// Paths / logging
// ---------------------------------------------------------------------------
static std::wstring SelfDirW() {
    std::vector<wchar_t> buf(1024);
    while (buf.size() <= 32768) {
        DWORD n = GetModuleFileNameW(g_selfModule, buf.data(),
                                     static_cast<DWORD>(buf.size()));
        if (!n) return L"";
        if (n < buf.size() - 1) {
            std::wstring path(buf.data(), n);
            size_t slash = path.find_last_of(L"\\/");
            return slash == std::wstring::npos ? L"" : path.substr(0, slash);
        }
        buf.resize(buf.size() * 2);
    }
    return L"";
}

static void LoadDictionaries() {
    g_translations.clear();
    g_foldedTranslations.clear();
    g_sortedKeys.clear();
    // Loading is a startup/idle operation, never concurrent with lookups.
    // Invalidate every thread's cache lazily, including when the folder is gone.
    g_dictionaryGeneration.fetch_add(1, std::memory_order_release);
    QDir dir(g_plugInDir + "/translations");
    if (!dir.exists()) return;
    QStringList files = dir.entryList({"*_zh.json"}, QDir::Files, QDir::Name);
    for (const QString& name : files) {
        QFile file(dir.filePath(name));
        if (!file.open(QIODevice::ReadOnly)) continue;
        QJsonParseError error{};
        const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &error);
        if (error.error != QJsonParseError::NoError || !document.isObject()) continue;
        const QJsonObject map = document.object()
                                    .value(QLatin1String("translations"))
                                    .toObject();
        for (auto it = map.constBegin(); it != map.constEnd(); ++it) {
            if (!it.value().isString()) continue;
            const QString value = it.value().toString();
            if (it.key().trimmed().isEmpty() || value.trimmed().isEmpty()) continue;
            const std::string key = it.key().toUtf8().toStdString();
            g_translations[key] = value.toUtf8().toStdString();
        }
    }
    // Build the fuzzy index (normalized English key -> Chinese).
    // Ambiguous normalized keys must not select an arbitrary unordered-map value.
    for (const auto& kv : g_translations) {
        const std::string folded = normalizeForMatch(QString::fromUtf8(kv.first.c_str())).toUtf8().constData();
        auto inserted = g_foldedTranslations.emplace(folded, kv.second);
        if (!inserted.second && inserted.first->second != kv.second)
            inserted.first->second.clear();
    }
    // Build the sorted, lower-cased key table for O(log N) elided-text lookups.
    g_sortedKeys.clear();
    g_sortedKeys.reserve(g_translations.size());
    for (const auto& kv : g_translations) {
        std::string low = kv.first;
        for (char& c : low) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        g_sortedKeys.emplace_back(std::move(low), kv.first);
    }
    std::sort(g_sortedKeys.begin(), g_sortedKeys.end(),
              [](const auto& a, const auto& b) { return a.first < b.first; });
}

// ---------------------------------------------------------------------------
// i18n normalization + fuzzy lookup (mirrors the Substance localizer)
// ---------------------------------------------------------------------------
static QString normalizeForMatch(QString text) {
    text.replace(QChar(0x00A0), QLatin1Char(' '));
    text.replace(QChar(0x3000), QLatin1Char(' '));
    text.replace(QLatin1Char('\r'), QLatin1Char('\n'));
    text = text.simplified();
    text.remove(QChar(0x200B)); text.remove(QChar(0x200D)); text.remove(QChar(0xFEFF));
    while (text.endsWith(QChar(0x2026))) text.chop(1);
    while (text.endsWith(QLatin1String("..."))) text.chop(3);
    if (text.endsWith(QLatin1String(" *"))) text.chop(2);
    text.replace(QLatin1Char('_'), QLatin1Char(' '));
    text = text.normalized(QString::NormalizationForm_C);
    for (int i = 0; i < text.size(); ++i) {
        const uint code = text.at(i).unicode();
        if (code >= 0xFF01 && code <= 0xFF5E) text[i] = QChar(code - 0xFEE0);
    }
    text = text.toCaseFolded();
    text = text.normalized(QString::NormalizationForm_D);
    QString stripped; stripped.reserve(text.size());
    for (const QChar ch : text) {
        const QChar::Category cat = ch.category();
        if (cat == QChar::Mark_NonSpacing || cat == QChar::Mark_SpacingCombining ||
            cat == QChar::Mark_Enclosing)
            continue;
        stripped.append(ch);
    }
    stripped.remove(QLatin1Char(' '));
    return stripped.normalized(QString::NormalizationForm_C);
}

static bool containsCjk(const QString& text) {
    for (QChar ch : text) {
        const ushort u = ch.unicode();
        if ((u >= 0x3400 && u <= 0x9FFF) || (u >= 0xF900 && u <= 0xFAFF))
            return true;
    }
    return false;
}

// JSON-string escape for a UTF-8 key/value (quotes, backslash, control chars).
static std::string jsonEscape(const std::string& s) {
    std::string out;
    out.reserve(s.size() + 8);
    for (unsigned char c : s) {
        switch (c) {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\b': out += "\\b";  break;
            case '\f': out += "\\f";  break;
            case '\n': out += "\\n";  break;
            case '\r': out += "\\r";  break;
            case '\t': out += "\\t";  break;
            default:
                if (c < 0x20) {
                    const char* hex = "0123456789abcdef";
                    out += "\\u00";
                    out.push_back(hex[(c >> 4) & 0xF]);
                    out.push_back(hex[c & 0xF]);
                } else {
                    out.push_back((char)c);
                }
        }
    }
    return out;
}

// Plain display-layer lookup: exact first, then fuzzy.
static QString translatedUncached(const QString& raw) {
    QString key = raw.trimmed();
    if (key.isEmpty()) return {};

    auto exact = g_translations.find(key.toUtf8().constData());
    if (exact != g_translations.end())
        return QString::fromUtf8(exact->second.c_str());
    if (containsCjk(key)) return {};

    // mnemonic / state suffix handling
    QString stateSuffix;
    const bool hasSuffix = key.endsWith(QLatin1Char('*'));
    const bool hasMnemonic = key.contains(QLatin1Char('&'));
    if (hasSuffix) { key.chop(1); key = key.trimmed(); stateSuffix = "*"; }
    if (hasMnemonic) key.remove(QLatin1Char('&'));
    if (hasSuffix || hasMnemonic) {
        auto found = g_translations.find(key.toUtf8().constData());
        if (found != g_translations.end())
            return QString::fromUtf8(found->second.c_str()) + stateSuffix;
    }

    const std::string norm = normalizeForMatch(key).toUtf8().constData();
    auto fuzzy = g_foldedTranslations.find(norm);
    if (fuzzy != g_foldedTranslations.end() && !fuzzy->second.empty())
        return QString::fromUtf8(fuzzy->second.c_str()) + stateSuffix;
    return {};
}

static QString translated(const QString& raw) {
    if (!g_enabled || raw.isEmpty()) return {};
    auto& cache = translationCache().lookup;
    if (const QString* result = cache.object(raw)) return *result;
    const QString result = translatedUncached(raw);
    cacheResult(cache, raw, result);
    return result;
}

// ---------------------------------------------------------------------------
// Item-view delegate: display-layer translation for list/tree/table.
// ---------------------------------------------------------------------------
class TranslateDelegate : public QStyledItemDelegate {
public:
    using QStyledItemDelegate::QStyledItemDelegate;
    QString displayText(const QVariant& value, const QLocale& locale) const override {
        QString original = QStyledItemDelegate::displayText(value, locale);
        if (original.isEmpty() || containsCjk(original)) return original;
        QString t = translated(original);
        return t.isEmpty() ? original : t;
    }
};

// ---------------------------------------------------------------------------
// DisplayLayer: QProxyStyle-based painting + sizing translation.
//
// This covers every control whose text is drawn through QStyle (buttons,
// checkboxes, menu items, group-box titles, tab bars, combo buttons, header
// sections, tool buttons). It uses only Qt's public virtual API: no hard-coded
// mangled symbol names and no hand-written x64 sret ABI model, so it is safe
// within the supported Qt 5 family. The widget text is never mutated; translation happens at
// the style boundary. The PaintLayer (drawText IAT) below never double-
// translates because it short-circuits on any string that already contains CJK.
// ---------------------------------------------------------------------------
class ChineseProxyStyle : public QProxyStyle {
public:
    using QProxyStyle::QProxyStyle;

    void drawControl(ControlElement element, const QStyleOption* option,
                     QPainter* painter, const QWidget* widget = nullptr) const override {
        UiPaintBoundary::WidgetPaintScope uiPaint(painter, widget);
        if (option && element == CE_MenuBarItem) {
            if (const auto* item = qstyleoption_cast<const QStyleOptionMenuItem*>(option)) {
                if (item->text == QStringLiteral("中/英") ||
                    item->text == QStringLiteral("Bilibili 神说要凑数汉化")) {
                    QStyleOptionMenuItem copy(*item);
                    copy.state &= ~(State_Selected | State_MouseOver | State_Sunken);
                    QProxyStyle::drawControl(element, &copy, painter, widget);
                    return;
                }
            }
        }
        QProxyStyle::drawControl(element, option, painter, widget);
    }

    void drawPrimitive(PrimitiveElement element, const QStyleOption* option,
                       QPainter* painter, const QWidget* widget = nullptr) const override {
        UiPaintBoundary::WidgetPaintScope uiPaint(painter, widget);
        QProxyStyle::drawPrimitive(element, option, painter, widget);
    }

    void drawComplexControl(ComplexControl control, const QStyleOptionComplex* option,
                            QPainter* painter, const QWidget* widget = nullptr) const override {
        UiPaintBoundary::WidgetPaintScope uiPaint(painter, widget);
        QProxyStyle::drawComplexControl(control, option, painter, widget);
    }

    void drawItemText(QPainter* painter, const QRect& rect, int flags,
                      const QPalette& pal, bool enabled, const QString& text,
                      QPalette::ColorRole textRole = QPalette::NoRole) const override {
        UiPaintBoundary::WidgetPaintScope uiPaint(painter);
        const QString tr = UiPaintBoundary::isDisplayPainter(painter)
            ? translated(text) : QString();
        const QString draw = tr.isEmpty() ? text : tr;
        // Plugin menu actions share the attribution blue.
        if (text == QStringLiteral("中/英") ||
            text == QStringLiteral("Bilibili 神说要凑数汉化")) {
            // The base drawItemText() re-selects the pen from the palette, so a
            // setPen() here would be overridden.  Pass a palette whose ButtonText
            // (the role CE_MenuBarItem uses) is blue instead.
            QPalette p = pal;
            const QColor blue(0x66, 0xAA, 0xFF);   // same #66aaff as the old links
            p.setColor(QPalette::WindowText, blue);
            p.setColor(QPalette::ButtonText, blue);
            QProxyStyle::drawItemText(painter, rect, flags, p, enabled, draw, textRole);
            return;
        }
        QProxyStyle::drawItemText(painter, rect, flags, pal, enabled,
                                  draw, textRole);
    }

    QRect itemTextRect(const QFontMetrics& fm, const QRect& r, int flags,
                       bool enabled, const QString& text) const override {
        QString tr = translated(text);
        return QProxyStyle::itemTextRect(fm, r, flags, enabled,
                                         tr.isEmpty() ? text : tr);
    }

};

static bool g_proxyStyleInstalled = false;

static void installProxyStyle() {
    if (g_proxyStyleInstalled || !QApplication::instance()) return;
    // IMPORTANT: never hand QApplication::style() (Qt's globally-owned style) to
    // QProxyStyle -- the proxy would delete it on destruction, and Qt would too,
    // causing a double free. Wrap a *fresh* instance of the same style instead
    // (QProxyStyle takes ownership of the base it is given).
    const QString key = QApplication::style() ? QApplication::style()->objectName()
                                              : QString();
    QStyle* base = QStyleFactory::create(key.isEmpty() ? QStringLiteral("fusion")
                                                       : key);
    if (!base) base = QStyleFactory::create(QStringLiteral("fusion"));
    QApplication::setStyle(new ChineseProxyStyle(base));
    g_proxyStyleInstalled = true;
}

// ---------------------------------------------------------------------------
// Item-view delegation: list/tree/table items are translated via the delegate.
// Everything else is translated only at QPainter::drawText call time.
// ---------------------------------------------------------------------------
static void applyWidget(QWidget* w);

static void applyTranslations() {
    if (!g_enabled) return;
    for (QWidget* w : QApplication::allWidgets())
        applyWidget(w);
}

// ---------------------------------------------------------------------------
// CElidingLabel (Qt Advanced Docking) read-only full-text access.
// Its tooltip retains the full source while QLabel::text() may be elided.
// ---------------------------------------------------------------------------
static bool isElidingLabel(QWidget* w) {
    return w && QString::fromLatin1(w->metaObject()->className()) ==
                    QLatin1String("ads::CElidingLabel");
}

// Full source text for a label, masking Qt Advanced Docking eliding labels.
static QString labelSource(QLabel* lbl) {
    if (!lbl) return {};
    QString t = lbl->text();
    // ads::CElidingLabel keeps the FULL source in the toolTip (set by its own
    // setText) while QLabel::text() holds only the elided fragment drawn
    // (e.g. "Mater…" for "Material").  Reading the toolTip therefore recovers
    // the complete string for dictionary lookup without a by-value call.
    if (isElidingLabel(lbl)) {
        QString tip = lbl->toolTip();
        if (!tip.isEmpty()) return tip;
        return t;
    }
    // 原生控件（普通 QLabel）:text() 就是完整原文（省略只发生在绘制阶段，
    // 不修改存值），直接返回即可。不再回退到 toolTip。
    return t;
}

// Flip the 中/英 state. Text is never rewritten, so the change is re-rendered
// through the display layers. The toggle runs synchronously so it takes effect
// the moment the mouse is released. Maximized / fullscreen windows are left
// alone so the resize does not fight the user's own window state.
static void toggleTranslation() {
    g_enabled = !g_enabled;
    if (g_enabled) applyTranslations();
    // Custom-painted / docked controls cache the text widths and item positions
    // they measured at startup and keep using them after a language switch.
    // Broadcasting a font change makes each re-measure its content for the new
    // language (Qt widgets recompute cached metrics in changeEvent(FontChange)),
    // which is what clicking a node also does -- a plain repaint() only redraws
    // the stale cache and is not enough.
    std::vector<QPointer<QWidget>> widgets;
    for (QWidget* widget : QApplication::allWidgets()) widgets.emplace_back(widget);
    for (const auto& w : widgets) {
        if (!w) continue;
        QEvent ev(QEvent::FontChange);
        QApplication::sendEvent(w.data(), &ev);
    }
    refreshLayouts();
    // Force the (persistent) top menu bar to re-measure its action rects on a
    // language switch.  QMenuBar caches those rects and only recomputes them when
    // itemsDirty is set; QMenuBar::changeEvent() does exactly that on
    // StyleChange / FontChange / ApplicationFontChange, so the menu items
    // shrink/grow to the measured Chinese / English width immediately.
    for (const auto& w : widgets) {
        if (auto* mb = qobject_cast<QMenuBar*>(w.data())) {
            QEvent ev(QEvent::StyleChange);
            QApplication::sendEvent(mb, &ev);
        }
    }
    const QWidgetList tops = QApplication::topLevelWidgets();
    for (QWidget* w : tops) {
        if (!w || !w->isVisible()) continue;
        // Force a synchronous re-layout so width follows the language width
        // immediately, instead of waiting for a deferred LayoutRequest.
        // QLayout::activate() re-runs doResize() so the dock/central content
        // sizes follow the measured Chinese / English width right now.  (The
        // menu bar's own action rects are refreshed by the ApplicationFontChange
        // loop above, which triggers its itemsDirty -> calcActionRects.)
        if (QLayout* lay = w->layout()) {
            lay->invalidate();   // clear the "already activated" flag
            lay->activate();     // size children (and the menu bar) right now
        }
        // Keep the user's window geometry. Layout invalidation is sufficient.
    }
    repaintAll();
}

// Only wrap the view's *genuinely default* delegate. A custom delegate (which
// TopoGun uses for its node / property editors) owns its own paint/editor
// logic; replacing it would silently drop that behavior. Text from custom
// delegates is still translated at render time by the QStyle + QPainter hooks,
// so we only need displayText translation for the stock QStyledItemDelegate.
static bool isDefaultItemDelegate(QAbstractItemView* view) {
    QAbstractItemDelegate* d = view->itemDelegate();
    return d && QString::fromLatin1(d->metaObject()->className()) ==
                    QLatin1String("QStyledItemDelegate");
}

// Install only the display-layer delegate. Standard controls are handled by
// drawText hooks, leaving their text properties and sizeHint inputs untouched.
// Called on Show / Polish as controls appear, and when translation is enabled.
static void applyWidget(QWidget* w) {
    if (!w || !g_enabled) return;
    if (auto* view = qobject_cast<QAbstractItemView*>(w)) {
        if (isDefaultItemDelegate(view) &&
            !dynamic_cast<TranslateDelegate*>(view->itemDelegate())) {
            auto* d = new TranslateDelegate(view);
            view->setItemDelegate(d);
        }
    }
}

// ---------------------------------------------------------------------------
// translatable-string test (used by manual text capture)
// ---------------------------------------------------------------------------
static bool isTranslatableString(const QString& s) {
    QString t = s.trimmed();
    if (t.isEmpty() || containsCjk(t)) return false;
    if (t.size() < 2) return false;
    if (t[0] == '<') return false;                 // HTML
    if (t.startsWith(QLatin1String("http://")) ||
        t.startsWith(QLatin1String("https://"))) return false;
    // numeric detection
    bool numeric = true;
    for (QChar c : t) {
        if (!(c.isDigit() || c == '.' || c == ',' || c == '%' || c == '-' ||
              c == '+' || c == 'e' || c == 'E' || c == ' ')) { numeric = false; break; }
    }
    return !numeric;
}

// ---------------------------------------------------------------------------
// Manual untranslated-text sniffer (Shift + `): walk the live UI read-only and
// append every English string not yet in the dictionary to the user's Desktop
// as <Desktop>\TopoGun_untranslated_zh.json (sp-translation-v1, empty values). Each
// press only ADDS entries that aren't already there, never overwrites existing
// ones (so manually filled values survive).  Unlike the old ST_TOUR/ST_TRPROBE
// probes it never mutates the UI and only runs on hotkey.
// ---------------------------------------------------------------------------
static void collectMenuActions(QMenu* menu, const std::function<void(const QString&)>& add,
                               QSet<QMenu*>& visited) {
    if (!menu || visited.contains(menu) || visited.size() >= 1024) return;
    visited.insert(menu);
    for (QAction* a : menu->actions()) {
        if (!a->text().isEmpty()) add(a->text());
        if (a->menu()) collectMenuActions(a->menu(), add, visited);
    }
}

// Walk each item's model recursively so collapsed branches are searched too.
// This reads DisplayRole only; it does not expand nodes or mutate the control.
// Depth and cell budgets keep pathological or cyclic custom models bounded.
static void collectModelDeep(QAbstractItemModel* model,
                             const QModelIndex& parent,
                             const std::function<void(const QString&)>& add,
                             int depth, int& budget, int maxDepth = 64) {
    if (!model || depth > maxDepth || budget <= 0) return;

    const int rows = model->rowCount(parent);
    const int columns = model->columnCount(parent);
    if (rows <= 0 || columns <= 0) return;

    for (int row = 0; row < rows && budget > 0; ++row) {
        const QModelIndex childParent = model->index(row, 0, parent);
        for (int column = 0; column < columns && budget > 0; ++column) {
            const QModelIndex child = model->index(row, column, parent);
            const QVariant value = child.data(Qt::DisplayRole);
            if (value.isValid()) add(value.toString());
            --budget;
        }
        collectModelDeep(model, childParent, add, depth + 1, budget, maxDepth);
    }
}

static void captureUntranslated() {
    std::map<std::string, std::string> pending;
    auto add = [&](const QString& s) {
        if (!isTranslatableString(s)) return;
        std::string k = s.trimmed().toUtf8().constData();
        if (k.empty() || g_translations.count(k)) return;
        pending.emplace(k, "");
    };
    const auto widgets = QApplication::allWidgets();
    // Shared across all controls so deeply nested or very large models cannot
    // stall the GUI thread for an unbounded amount of time.
    int cellBudget = 20000;
    QSet<QMenu*> menusSeen;
    QSet<QAbstractItemModel*> modelsSeen;
    for (QWidget* w : widgets) {
        if (auto* lbl = qobject_cast<QLabel*>(w)) add(labelSource(lbl));
        if (auto* btn = qobject_cast<QAbstractButton*>(w)) add(btn->text());
        // Capture the UI hint, never user-entered names, paths, email or passwords.
        if (auto* le = qobject_cast<QLineEdit*>(w)) add(le->placeholderText());
        if (auto* cb = qobject_cast<QComboBox*>(w)) {
            add(cb->currentText());
            for (int i = 0; i < cb->count(); ++i) add(cb->itemText(i));
        }
        if (auto* tb = qobject_cast<QTabBar*>(w))
            for (int i = 0; i < tb->count(); ++i) add(tb->tabText(i));
        if (!w->toolTip().isEmpty()) add(w->toolTip());
        if (auto* menu = qobject_cast<QMenu*>(w))
            collectMenuActions(menu, add, menusSeen);
        if (auto* mb = qobject_cast<QMenuBar*>(w))
            for (QAction* a : mb->actions()) {
                add(a->text());
                if (a->menu()) collectMenuActions(a->menu(), add, menusSeen);
            }
        if (auto* tb2 = qobject_cast<QToolBar*>(w))
            for (QAction* a : tb2->actions()) add(a->text());
        if (auto* view = qobject_cast<QAbstractItemView*>(w)) {
            if (view->model() && cellBudget > 0 && !modelsSeen.contains(view->model())) {
                modelsSeen.insert(view->model());
                collectModelDeep(view->model(), QModelIndex(), add,
                                 0, cellBudget);
            }
        }
    }


    // Save to the user's Desktop so it is easy to find.
    wchar_t desktop[MAX_PATH] = {};
    if (FAILED(SHGetFolderPathW(nullptr, CSIDL_DESKTOPDIRECTORY, nullptr, 0, desktop)) ||
        !desktop[0])
        return;
    const std::wstring path =
        std::wstring(desktop) + L"\\TopoGun_untranslated_zh.json";

    // Load whatever was captured on a previous press so we only ADD new keys
    // and never clobber existing ones (including any values already filled in).
    std::map<std::string, std::string> merged;
    QString saveError;
    {
        QFile f(QString::fromStdWString(path));
        if (f.exists() && !f.open(QIODevice::ReadOnly)) {
            saveError = f.errorString();
        } else if (f.isOpen()) {
            QJsonParseError err{};
            const QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &err);
            if (err.error == QJsonParseError::NoError && doc.isObject() &&
                doc.object().value(QLatin1String("translations")).isObject()) {
                const QJsonObject tr = doc.object()
                                           .value(QLatin1String("translations"))
                                           .toObject();
                for (auto it = tr.constBegin(); it != tr.constEnd(); ++it) {
                    if (!it.value().isString()) {
                        saveError = QStringLiteral("已有采集文件包含非文本译文，已保留原文件。");
                        break;
                    }
                    merged[it.key().toStdString()] = it.value().toString().toStdString();
                }
            } else saveError = QStringLiteral("已有采集文件格式无效，已保留原文件，请先备份并修复。");
        }
    }
    int added = 0;
    for (const auto& kv : pending) {
        if (merged.emplace(kv.first, "").second) ++added;
    }
    // 有新增才写文件（避免无意义改写）；无论新增多少都弹窗反馈。
    if (added > 0 && saveError.isEmpty()) {
        std::string json = "{\n  \"$schema\": \"sp-translation-v1\",\n"
                           "  \"id\": \"topogun-untranslated\",\n"
                           "  \"language\": \"zh-CN\",\n"
                           "  \"description\": \"Shift+` 增量采集的未翻译词条（待填写）\",\n"
                           "  \"translations\": {\n";
        bool first = true;
        for (const auto& kv : merged) {
            if (!first) json += ",\n";
            json += "    \"" + jsonEscape(kv.first) + "\": \"" + jsonEscape(kv.second) + "\"";
            first = false;
        }
        json += "\n  }\n}\n";
        QSaveFile output(QString::fromStdWString(path));
        if (!output.open(QIODevice::WriteOnly) ||
            output.write(json.data(), static_cast<qint64>(json.size())) != static_cast<qint64>(json.size()) ||
            !output.commit()) saveError = output.errorString();
    }
    // Defer the pop-up until after this shortcut event is fully processed, so
    // a modal dialog is never shown from inside the event filter.
    const int         addedCount = added;
    const int         totalCount = static_cast<int>(merged.size());
    const std::wstring savedPath = path;
    QTimer::singleShot(0, QApplication::instance(), [addedCount, totalCount, savedPath, saveError]() {
        if (!saveError.isEmpty()) {
            QMessageBox::warning(nullptr, QStringLiteral("采集保存失败"), saveError);
            return;
        }
        QMessageBox::information(
            nullptr, QStringLiteral("未翻译词条"),
            QStringLiteral("已捕获 ") + QString::number(addedCount) +
                QStringLiteral(" 条（共 ") + QString::number(totalCount) +
                QStringLiteral(" 条）。\n已保存到桌面：\n") +
                QString::fromStdWString(savedPath));
    });
}

class TranslatorFilter : public QObject {
public:
    using QObject::QObject;
    bool eventFilter(QObject* object, QEvent* event) override {
        // QMessageBox's wrapped plain-text labels can use QTextLayout rather
        // than exported QPainter::drawText, bypassing the import hooks. Paint
        // only the dialog's standard message labels; keep their source intact.
        if (g_enabled && event->type() == QEvent::Paint) {
            auto* label = qobject_cast<QLabel*>(object);
            if (label && qobject_cast<QMessageBox*>(label->window()) &&
                (label->objectName() == QLatin1String("qt_msgbox_label") ||
                 label->objectName() == QLatin1String("qt_msgbox_informativelabel")) &&
                label->textFormat() != Qt::RichText &&
                !label->text().contains(QLatin1Char('<'))) {
                const QString text = translated(label->text());
                if (!text.isEmpty()) {
                    QPainter painter(label);
                    QStyleOption option;
                    option.initFrom(label);
                    label->style()->drawPrimitive(QStyle::PE_Widget, &option, &painter, label);
                    const int margin = label->margin();
                    QRect rect = label->contentsRect().adjusted(margin, margin, -margin, -margin);
                    const int indent = label->indent();
                    if (indent > 0) rect.adjust(indent, 0, 0, 0);
                    int flags = static_cast<int>(label->alignment());
                    if (label->wordWrap()) flags |= Qt::TextWordWrap;
                    label->style()->drawItemText(&painter, rect, flags,
                        label->palette(), label->isEnabled(), text, label->foregroundRole());
                    return true;
                }
            }
        }
        // Reserve the configured key before Qt dispatches a host QAction
        // shortcut. Applies to every configured key, including F1-F24.
        // KeyPress alone runs too late in that case.
        if (!g_hotkeyDlgOpen && event->type() == QEvent::ShortcutOverride) {
            auto* key = static_cast<QKeyEvent*>(event);
            if (key->key() == g_toggleKey && key->modifiers() == Qt::NoModifier) {
                key->accept();
                return true;
            }
        }
        if (event->type() == QEvent::KeyPress) {
            auto* ke = static_cast<QKeyEvent*>(event);
            // While the rebind dialog is open, it owns the keyboard to capture a
            // new hotkey.  Never toggle / sniff in that window, or pressing F3
            // to rebind would also flip the language.
            if (g_hotkeyDlgOpen) return false;
            // Shift + ` (the key that also yields ~) dumps untranslated text.
            // The reported key is Key_AsciiTilde (~) on most layouts, or
            // Key_QuoteLeft (`) where the shift doesn't remap it: accept both.
            const bool isTildeKey = ke->key() == Qt::Key_AsciiTilde ||
                                    ke->key() == Qt::Key_QuoteLeft;
            if (isTildeKey && (ke->modifiers() & Qt::ShiftModifier)) {
                if (!ke->isAutoRepeat()) captureUntranslated();
                return true;
            }
            if (ke->key() == g_toggleKey && ke->modifiers() == Qt::NoModifier) {
                if (!ke->isAutoRepeat()) toggleTranslation();
                return true;
            }
            return false;
        }
        if (!g_enabled) return false;
        if (event->type() == QEvent::Show || event->type() == QEvent::Polish) {
            if (auto* w = qobject_cast<QWidget*>(object))
                applyWidget(w);
            if (object && qobject_cast<QMenuBar*>(object) &&
                event->type() == QEvent::Show)
                installAuthorLink();
        }
        return false;
    }
};

// ---------------------------------------------------------------------------
// QPainter::drawText IAT hook (custom-painted text) -- mirrors Substance.
// ---------------------------------------------------------------------------
typedef void(__fastcall* DrawPoint)(QPainter*, const QPoint&, const QString&);
typedef void(__fastcall* DrawPointF)(QPainter*, const QPointF&, const QString&);
typedef void(__fastcall* DrawRect)(QPainter*, const QRect&, int, const QString&, QRect*);
typedef void(__fastcall* DrawRectFOption)(QPainter*, const QRectF&, const QString&, const QTextOption&);
typedef void(__fastcall* DrawRectFAlign)(QPainter*, const QRectF&, int, const QString&, QRectF*);
typedef void(__fastcall* DrawXY)(QPainter*, int, int, const QString&);
typedef void(__fastcall* DrawXYWH)(QPainter*, int, int, int, int, int, const QString&, QRect*);
typedef void(__fastcall* DrawPointF2)(QPainter*, const QPointF&, const QString&, int, int);

static DrawPoint     g_drawPoint     = nullptr;
static DrawPointF    g_drawPointF    = nullptr;
static DrawRect      g_drawRect      = nullptr;
static DrawRectFOption g_drawRectFOption = nullptr;
static DrawRectFAlign g_drawRectFAlign = nullptr;
static DrawXY        g_drawXY        = nullptr;
static DrawXYWH      g_drawXYWH      = nullptr;
static DrawPointF2   g_drawPointF2   = nullptr;

static QString translateElidedUncached(const QString& text) {
    QString base = text;
    base.chop(text.endsWith(QChar(0x2026)) ? 1 : 3);
    base = base.trimmed();
    if (base.size() < 2) return {};
    const std::string low = base.toLower().toUtf8().toStdString();
    auto it = std::lower_bound(
        g_sortedKeys.begin(), g_sortedKeys.end(), low,
        [](const auto& entry, const std::string& value) { return entry.first < value; });
    const std::string* best = nullptr;
    for (; it != g_sortedKeys.end(); ++it) {
        if (it->first.compare(0, low.size(), low) != 0) break;
        if (!best || it->second.size() < best->size()) best = &it->second;
    }
    return best ? translated(QString::fromUtf8(best->c_str())) : QString();
}

// A style can also be called for an exported image. A QWidget paint target or
// a known visible widget owner establishes the display-cache scope.
static bool isDisplayPainter(const QPainter* painter) {
    return painter && painter->isActive() && UiPaintBoundary::isDisplayPainter(painter);
}

static QString translatePaint(QPainter* painter, const QString& text) {
    if (!g_enabled) return {};
    if (text.isEmpty() || containsCjk(text)) return {};
    // Cheap reject for values that can never be UI text (numbers, tiny tokens),
    // so the expensive QLabel recovery + fuzzy-normalization path is not hit on
    // every draw (the paint hook runs for every drawText in the whole app).
    if (text.size() <= 1) return {};
    bool allValue = true;
    for (QChar c : text) {
        if (c.isDigit() || c == QLatin1Char('.') || c == QLatin1Char(',') ||
            c == QLatin1Char('%') || c == QLatin1Char('-') || c == QLatin1Char('+') ||
            c == QLatin1Char('e') || c == QLatin1Char('E') || c == QLatin1Char(' '))
            continue;
        allValue = false; break;
    }
    if (allValue) return {};
    // Recover the FULL source from the painted control (Substance approach):
    // an elided QLabel draws "Mater…" but keeps "Material" in its tooltip /
    // property, so translating the elided fragment misses. Read the complete
    // source from the widget being painted and translate that instead. This is
    // what the delegate does for item views (full DisplayRole) and what the
    // graph/elided-label paths do via the owning property / tooltip.
    if (QWidget* dev = (painter ? dynamic_cast<QWidget*>(painter->device()) : nullptr)) {
        if (auto* lbl = qobject_cast<QLabel*>(dev)) {
            const QString full = labelSource(lbl);
            if (!full.isEmpty() && full != text) {
                const QString fullTr = translated(full);
                if (!fullTr.isEmpty()) return fullTr;
            }
        }
    }
    QString tr = translated(text);
    if (!tr.isEmpty()) return tr;
    // Elided-text recovery: Qt elides by keeping the head and appending "…", so
    // a collapsed label passes "Mater…" to drawText, which never matches the
    // full key "Material". Find the dictionary key that begins with the elided
    // head (prefer the shortest match) so collapsed/truncated labels still
    // translate. Uses the sorted key table for O(log N + matches) instead of a
    // full scan, so it stays cheap on every paint.
    if (text.endsWith(QChar(0x2026)) || text.endsWith(QLatin1String("..."))) {
        // Do not cache the QLabel recovery above: identical fragments can
        // belong to different controls whose complete source text differs.
        auto& cache = translationCache().elided;
        if (const QString* result = cache.object(text)) return *result;
        const QString result = translateElidedUncached(text);
        cacheResult(cache, text, result);
        return result;
    }
    return {};
}

// Host style sheets may bypass drawItemText. Tint only our labels at the
// final painter boundary, restoring the original pen after every draw.
class PluginLabelPen final {
    QPainter* painter_ = nullptr;
    QPen previous_;
public:
    PluginLabelPen(QPainter* painter, const QString& text) {
        if (!isDisplayPainter(painter) || (text != QStringLiteral("中/英") &&
            text != QStringLiteral("Bilibili 神说要凑数汉化"))) return;
        painter_ = painter;
        previous_ = painter->pen();
        // Cover the full native selection rectangle so no pale border remains.
        auto* bar = qobject_cast<QMenuBar*>(dynamic_cast<QWidget*>(painter->device()));
        if (bar && bar->property("topogun_hover_label").toString() == text) {
            for (QAction* action : bar->actions()) {
                if (action->text() != text) continue;
                painter->save();
                painter->setPen(Qt::NoPen);
                painter->setBrush(QColor(0x3D, 0x50, 0x66));
                painter->drawRect(bar->actionGeometry(action));
                painter->restore();
                break;
            }
        }
        QPen blue = previous_;
        blue.setColor(QColor(0x66, 0xAA, 0xFF));
        painter->setPen(blue);
    }
    ~PluginLabelPen() { if (painter_) painter_->setPen(previous_); }
};

static void hookedDrawPoint(QPainter* p, const QPoint& pt, const QString& t) {
    PluginLabelPen color(p, t);
    QString tr = isDisplayPainter(p) ? translatePaint(p, t) : QString();
    if (g_drawPoint) g_drawPoint(p, pt, tr.isEmpty() ? t : tr);
}
static void hookedDrawPointF(QPainter* p, const QPointF& pt, const QString& t) {
    PluginLabelPen color(p, t);
    QString tr = isDisplayPainter(p) ? translatePaint(p, t) : QString();
    if (g_drawPointF) g_drawPointF(p, pt, tr.isEmpty() ? t : tr);
}
static void hookedDrawRect(QPainter* p, const QRect& r, int f, const QString& t, QRect* br) {
    PluginLabelPen color(p, t);
    QString tr = isDisplayPainter(p) ? translatePaint(p, t) : QString();
    if (g_drawRect) g_drawRect(p, r, f, tr.isEmpty() ? t : tr, br);
}
static void hookedDrawRectFOption(QPainter* p, const QRectF& r, const QString& t, const QTextOption& o) {
    PluginLabelPen color(p, t);
    QString tr = isDisplayPainter(p) ? translatePaint(p, t) : QString();
    if (g_drawRectFOption) g_drawRectFOption(p, r, tr.isEmpty() ? t : tr, o);
}
static void hookedDrawRectFAlign(QPainter* p, const QRectF& r, int f, const QString& t, QRectF* br) {
    PluginLabelPen color(p, t);
    QString tr = isDisplayPainter(p) ? translatePaint(p, t) : QString();
    if (g_drawRectFAlign) g_drawRectFAlign(p, r, f, tr.isEmpty() ? t : tr, br);
}
static void hookedDrawXY(QPainter* p, int x, int y, const QString& t) {
    PluginLabelPen color(p, t);
    QString tr = isDisplayPainter(p) ? translatePaint(p, t) : QString();
    if (g_drawXY) g_drawXY(p, x, y, tr.isEmpty() ? t : tr);
}
static void hookedDrawXYWH(QPainter* p, int x, int y, int w, int h, int f, const QString& t, QRect* br) {
    PluginLabelPen color(p, t);
    QString tr = isDisplayPainter(p) ? translatePaint(p, t) : QString();
    if (g_drawXYWH) g_drawXYWH(p, x, y, w, h, f, tr.isEmpty() ? t : tr, br);
}
static void hookedDrawPointF2(QPainter* p, const QPointF& pt, const QString& t, int f, int d) {
    PluginLabelPen color(p, t);
    QString tr = isDisplayPainter(p) ? translatePaint(p, t) : QString();
    if (g_drawPointF2) g_drawPointF2(p, pt, tr.isEmpty() ? t : tr, f, d);
}

// Patch every import slot in `module` that points at `original`.
static bool replaceImports(HMODULE module, void* original, void* replacement) {
    auto* base = reinterpret_cast<unsigned char*>(module);
    auto* dos = reinterpret_cast<IMAGE_DOS_HEADER*>(base);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) return false;
    auto* nt = reinterpret_cast<IMAGE_NT_HEADERS*>(base + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE) return false;
    const DWORD importRva = nt->OptionalHeader
        .DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT].VirtualAddress;
    if (!importRva) return false;
    bool replaced = false;
    auto* desc = reinterpret_cast<IMAGE_IMPORT_DESCRIPTOR*>(base + importRva);
    for (; desc->Name; ++desc) {
        if (!desc->FirstThunk) continue;
        auto* thunk = reinterpret_cast<IMAGE_THUNK_DATA*>(base + desc->FirstThunk);
        for (; thunk->u1.Function; ++thunk) {
            void** slot = reinterpret_cast<void**>(&thunk->u1.Function);
            if (*slot != original) continue;
            DWORD old = 0;
            if (!VirtualProtect(slot, sizeof(void*), PAGE_READWRITE, &old)) continue;
            *slot = replacement;
            DWORD ign = 0;
            VirtualProtect(slot, sizeof(void*), old, &ign);
            FlushInstructionCache(GetCurrentProcess(), slot, sizeof(void*));
            replaced = true;
        }
    }
    return replaced;
}

// Patch the IAT slot in `module` that imports the symbol `name` so it calls
// `replacement`, and report the ORIGINAL resolved address via *outOriginal.
// Unlike replaceImports(), this matches by the import NAME, not by address.  That
// matters for symbols such as QFontMetrics::size which the loader resolves as a
// normal import (QFontMetrics::size is in Qt5Widgets' IAT pointing into
// Qt5Gui.dll) even though pefile cannot find "size" in Qt5Gui's export table.
// Hooking it is what lets the menu bar / group box / header -- which measure
// text through fm.size() -- re-measure at Chinese width.
static bool replaceImportByName(HMODULE module, const char* name, void* replacement,
                                void** outOriginal) {
    if (outOriginal) *outOriginal = nullptr;
    auto* base = reinterpret_cast<unsigned char*>(module);
    auto* dos = reinterpret_cast<IMAGE_DOS_HEADER*>(base);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) return false;
    auto* nt = reinterpret_cast<IMAGE_NT_HEADERS*>(base + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE) return false;
    const DWORD importRva = nt->OptionalHeader
        .DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT].VirtualAddress;
    if (!importRva) return false;
    auto* desc = reinterpret_cast<IMAGE_IMPORT_DESCRIPTOR*>(base + importRva);
    for (; desc->Name; ++desc) {
        if (!desc->OriginalFirstThunk || !desc->FirstThunk) continue;
        auto* oft = reinterpret_cast<IMAGE_THUNK_DATA*>(base + desc->OriginalFirstThunk);
        auto* ftn = reinterpret_cast<IMAGE_THUNK_DATA*>(base + desc->FirstThunk);
        for (DWORD i = 0; oft[i].u1.AddressOfData; ++i) {
            const ULONGLONG ordinalFlag = 0x8000000000000000ULL;   // IMAGE_ORDINAL_FLAG64
            if (oft[i].u1.AddressOfData & ordinalFlag) continue;    // imported by ordinal
            const char* pname = reinterpret_cast<const char*>(
                base + oft[i].u1.AddressOfData + 2);               // skip 2-byte hint
            if (std::strcmp(pname, name) != 0) continue;
            void** slot = reinterpret_cast<void**>(&ftn[i].u1.Function);
            if (outOriginal) *outOriginal = *slot;
            DWORD old = 0;
            if (!VirtualProtect(slot, sizeof(void*), PAGE_READWRITE, &old)) return false;
            *slot = replacement;
            DWORD ign = 0;
            VirtualProtect(slot, sizeof(void*), old, &ign);
            FlushInstructionCache(GetCurrentProcess(), slot, sizeof(void*));
            return true;
        }
    }
    return false;
}

// ---------------------------------------------------------------------------
// QFontMetrics measurement hooks, scoped to Qt5Widgets' UI layout code.
//
// Qt layouts calculate sizeHint from the widget's ORIGINAL English text. The
// pure-paint path translates only at drawText time, so the layout can leave too
// little space for the wider Chinese rendering. Mirroring Toolbag's approach,
// translate only Qt5Widgets' measurement calls: the widget keeps English
// internally while its layout/eliding sees the Chinese glyph bounds. Hooking
// TopoGun.exe or arbitrary plug-ins here could also change exported text
// geometry or other non-UI calculations, so those callers are left untouched.
// ---------------------------------------------------------------------------
typedef int (__fastcall* FmHAdv)(const QFontMetrics*, const QString&, int);
typedef int (__fastcall* FmHAdvOpt)(const QFontMetrics*, const QString&, const QTextOption&);
// MSVC x64 returns QRect/QSize/QString through a hidden sret pointer. The
// function pointer types below model the raw ABI explicitly: RCX = this,
// RDX = sret, then the ordinary Qt arguments in registers/stack.  This is the
// MSVC x64 member-function convention and matches TopoGun's Qt build (which
// is also MSVC x64); a MinGW or 32-bit build would need different modelling,
// so we hard-fail the build on anything other than 64-bit.
static_assert(sizeof(void*) == 8, "TopoGunChinese hooks assume MSVC x64 (8-byte pointers)");
typedef void* (__fastcall* FmBRectRaw)(const QFontMetrics* self, QRect* result, const QString& text);
typedef void* (__fastcall* FmBRectOptRaw)(const QFontMetrics* self, QRect* result, const QString& text, const QTextOption& opt);
typedef void* (__fastcall* FmBRectRectRaw)(const QFontMetrics* self, QRect* result, const QRect& rect, int flags, const QString& text, int tabstops, int* tabarray);
typedef void* (__fastcall* FmBRectXYWHRaw)(const QFontMetrics* self, QRect* result, int x, int y, int w, int h, int flags, const QString& text, int tabstops, int* tabarray);
typedef void* (__fastcall* FmSizeRaw)(const QFontMetrics* self, QSize* result, int flags, const QString& text, int tabstops, int* tabarray);
typedef void* (__fastcall* FmTBRectRaw)(const QFontMetrics* self, QRect* result, const QString& text);
typedef void* (__fastcall* FmTBRectOptRaw)(const QFontMetrics* self, QRect* result, const QString& text, const QTextOption& opt);
typedef void* (__fastcall* FmElideRaw)(const QFontMetrics* self, QString* result, const QString& text, Qt::TextElideMode mode, int width, int flags);

typedef qreal (__fastcall* FmFHAdv)(const QFontMetricsF*, const QString&, int);
typedef qreal (__fastcall* FmFHAdvOpt)(const QFontMetricsF*, const QString&, const QTextOption&);
typedef void* (__fastcall* FmFBRectRaw)(const QFontMetricsF* self, QRectF* result, const QString& text);
typedef void* (__fastcall* FmFBRectOptRaw)(const QFontMetricsF* self, QRectF* result, const QString& text, const QTextOption& opt);
typedef void* (__fastcall* FmFBRectRectRaw)(const QFontMetricsF* self, QRectF* result, const QRectF& rect, int flags, const QString& text, int tabstops, int* tabarray);
typedef void* (__fastcall* FmFSizeRaw)(const QFontMetricsF* self, QSizeF* result, int flags, const QString& text, int tabstops, int* tabarray);
typedef void* (__fastcall* FmFTBRectRaw)(const QFontMetricsF* self, QRectF* result, const QString& text);
typedef void* (__fastcall* FmFTBRectOptRaw)(const QFontMetricsF* self, QRectF* result, const QString& text, const QTextOption& opt);
typedef void* (__fastcall* FmFElideRaw)(const QFontMetricsF* self, QString* result, const QString& text, Qt::TextElideMode mode, qreal width, int flags);

static FmHAdv     g_fmHAdv      = nullptr;
static FmHAdvOpt  g_fmHAdvOpt   = nullptr;
static FmBRectRaw g_fmBRect     = nullptr;
static FmBRectOptRaw g_fmBRectOpt = nullptr;
static FmBRectRectRaw g_fmBRectRect = nullptr;
static FmBRectXYWHRaw g_fmBRectXYWH = nullptr;
static FmSizeRaw  g_fmSize      = nullptr;
static FmTBRectRaw g_fmTBRect   = nullptr;
static FmTBRectOptRaw g_fmTBRectOpt = nullptr;
static FmElideRaw g_fmElide     = nullptr;
static FmFHAdv    g_fmfHAdv     = nullptr;
static FmFHAdvOpt g_fmfHAdvOpt  = nullptr;
static FmFBRectRaw g_fmfBRect   = nullptr;
static FmFBRectOptRaw g_fmfBRectOpt = nullptr;
static FmFBRectRectRaw g_fmfBRectRect = nullptr;
static FmFSizeRaw g_fmfSize     = nullptr;
static FmFTBRectRaw g_fmfTBRect = nullptr;
static FmFTBRectOptRaw g_fmfTBRectOpt = nullptr;
static FmFElideRaw g_fmfElide   = nullptr;
static bool       g_fontHooksInstalled = false;
// Set only after every resolved QFontMetrics symbol is verified to be a real,
// executable code address.  When false the whole measurement layer is skipped
// (never installs / never calls the raw-ABI pointers) rather than risking a
// crash on a mismatched calling convention.
static bool       g_fontMeasureReady = false;
static DWORD      g_metricGuiThread = 0;

static QString measureText(const QString& text) {
    if (!g_enabled || GetCurrentThreadId() != g_metricGuiThread ||
        text.isEmpty() || containsCjk(text)) return {};
    return translated(text);
}

static int hookedFontHAdv(const QFontMetrics* fm, const QString& text, int len) {
    QString t = text;
    // Prefix lengths index the source string (cursor/selection layout). Only
    // whole-string measurements can be replaced by the full translation.
    QString tr = (len < 0 || len >= text.size()) ? measureText(text) : QString();
    if (!tr.isEmpty()) t = tr;
    if (!tr.isEmpty()) len = -1;
    if (g_fmHAdv) return g_fmHAdv(fm, t, len);
    return 0;
}
static int hookedFontHAdvOpt(const QFontMetrics* fm, const QString& text, const QTextOption& opt) {
    QString t = text;
    QString tr = measureText(text);
    if (!tr.isEmpty()) t = tr;
    if (g_fmHAdvOpt) return g_fmHAdvOpt(fm, t, opt);
    return 0;
}
static void* hookedFontBRect(const QFontMetrics* fm, QRect* result, const QString& text) {
    QString t = text;
    QString tr = measureText(text);
    if (!tr.isEmpty()) t = tr;
    if (g_fmBRect) g_fmBRect(fm, result, t);
    return result;
}
static void* hookedFontBRectOpt(const QFontMetrics* fm, QRect* result, const QString& text, const QTextOption& opt) {
    QString t = text;
    QString tr = measureText(text);
    if (!tr.isEmpty()) t = tr;
    if (g_fmBRectOpt) g_fmBRectOpt(fm, result, t, opt);
    return result;
}
static void* hookedFontBRectRect(const QFontMetrics* fm, QRect* result, const QRect& r, int flags, const QString& text, int tabstops, int* tabarray) {
    QString t = text;
    QString tr = measureText(text);
    if (!tr.isEmpty()) t = tr;
    if (g_fmBRectRect) g_fmBRectRect(fm, result, r, flags, t, tabstops, tabarray);
    return result;
}
static void* hookedFontBRectXYWH(const QFontMetrics* fm, QRect* result, int x, int y, int w, int h, int flags, const QString& text, int tabstops, int* tabarray) {
    QString t = text;
    QString tr = measureText(text);
    if (!tr.isEmpty()) t = tr;
    if (g_fmBRectXYWH) g_fmBRectXYWH(fm, result, x, y, w, h, flags, t, tabstops, tabarray);
    return result;
}
static void* hookedFontSize(const QFontMetrics* fm, QSize* result, int flags, const QString& text, int tabstops, int* tabarray) {
    QString t = text;
    QString tr = measureText(text);
    if (!tr.isEmpty()) t = tr;
    if (g_fmSize) g_fmSize(fm, result, flags, t, tabstops, tabarray);
    return result;
}
static void* hookedFontTBRect(const QFontMetrics* fm, QRect* result, const QString& text) {
    QString t = text;
    QString tr = measureText(text);
    if (!tr.isEmpty()) t = tr;
    if (g_fmTBRect) g_fmTBRect(fm, result, t);
    return result;
}
static void* hookedFontTBRectOpt(const QFontMetrics* fm, QRect* result, const QString& text, const QTextOption& opt) {
    QString t = text;
    QString tr = measureText(text);
    if (!tr.isEmpty()) t = tr;
    if (g_fmTBRectOpt) g_fmTBRectOpt(fm, result, t, opt);
    return result;
}
static void* hookedFontElide(const QFontMetrics* fm, QString* result, const QString& text, Qt::TextElideMode mode, int width, int flags) {
    QString t = text;
    QString tr = measureText(text);
    if (!tr.isEmpty()) t = tr;
    if (g_fmElide) g_fmElide(fm, result, t, mode, width, flags);
    else *result = t;
    return result;
}

static qreal hookedFontFHAdv(const QFontMetricsF* fm, const QString& text, int len) {
    QString t = text;
    QString tr = (len < 0 || len >= text.size()) ? measureText(text) : QString();
    if (!tr.isEmpty()) t = tr;
    if (!tr.isEmpty()) len = -1;
    if (g_fmfHAdv) return g_fmfHAdv(fm, t, len);
    return 0;
}
static qreal hookedFontFHAdvOpt(const QFontMetricsF* fm, const QString& text, const QTextOption& opt) {
    QString t = text;
    QString tr = measureText(text);
    if (!tr.isEmpty()) t = tr;
    if (g_fmfHAdvOpt) return g_fmfHAdvOpt(fm, t, opt);
    return 0;
}
static void* hookedFontFBRect(const QFontMetricsF* fm, QRectF* result, const QString& text) {
    QString t = text;
    QString tr = measureText(text);
    if (!tr.isEmpty()) t = tr;
    if (g_fmfBRect) g_fmfBRect(fm, result, t);
    return result;
}
static void* hookedFontFBRectOpt(const QFontMetricsF* fm, QRectF* result, const QString& text, const QTextOption& opt) {
    QString t = text;
    QString tr = measureText(text);
    if (!tr.isEmpty()) t = tr;
    if (g_fmfBRectOpt) g_fmfBRectOpt(fm, result, t, opt);
    return result;
}
static void* hookedFontFBRectRect(const QFontMetricsF* fm, QRectF* result, const QRectF& r, int flags, const QString& text, int tabstops, int* tabarray) {
    QString t = text;
    QString tr = measureText(text);
    if (!tr.isEmpty()) t = tr;
    if (g_fmfBRectRect) g_fmfBRectRect(fm, result, r, flags, t, tabstops, tabarray);
    return result;
}
static void* hookedFontFSize(const QFontMetricsF* fm, QSizeF* result, int flags, const QString& text, int tabstops, int* tabarray) {
    QString t = text;
    QString tr = measureText(text);
    if (!tr.isEmpty()) t = tr;
    if (g_fmfSize) g_fmfSize(fm, result, flags, t, tabstops, tabarray);
    return result;
}
static void* hookedFontFTBRect(const QFontMetricsF* fm, QRectF* result, const QString& text) {
    QString t = text;
    QString tr = measureText(text);
    if (!tr.isEmpty()) t = tr;
    if (g_fmfTBRect) g_fmfTBRect(fm, result, t);
    return result;
}
static void* hookedFontFTBRectOpt(const QFontMetricsF* fm, QRectF* result, const QString& text, const QTextOption& opt) {
    QString t = text;
    QString tr = measureText(text);
    if (!tr.isEmpty()) t = tr;
    if (g_fmfTBRectOpt) g_fmfTBRectOpt(fm, result, t, opt);
    return result;
}
static void* hookedFontFElide(const QFontMetricsF* fm, QString* result, const QString& text, Qt::TextElideMode mode, qreal width, int flags) {
    QString t = text;
    QString tr = measureText(text);
    if (!tr.isEmpty()) t = tr;
    if (g_fmfElide) g_fmfElide(fm, result, t, mode, width, flags);
    else *result = t;
    return result;
}

// True if p is a committed, executable code address (a real function body, not
// a null / data / freed pointer).  GetProcAddress() normally returns such an
// address; this is a cheap defensive check so the font layer can abandon itself
// if any resolved symbol looks anomalous.
static bool isExecutableCodeAddr(const void* p) {
    if (!p) return false;
    MEMORY_BASIC_INFORMATION mbi{};
    if (!VirtualQuery(p, &mbi, sizeof(mbi))) return false;
    if (mbi.State != MEM_COMMIT || mbi.Protect == PAGE_NOACCESS) return false;
    const DWORD exec = PAGE_EXECUTE | PAGE_EXECUTE_READ |
                       PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY;
    return (mbi.Protect & exec) != 0;
}

// Validate every QFontMetrics/QFontMetricsF symbol that GetProcAddress resolved.
// A null symbol is acceptable (that overload is simply skipped), but a NON-null
// symbol that does not point into executable code means the resolution is bogus,
// so we abandon the whole measurement layer instead of calling into a bad ABI.
static bool fontSymbolsAreValid() {
    const void* ptrs[] = {
        (void*)g_fmHAdv,      (void*)g_fmHAdvOpt,   (void*)g_fmBRect,
        (void*)g_fmBRectOpt,  (void*)g_fmBRectRect, (void*)g_fmBRectXYWH,
        (void*)g_fmSize,      (void*)g_fmTBRect,    (void*)g_fmTBRectOpt,
        (void*)g_fmElide,
        (void*)g_fmfHAdv,     (void*)g_fmfHAdvOpt,  (void*)g_fmfBRect,
        (void*)g_fmfBRectOpt, (void*)g_fmfBRectRect,(void*)g_fmfSize,
        (void*)g_fmfTBRect,   (void*)g_fmfTBRectOpt,(void*)g_fmfElide,
    };
    for (const void* p : ptrs)
        if (p && !isExecutableCodeAddr(p)) return false;
    return true;
}

// Redirect only Qt5Widgets' QFontMetrics imports. Qt5Widgets owns standard
// widget layout and eliding, while host and plug-in modules may also measure
// text for scene assets or exports. Do not patch those modules globally.
static void patchFontModules() {
    if (!g_fontMeasureReady) return;   // layer disabled: never patch/call
    struct Sym { const char* name; void* orig; void* hook; };
    Sym syms[] = {
        {"FMHAdv",      (void*)g_fmHAdv,      (void*)&hookedFontHAdv},
        {"FMHAdvOpt",   (void*)g_fmHAdvOpt,   (void*)&hookedFontHAdvOpt},
        {"FMBRect",     (void*)g_fmBRect,     (void*)&hookedFontBRect},
        {"FMBRectOpt",  (void*)g_fmBRectOpt,  (void*)&hookedFontBRectOpt},
        {"FMBRectRect", (void*)g_fmBRectRect, (void*)&hookedFontBRectRect},
        {"FMBRectXYWH", (void*)g_fmBRectXYWH, (void*)&hookedFontBRectXYWH},
        {"FMSize",      (void*)g_fmSize,      (void*)&hookedFontSize},
        {"FMTBRect",    (void*)g_fmTBRect,    (void*)&hookedFontTBRect},
        {"FMTBRectOpt", (void*)g_fmTBRectOpt, (void*)&hookedFontTBRectOpt},
        {"FMElide",     (void*)g_fmElide,     (void*)&hookedFontElide},
        {"FMfHAdv",     (void*)g_fmfHAdv,     (void*)&hookedFontFHAdv},
        {"FMfHAdvOpt",  (void*)g_fmfHAdvOpt,  (void*)&hookedFontFHAdvOpt},
        {"FMfBRect",    (void*)g_fmfBRect,    (void*)&hookedFontFBRect},
        {"FMfBRectOpt", (void*)g_fmfBRectOpt, (void*)&hookedFontFBRectOpt},
        {"FMfBRectRect",(void*)g_fmfBRectRect,(void*)&hookedFontFBRectRect},
        {"FMfSize",     (void*)g_fmfSize,     (void*)&hookedFontFSize},
        {"FMfTBRect",   (void*)g_fmfTBRect,   (void*)&hookedFontFTBRect},
        {"FMfTBRectOpt",(void*)g_fmfTBRectOpt,(void*)&hookedFontFTBRectOpt},
        {"FMfElide",    (void*)g_fmfElide,    (void*)&hookedFontFElide},
    };
    HMODULE widgets = GetModuleHandleW(kWidgetsModule);
    if (!widgets) return;
    for (const auto& symbol : syms) {
        if (symbol.orig) replaceImports(widgets, symbol.orig, symbol.hook);
    }
    void* original = nullptr;
    if (replaceImportByName(widgets,
            "?size@QFontMetrics@@QEBA?AVQSize@@HAEBVQString@@HPEAH@Z",
            (void*)&hookedFontSize, &original) && !g_fmSize && original)
        g_fmSize = (FmSizeRaw)original;
    original = nullptr;
    if (replaceImportByName(widgets,
            "?size@QFontMetricsF@@QEBA?AVQSizeF@@HAEBVQString@@HPEAH@Z",
            (void*)&hookedFontFSize, &original) && !g_fmfSize && original)
        g_fmfSize = (FmFSizeRaw)original;
}

static void installFontMeasurementHooks() {
    if (g_fontHooksInstalled) return;
    g_metricGuiThread = GetCurrentThreadId();
    HMODULE qtGui = GetModuleHandleW(kGuiModule);
    if (!qtGui) return;

    g_fmHAdv = (FmHAdv)GetProcAddress(qtGui,
        "?horizontalAdvance@QFontMetrics@@QEBAHAEBVQString@@H@Z");
    g_fmHAdvOpt = (FmHAdvOpt)GetProcAddress(qtGui,
        "?horizontalAdvance@QFontMetrics@@QEBAHAEBVQString@@AEBVQTextOption@@@Z");
    g_fmBRect = (FmBRectRaw)GetProcAddress(qtGui,
        "?boundingRect@QFontMetrics@@QEBA?AVQRect@@AEBVQString@@@Z");
    g_fmBRectOpt = (FmBRectOptRaw)GetProcAddress(qtGui,
        "?boundingRect@QFontMetrics@@QEBA?AVQRect@@AEBVQString@@AEBVQTextOption@@@Z");
    g_fmBRectRect = (FmBRectRectRaw)GetProcAddress(qtGui,
        "?boundingRect@QFontMetrics@@QEBA?AVQRect@@AEBV2@HAEBVQString@@HPEAH@Z");
    g_fmBRectXYWH = (FmBRectXYWHRaw)GetProcAddress(qtGui,
        "?boundingRect@QFontMetrics@@QEBA?AVQRect@@HHHHHAEBVQString@@HPEAH@Z");
    g_fmSize = (FmSizeRaw)GetProcAddress(qtGui,
        "?size@QFontMetrics@@QEBA?AVQSize@@HAEBVQString@@HPEAH@Z");
    g_fmTBRect = (FmTBRectRaw)GetProcAddress(qtGui,
        "?tightBoundingRect@QFontMetrics@@QEBA?AVQRect@@AEBVQString@@@Z");
    g_fmTBRectOpt = (FmTBRectOptRaw)GetProcAddress(qtGui,
        "?tightBoundingRect@QFontMetrics@@QEBA?AVQRect@@AEBVQString@@AEBVQTextOption@@@Z");
    g_fmElide = (FmElideRaw)GetProcAddress(qtGui,
        "?elidedText@QFontMetrics@@QEBA?AVQString@@AEBV2@W4TextElideMode@Qt@@HH@Z");

    g_fmfHAdv = (FmFHAdv)GetProcAddress(qtGui,
        "?horizontalAdvance@QFontMetricsF@@QEBANAEBVQString@@H@Z");
    g_fmfHAdvOpt = (FmFHAdvOpt)GetProcAddress(qtGui,
        "?horizontalAdvance@QFontMetricsF@@QEBANAEBVQString@@AEBVQTextOption@@@Z");
    g_fmfBRect = (FmFBRectRaw)GetProcAddress(qtGui,
        "?boundingRect@QFontMetricsF@@QEBA?AVQRectF@@AEBVQString@@@Z");
    g_fmfBRectOpt = (FmFBRectOptRaw)GetProcAddress(qtGui,
        "?boundingRect@QFontMetricsF@@QEBA?AVQRectF@@AEBVQString@@AEBVQTextOption@@@Z");
    g_fmfBRectRect = (FmFBRectRectRaw)GetProcAddress(qtGui,
        "?boundingRect@QFontMetricsF@@QEBA?AVQRectF@@AEBV2@HAEBVQString@@HPEAH@Z");
    g_fmfSize = (FmFSizeRaw)GetProcAddress(qtGui,
        "?size@QFontMetricsF@@QEBA?AVQSizeF@@HAEBVQString@@HPEAH@Z");
    g_fmfTBRect = (FmFTBRectRaw)GetProcAddress(qtGui,
        "?tightBoundingRect@QFontMetricsF@@QEBA?AVQRectF@@AEBVQString@@@Z");
    g_fmfTBRectOpt = (FmFTBRectOptRaw)GetProcAddress(qtGui,
        "?tightBoundingRect@QFontMetricsF@@QEBA?AVQRectF@@AEBVQString@@AEBVQTextOption@@@Z");
    g_fmfElide = (FmFElideRaw)GetProcAddress(qtGui,
        "?elidedText@QFontMetricsF@@QEBA?AVQString@@AEBV2@W4TextElideMode@Qt@@NH@Z");

    g_fontHooksInstalled = true;
    // Fail-safe: only turn the measurement layer on if every resolved symbol is
    // a genuine executable address.  On any anomaly, abandon the whole layer --
    // the app degrades to English-width layout instead of risking a raw-ABI
    // call that could corrupt the stack.
    g_fontMeasureReady = fontSymbolsAreValid();
    if (g_fontMeasureReady) patchFontModules();
}

// (Re)scan every loaded module and redirect its QPainter::drawText imports.
// Idempotent: replaceImports() only rewrites slots that still point at the
// ORIGINAL, so re-running patches any DLL that loaded after the first pass.
static void patchDrawTextModules() {
    struct Sym { const char* name; void* orig; void* hook; };
    Sym syms[] = {
        {"Point",     (void*)g_drawPoint,     (void*)&hookedDrawPoint},
        {"PointF",    (void*)g_drawPointF,    (void*)&hookedDrawPointF},
        {"Rect",      (void*)g_drawRect,      (void*)&hookedDrawRect},
        {"RectF",     (void*)g_drawRectFOption,(void*)&hookedDrawRectFOption},
        {"RectFAlign",(void*)g_drawRectFAlign, (void*)&hookedDrawRectFAlign},
        {"XY",        (void*)g_drawXY,        (void*)&hookedDrawXY},
        {"XYWH",      (void*)g_drawXYWH,      (void*)&hookedDrawXYWH},
        {"PointF2",   (void*)g_drawPointF2,   (void*)&hookedDrawPointF2},
    };
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, GetCurrentProcessId());
    if (snap != INVALID_HANDLE_VALUE) {
        MODULEENTRY32W mod{}; mod.dwSize = sizeof(mod);
        if (Module32FirstW(snap, &mod)) {
            do {
                // Patch every module EXCEPT our own hook DLL and Qt5Core. We
                // MUST include Qt5Widgets/Gui: QComboBox and other Qt-styled
                // widgets draw their text through Qt's own QPainter::drawText,
                // which would otherwise escape translation.
                if (_wcsnicmp(mod.szModule, kHookDllPrefix, wcslen(kHookDllPrefix)) == 0) continue;
                if (_wcsicmp(mod.szModule, kCoreModule) == 0) continue;
                HMODULE h = GetModuleHandleW(mod.szModule);
                if (!h) continue;
                for (auto& s : syms) {
                    if (s.orig) replaceImports(h, s.orig, s.hook);
                }
            } while (Module32NextW(snap, &mod));
        }
        CloseHandle(snap);
    }
}

static void installDrawTextHooks() {
    if (g_hooksInstalled) return;
    HMODULE qtGui = GetModuleHandleW(kGuiModule);
    if (!qtGui) return;
    g_drawPoint = (DrawPoint)GetProcAddress(qtGui,
        "?drawText@QPainter@@QEAAXAEBVQPoint@@AEBVQString@@@Z");
    g_drawPointF = (DrawPointF)GetProcAddress(qtGui,
        "?drawText@QPainter@@QEAAXAEBVQPointF@@AEBVQString@@@Z");
    g_drawRect = (DrawRect)GetProcAddress(qtGui,
        "?drawText@QPainter@@QEAAXAEBVQRect@@HAEBVQString@@PEAV2@@Z");
    g_drawRectFOption = (DrawRectFOption)GetProcAddress(qtGui,
        "?drawText@QPainter@@QEAAXAEBVQRectF@@AEBVQString@@AEBVQTextOption@@@Z");
    g_drawRectFAlign = (DrawRectFAlign)GetProcAddress(qtGui,
        "?drawText@QPainter@@QEAAXAEBVQRectF@@HAEBVQString@@PEAV2@@Z");
    g_drawXY = (DrawXY)GetProcAddress(qtGui,
        "?drawText@QPainter@@QEAAXHHAEBVQString@@@Z");
    g_drawXYWH = (DrawXYWH)GetProcAddress(qtGui,
        "?drawText@QPainter@@QEAAXHHHHHAEBVQString@@PEAVQRect@@@Z");
    g_drawPointF2 = (DrawPointF2)GetProcAddress(qtGui,
        "?drawText@QPainter@@QEAAXAEBVQPointF@@AEBVQString@@HH@Z");
    g_hooksInstalled = true;
    patchDrawTextModules();
}

// ---------------------------------------------------------------------------
// GUI-thread worker: waits for QApplication, then installs the engine.
// ---------------------------------------------------------------------------
static const QString kAuthorUrl = QStringLiteral("https://space.bilibili.com/281243426?spm_id_from=333.1007.0.0");



static void repaintAll() {
    for (QWidget* w : QApplication::allWidgets()) {
        if (w && w->isVisible()) w->update();
    }
    // The dock / central content can finish re-laying-out on a deferred
    // LayoutRequest *after* this toggle returns, which would leave the panels
    // showing the previous frame.  Repaint again on the next loop iteration so
    // the custom / docked controls redraw at the settled Chinese/English width.
    QTimer::singleShot(0, QApplication::instance(), [] {
        for (QWidget* w : QApplication::allWidgets())
            if (w && w->isVisible()) w->update();
    });
}

static void refreshLayouts() {
    for (QWidget* w : QApplication::allWidgets()) {
        // QMenuBar's internal layout uses cached action geometries and does not
        // tolerate a blanket invalidate during startup; invalidating it can make
        // actionGeometry() return garbage and blank the top menu bar. Leave menu
        // bars/menus alone here; toggleTranslation() nudges the menu bar with a
        // StyleChange (which triggers its itemsDirty -> calcActionRects) when the
        // user actually switches language.
        if (qobject_cast<QMenuBar*>(w) || qobject_cast<QMenu*>(w))
            continue;
        if (w) w->updateGeometry();
        if (w && w->layout()) w->layout()->invalidate();
    }
}


static QPointer<QAction> g_langAction;
static QPointer<QAction> g_authorAction;
static QPointer<QMenuBar> g_authorLinkBar;   // kept for the hotkey-dialog parent

static void openHotkeyDialog() {
    static QPointer<ToggleHotkeyDialog> g_hotkeyDlg;
    if (g_hotkeyDlg) {
        // A hidden dialog may still have a deferred delete pending. Do not
        // create a second one whose shared state the old destructor can clear.
        if (g_hotkeyDlg->isVisible()) {
            g_hotkeyDlg->raise();
            g_hotkeyDlg->activateWindow();
        }
        return;
    }
    QWidget* parent = g_authorLinkBar ? g_authorLinkBar->window() : nullptr;
    auto* dlg = new ToggleHotkeyDialog(parent, g_toggleKey, g_hotkeyDlgOpen,
        ChineseHotkeyConfig::path(QStringLiteral("TopoGunChinese")), g_authorLinkBar.data());
    // Keep the shared dialog's compact layout and scoped styling.
    g_hotkeyDlg = dlg;
    g_hotkeyDlgOpen = true;
    QObject::connect(dlg, &QObject::destroyed, [] {
        g_hotkeyDlg = nullptr;
        g_hotkeyDlgOpen = false;
    });
    dlg->show(); dlg->raise();
}

class LangMouseFilter : public QObject {
public:
    LangMouseFilter(QObject* p) : QObject(p) {}
    bool eventFilter(QObject* obj, QEvent* ev) override {
        auto* mb = qobject_cast<QMenuBar*>(obj);
        if (!mb) return false;
        if (ev->type() == QEvent::MouseMove) {
            auto* me = static_cast<QMouseEvent*>(ev);
            const bool overLink =
                (g_langAction &&
                 mb->actionGeometry(g_langAction).contains(me->pos())) ||
                (g_authorAction &&
                 mb->actionGeometry(g_authorAction).contains(me->pos()));
            if (overLink) mb->setCursor(Qt::PointingHandCursor);
            else mb->unsetCursor();
            QAction* hovered = mb->actionAt(me->pos());
            const QString label = overLink && hovered ? hovered->text() : QString();
            if (mb->property("topogun_hover_label").toString() != label) {
                mb->setProperty("topogun_hover_label", label);
                mb->update();
            }
            return false;
        }
        if (ev->type() == QEvent::Leave) {
            mb->unsetCursor();
            mb->setProperty("topogun_hover_label", QString());
            mb->update();
            return false;
        }
        // Qt reports the second press of a double-click as MouseButtonDblClick,
        // not MouseButtonPress. Handle both so a double-click's second release
        // still reaches the toggle below instead of falling through to the
        // menu bar (which caused the "not following the hand" lag).
        if (obj && (ev->type() == QEvent::MouseButtonPress ||
                    ev->type() == QEvent::MouseButtonDblClick) && g_langAction) {
            if (!ev->spontaneous()) return false;           // ignore synthetic events
            auto* me = static_cast<QMouseEvent*>(ev);
            if (mb && mb->actionGeometry(g_langAction).contains(me->pos())) {
                if (me->button() == Qt::RightButton) {
                    openHotkeyDialog();
                    return true;                       // consume right-click on 中/英
                }
                if (me->button() == Qt::LeftButton) {
                    pressed_ = true;
                    return true;                       // consume left-click on 中/英
                }
            }
        } else if (obj && ev->type() == QEvent::MouseButtonRelease && g_langAction && pressed_) {
            if (!ev->spontaneous()) return false;           // ignore synthetic events
            auto* me = static_cast<QMouseEvent*>(ev);
            if (me->button() == Qt::LeftButton) {
                pressed_ = false;
                if (mb && mb->actionGeometry(g_langAction).contains(me->pos()))
                    g_langAction->trigger();            // single action on release over 中/英
                return true;
            }
        }
        return false;
    }
private:
    bool pressed_ = false;
};

static void installAuthorLink() {
    if (g_authorLinkBar) return;      // already installed on this bar
    QMenuBar* mb = nullptr;
    for (QWidget* w : QApplication::allWidgets()) {
        if (auto* menu = qobject_cast<QMenuBar*>(w)) {
            if (menu->actions().size() >= 4) { mb = menu; break; }
        }
    }
    if (!mb) return;

    // Locate Help so the attribution sits right after it.
    QAction* helpAction = nullptr;
    for (QAction* a : mb->actions()) {
        QString t = a->text(); t.remove(QLatin1Char('&'));
        if (t.contains(QStringLiteral("帮助")) || t.contains(QStringLiteral("Help"))) {
            helpAction = a; break;
        }
    }
    if (!helpAction) return;

    // "中/英" as a real menu-bar action -> inherits the exact Help item style.
    QAction* langAct = new QAction(QStringLiteral("中/英"), mb);
    langAct->setObjectName(QStringLiteral("topogun_lang"));
    // Mouse releases above are consumed, so routing them through the same
    // action also supports keyboard/accessibility activation without toggling twice.
    QObject::connect(langAct, &QAction::triggered, langAct, [] { toggleTranslation(); });
    g_langAction = langAct;
    mb->installEventFilter(new LangMouseFilter(mb));   // right-click -> hotkey dialog
    mb->setMouseTracking(true);
    const auto acts = mb->actions();
    int idx = helpAction ? acts.indexOf(helpAction) : -1;
    QAction* after = (idx >= 0 && idx + 1 < acts.size()) ? acts[idx + 1] : nullptr;
    if (after) mb->insertAction(after, langAct); else mb->addAction(langAct);

    // Author link as a real menu-bar action (same style as 中/英), each
    // opening its link on click.  Their labels are tinted blue in
    // ChineseProxyStyle::drawItemText.
    QAction* afterLang = nullptr;
    {
        const auto as = mb->actions();
        int li = as.indexOf(langAct);
        afterLang = (li >= 0 && li + 1 < as.size()) ? as[li + 1] : nullptr;
    }
    auto addLink = [&](const QString& label, const QString& url,
                       const char* obj) -> QAction* {
        auto* act = new QAction(label, mb);
        act->setObjectName(QLatin1String(obj));
        act->setData(url);
        QObject::connect(act, &QAction::triggered, [act] {
            const QString u = act->data().toString();
            if (!u.isEmpty()) QDesktopServices::openUrl(QUrl(u));
        });
        if (afterLang) mb->insertAction(afterLang, act);
        else mb->addAction(act);
        return act;
    };
    g_authorAction = addLink(QStringLiteral("Bilibili 神说要凑数汉化"),
                             kAuthorUrl, "topogun_author");


    g_authorLinkBar = mb;
}

static void RunOnGuiThread(QApplication* app) {
    g_plugInDir = QString::fromStdWString(SelfDirW());
    QString hotkeyError;
    g_toggleKey = ChineseHotkeyConfig::load(
        ChineseHotkeyConfig::path(QStringLiteral("TopoGunChinese")), &hotkeyError);
    if (!hotkeyError.isEmpty()) OutputDebugStringW(
        reinterpret_cast<LPCWSTR>(hotkeyError.utf16()));
    // Phase 1: QApplication exists and execution is on its GUI thread.
    // Install every hook that can be resolved immediately.
    LoadDictionaries();
    installProxyStyle();
    applyTranslations();
    installDrawTextHooks();
    installFontMeasurementHooks();
    refreshLayouts();
    app->installEventFilter(new TranslatorFilter(app));
    installAuthorLink();

    // Phase 2: TranslatorFilter handles widgets created later. Phase 3 is an
    // optional bounded fallback for plug-in DLLs that load after startup and
    // therefore escape the initial drawText IAT scan. Font measurement is
    // intentionally limited to Qt5Widgets, which is already loaded here.
    if (!TopoGunHookLifecycle::kEnableDeferredPolling)
        return;

    auto* rescanTimer = new QTimer(app);
    rescanTimer->setInterval(TopoGunHookLifecycle::kDeferredScanIntervalMs);
    QObject::connect(rescanTimer, &QTimer::timeout, [] {
        patchDrawTextModules();
    });
    rescanTimer->start();
    QTimer::singleShot(TopoGunHookLifecycle::kDeferredScanWindowMs, app,
                       [rescanTimer] {
                           rescanTimer->stop();
                           rescanTimer->deleteLater();
                       });
}

static DWORD WINAPI Worker(LPVOID) {
    constexpr unsigned requiredQtMajor = 5;
    if (!TopoGunQtCompatibility::supportedRuntime(qVersion(),
                                                     requiredQtMajor)) {
        return 2;
    }
    HMODULE pinned = nullptr;
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                           GET_MODULE_HANDLE_EX_FLAG_PIN,
                           reinterpret_cast<LPCWSTR>(g_selfModule), &pinned))
        return 3;
    QApplication* app = nullptr;
    // Wait up to ~300s for QApplication. TopoGun can block on licensing /
    // update checks before the GUI is created, so allow a generous window and
    // report a clear diagnostic if we ever give up.
    for (int i = 0;
         i < TopoGunHookLifecycle::kApplicationWaitAttempts; ++i) {
        if (QCoreApplication::instance()) {
            app = qobject_cast<QApplication*>(QCoreApplication::instance());
            if (app) break;
        }
        Sleep(TopoGunHookLifecycle::kApplicationWaitIntervalMs);
    }
    if (!app) {
        return 1;
    }
    QMetaObject::invokeMethod(app, [app](){
        try { RunOnGuiThread(app); }
        catch (...) { OutputDebugStringW(L"TopoGunChinese initialization failed"); }
    }, Qt::QueuedConnection);
    return 0;
}

// ---------------------------------------------------------------------------
BOOL WINAPI DllMain(HINSTANCE hinst, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(hinst);
        g_selfModule = hinst;

        HANDLE h = CreateThread(nullptr, 0, Worker, nullptr, 0, nullptr);
        if (h) CloseHandle(h);
    }
    return TRUE;
}
