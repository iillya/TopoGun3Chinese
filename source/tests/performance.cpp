// Repeatable lookup microbenchmark; deliberately does not measure host FPS.
#include "../hook.cpp"
#include <chrono>
#include <iostream>

template<class Lookup>
static void benchmark(const char* name, const QStringList& corpus, Lookup lookup) {
    constexpr int calls = 200000;
    for (const QString& text : corpus) lookup(text);
    std::vector<double> samples;
    qint64 checksum = 0;
    for (int run = 0; run < 5; ++run) {
        auto start = std::chrono::steady_clock::now();
        for (int i = 0; i < calls; ++i)
            checksum += lookup(corpus.at(i % corpus.size())).size();
        samples.push_back(std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - start).count());
    }
    std::sort(samples.begin(), samples.end());
    std::cout << name << ": " << samples[2] << " ms / " << calls
              << " calls (median of 5); checksum=" << checksum << '\n';
}

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    if (argc != 2) return 1;
    g_plugInDir = QString::fromLocal8Bit(argv[1]);
    LoadDictionaries();
    QStringList exact, fuzzy, missing, elided;
    for (const auto& entry : g_sortedKeys) {
        const QString text = QString::fromUtf8(entry.second.c_str());
        exact << text;
        fuzzy << text.toUpper() + QStringLiteral("...");
        missing << QStringLiteral("Unknown UI text: ") + text;
        if (text.size() > 6) elided << text.left(5) + QChar(0x2026);
    }
    benchmark("exact", exact, translated);
    benchmark("normalized", fuzzy, translated);
    benchmark("unmatched", missing, translated);
    benchmark("elided paint", elided, [](const QString& text) { return translatePaint(nullptr, text); });
    benchmark("numeric paint", {"123.45", "507", "968", "100%"},
              [](const QString& text) { return translatePaint(nullptr, text); });
    g_enabled = false;
    benchmark("disabled measure", exact, measureText);
    return 0;
}
