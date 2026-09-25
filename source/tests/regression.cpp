// Exercise real lookup, capture filtering and shortcut code without a host scene.
#include "../hook.cpp"
#include <QtCore/QTemporaryDir>
#include <chrono>
#include <iostream>
#include <stdexcept>
#include <thread>

static void require(bool pass, const char* message) {
    if (!pass) throw std::runtime_error(message);
}

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    try {
        require(argc == 2, "expected source directory");
        g_plugInDir = QString::fromLocal8Bit(argv[1]);
        LoadDictionaries();
        require(g_translations.size() == 1743, "dictionary entry loss");
        require(translated(QStringLiteral("Restore Defaults")) == QStringLiteral("恢复默认设置"), "button lookup");
        require(!translated(QStringLiteral("The scene is not saved, quit anyway?")).isEmpty(), "question lookup");
        require(translated(QStringLiteral("Tool Options (Mask)")) == QStringLiteral("工具选项（遮罩）"), "composed tool title");
        require(isTranslatableString(QStringLiteral("Quit anyway?")), "question capture");
        require(!isTranslatableString(QStringLiteral("https://example.test/?x=1")), "URL capture");
        g_enabled = false;
        require(translated(QStringLiteral("Restore Defaults")).isEmpty(), "disabled lookup");
        require(translatePaint(nullptr, QStringLiteral("Restore...")).isEmpty(), "disabled elided lookup");
        g_enabled = true;
        TranslatorFilter filter;
        QTemporaryDir temporary;
        const QString config = temporary.path() + QStringLiteral("/hotkeys.json");
        for (int functionKey = Qt::Key_F1; functionKey <= Qt::Key_F24; ++functionKey) {
            g_toggleKey = functionKey;
            QKeyEvent key(QEvent::ShortcutOverride, functionKey, Qt::NoModifier);
            key.ignore();
            require(filter.eventFilter(nullptr, &key) && key.isAccepted(), "function key reservation");
            QKeyEvent other(QEvent::ShortcutOverride,
                functionKey == Qt::Key_F1 ? Qt::Key_F2 : Qt::Key_F1, Qt::NoModifier);
            require(!filter.eventFilter(nullptr, &other), "unconfigured key must reach host");
            QKeyEvent modified(QEvent::ShortcutOverride, functionKey, Qt::ControlModifier);
            require(!filter.eventFilter(nullptr, &modified), "modified key must reach host");
            QKeyEvent repeat(QEvent::KeyPress, functionKey, Qt::NoModifier, QString(), true);
            const bool enabled = g_enabled;
            require(filter.eventFilter(nullptr, &repeat) && g_enabled == enabled, "no repeat toggle");
            g_hotkeyDlgOpen = true;
            QKeyEvent editing(QEvent::ShortcutOverride, functionKey, Qt::NoModifier);
            require(!filter.eventFilter(nullptr, &editing), "dialog owns key capture");
            g_hotkeyDlgOpen = false;
            require(ChineseHotkeyConfig::save(config, functionKey), "hotkey save");
            require(ChineseHotkeyConfig::load(config) == functionKey, "hotkey reload");
        }
        QFile corrupt(config); require(corrupt.open(QIODevice::WriteOnly), "open config");
        corrupt.write("{broken"); corrupt.close();
        require(ChineseHotkeyConfig::load(config) == Qt::Key_F3, "corrupt config fallback");
        // Compare cold and cached results for every dictionary entry and the
        // transformations used by the display and measurement paths.
        for (const auto& entry : g_translations) {
            const QString source = QString::fromUtf8(entry.first.c_str());
            const QStringList variants{source, "  " + source + "  ", source + " *",
                "&" + source, source.toUpper() + "..."};
            for (const QString& variant : variants) {
                const QString expected = translatedUncached(variant);
                require(translated(variant) == expected, "cold lookup equivalence");
                require(translated(variant) == expected, "warm lookup equivalence");
            }
        }
        std::atomic<bool> workerOk{true};
        std::thread worker([&] {
            for (int i = 0; i < 1000; ++i)
                if (translated(QStringLiteral("Tool Options (Mask)")) != QStringLiteral("工具选项（遮罩）"))
                    workerOk = false;
        });
        worker.join();
        require(workerOk, "independent thread lookup");
        for (int i = 0; i < 20000; ++i) {
            translated(QString::number(i) + " missing");
            translatePaint(nullptr, QString::number(i) + " missing...");
        }
        auto& cache = translationCache();
        require(cache.lookup.totalCost() <= kLookupCacheCost, "lookup cost bound");
        require(cache.elided.totalCost() <= kElidedCacheCost, "elided cost bound");
        require(cache.lookup.size() <= kLookupCacheCost / kCacheEntryOverhead, "lookup count bound");
        const QString longText(10000, QLatin1Char('x'));
        translated(longText);
        require(!cache.lookup.contains(longText), "oversized text not retained");
        require(translated(QStringLiteral("Restore Defaults")) == QStringLiteral("恢复默认设置"), "lookup after eviction");

        // Cache keys must own data, including strings from temporary raw buffers.
        QChar buffer[] = {QChar('x'), QChar('y'), QChar('z')};
        translated(QString::fromRawData(buffer, 3));
        buffer[0] = QChar('a');
        require(cache.lookup.contains(QStringLiteral("xyz")), "owned cache key");

        const QString savedDirectory = g_plugInDir;
        g_plugInDir = temporary.path();
        QDir().mkpath(g_plugInDir + "/translations");
        const QString fixturePath = g_plugInDir + "/translations/test_zh.json";
        auto writeFixture = [&](const QJsonObject& translations) {
            QFile file(fixturePath);
            require(file.open(QIODevice::WriteOnly), "open fixture");
            const QByteArray json = QJsonDocument(QJsonObject{{"translations", translations}}).toJson();
            require(file.write(json) == json.size(), "write fixture");
            file.close();
            LoadDictionaries();
        };
        writeFixture({{"Alpha title", QStringLiteral("标题甲")},
            {"Cache A", QStringLiteral("甲")}, {"Cache_A", QStringLiteral("乙")}});
        require(translated("CACHE A *").isEmpty(), "ambiguous match must not emit lone state suffix");
        require(translated("Cache A") == QStringLiteral("甲"), "ambiguous exact match retained");
        require(translated("Later label").isEmpty(), "cache miss before reload");
        require(translatePaint(nullptr, "Alph...") == QStringLiteral("标题甲"), "initial elided match");
        writeFixture({{"Alpha title", QStringLiteral("标题乙")}, {"Later label", QStringLiteral("后续标签")}});
        require(translated("Later label") == QStringLiteral("后续标签"), "reload invalidates negative cache");
        require(translatePaint(nullptr, "Alph...") == QStringLiteral("标题乙"), "reload invalidates elided cache");
        g_enabled = false;
        require(translated("Later label").isEmpty(), "cached hit disabled");
        require(measureText("Later label").isEmpty(), "cached measure disabled");
        g_enabled = true;
        require(translated("Later label") == QStringLiteral("后续标签"), "cached hit restored");
        g_plugInDir += "/missing";
        LoadDictionaries();
        require(g_sortedKeys.empty() && translated("Later label").isEmpty(), "missing folder clears indexes and cache");
        g_plugInDir = savedDirectory;
        LoadDictionaries();
        auto start = std::chrono::steady_clock::now();
        for (int i=0; i<200000; ++i) translated(QStringLiteral("Restore Defaults"));
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now()-start).count();
        std::cout << "PASS: all dictionary variants, capture, F1-F24, bounded cache/eviction/ownership, reload, threads, ambiguity\n"
                  << "200000 exact lookups: " << elapsed << " ms (not a host FPS benchmark)\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n'; return 1;
    }
}
