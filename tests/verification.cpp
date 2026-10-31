#define main applicationMain
#include "../main.cpp"
#undef main
#include <QtTest/QTest>
#include <QTemporaryDir>
#include <QProcess>
#include <QDir>
#include <iostream>

static int failures = 0;
static void check(bool condition, const char *message) {
    if (!condition) { std::cerr << "FAIL: " << message << '\n'; ++failures; }
}

struct Verification {
    static int restored(int expected) {
        SpeedWindow window;
        if (window.unitSelector->currentIndex() != expected)
            std::cerr << "Expected " << expected << " got " << window.unitSelector->currentIndex()
                      << " settings=" << displaySettings().fileName().toStdString()
                      << " raw=" << displaySettings().value("display/unit").toString().toStdString() << '\n';
        check(window.unitSelector->currentIndex() == expected, "preference restored in a new process");
        return failures ? 1 : 0;
    }
    static void run(const QString &captureDir, const QString &settingsDir) {
        check(formatSpeed(8e6, SpeedUnit::Kbps) == "8000.0 Kbps", "Kbps conversion");
        check(formatSpeed(8e6, SpeedUnit::Mbps) == "8.00 Mbps", "Mbps conversion");
        check(formatSpeed(1e9, SpeedUnit::Gbps) == "1.00 Gbps", "Gbps conversion");
        check(formatSpeed(8e6, SpeedUnit::KBs) == "1000.0 KB/s", "bits to KB/s");
        check(formatSpeed(8e6, SpeedUnit::MBs) == "1.00 MB/s", "bits to MB/s");
        check(formatSpeed(785300, SpeedUnit::Gbps) == "0.001 Gbps", "small fixed Gbps value");
        check(automaticUnit(999999) == SpeedUnit::Kbps, "auto below Mbps boundary");
        check(automaticUnit(1e6) == SpeedUnit::Mbps, "auto Mbps boundary");
        check(automaticUnit(999999999) == SpeedUnit::Mbps, "auto below Gbps boundary");
        check(automaticUnit(1e9) == SpeedUnit::Gbps, "auto Gbps boundary");

        SpeedWindow window;
        check(window.timer.interval() == 500 && window.timer.isActive(), "500 ms update timer");
        check(window.hasPrevious, "Windows traffic statistics available");
        const int initial = window.chart->downloads.size();
        QTest::qWait(1600);
        check(window.chart->downloads.size() >= initial + 2, "live sampling advances history");
        window.timer.stop();
        window.chart->downloads.clear();
        window.chart->uploads.clear();
        for (int i = 0; i < 121; ++i) window.chart->append(i * 1e5, i * 2e4);
        check(window.chart->downloads.size() == 120 && window.chart->uploads.size() == 120, "60 seconds / 120 samples");
        check(window.chart->downloads.first() == 1e5, "oldest sample discarded");
        window.currentDown = 8e6;
        window.currentUp = 2e6;
        const auto oldDown = window.chart->downloads;
        const auto oldUp = window.chart->uploads;
        const char *names[] = {"Mbps", "Kbps", "Mbps", "Gbps", "KB/s", "MB/s"};
        window.show();
        QTest::qWait(50);
        for (int index = 0; index < 6; ++index) {
            window.unitSelector->setCurrentIndex(index);
            window.refreshDisplay();
            const SpeedUnit expected = index == 0 ? SpeedUnit::Mbps : static_cast<SpeedUnit>(index);
            check(window.chart->unit == expected, "chart shares selected unit");
            check(window.downloadValue->text().endsWith(names[index]) && window.uploadValue->text().endsWith(names[index]), "both cards share selected unit");
            check(window.downloadValue->text() == formatSpeed(8e6, expected), "download conversion in UI");
            check(window.uploadValue->text() == formatSpeed(2e6, expected), "upload conversion in UI");
            check(window.chart->downloads == oldDown && window.chart->uploads == oldUp, "unit change retains raw history");
            if (index > 0) // index 0 是初始值，setCurrentIndex 不會觸發儲存
                check(displaySettings().value("display/unit").toString() == QLatin1String(unitKeys[index]),
                      "unit preference stored as text key");
            auto preferences = displaySettings();
            // 交替寫入字串與舊版整數，兩種格式都要能還原
            if (index % 2) preferences.setValue("display/unit", QLatin1String(unitKeys[index]));
            else preferences.setValue("display/unit", index);
            preferences.sync();
            QProcess child;
            child.start(QCoreApplication::applicationFilePath(), {"--check-preference", settingsDir, QString::number(index)});
            check(child.waitForFinished(10000) && child.exitCode() == 0, "persistent preference survives process restart");
            if (child.exitCode() != 0) std::cerr << child.readAllStandardError().constData();
            if (!captureDir.isEmpty()) {
                window.resize(780, 620);
                QTest::qWait(50);
                check(window.grab().save(captureDir + "/unit-" + QString::number(index) + ".png"), "save UI preview");
                window.resize(650, 500);
                QTest::qWait(50);
                check(window.grab().save(captureDir + "/minimum-" + QString::number(index) + ".png"), "save minimum-size preview");
            }
        }
        window.unitSelector->setCurrentIndex(0);
        window.chart->append(2e9, 1e9);
        window.refreshDisplay();
        check(window.chart->unit == SpeedUnit::Gbps && window.downloadValue->text().endsWith("Gbps"), "auto shares historical peak across cards and chart");
        for (int i = 0; i < 120; ++i) window.chart->append(8000, 4000);
        window.currentDown = 8000; window.currentUp = 4000;
        window.refreshDisplay();
        check(window.chart->unit == SpeedUnit::Kbps, "auto returns to lower unit when peak expires");
        auto invalidPreferences = displaySettings();
        invalidPreferences.setValue("display/unit", 99);
        invalidPreferences.sync();
        QProcess invalid;
        invalid.start(QCoreApplication::applicationFilePath(), {"--check-preference", settingsDir, "0"});
        check(invalid.waitForFinished(10000) && invalid.exitCode() == 0, "invalid preference falls back to auto");
        check(unitIndexFromSetting(QStringLiteral("bogus")) == 0 && unitIndexFromSetting(QStringLiteral("MB/s")) == 5
              && unitIndexFromSetting(QVariant()) == 0, "unit preference parsing");

        // 網卡選單：第一項是所有實體網卡、最後一項是全部介面；切換網卡會清空歷史並記住選擇
        check(window.adapterSelector->count() >= 2, "adapter selector populated");
        check(window.adapterSelector->itemData(0).toString() == physicalKey, "physical adapters first");
        const int allIndex = window.adapterSelector->count() - 1;
        check(window.adapterSelector->itemData(allIndex).toString() == allKey, "all interfaces last");
        window.chart->append(1e6, 1e6);
        window.adapterSelector->setCurrentIndex(allIndex);
        check(window.chart->downloads.isEmpty(), "changing adapter clears history");
        check(displaySettings().value("display/adapter").toString() == allKey, "adapter preference saved");
        window.adapterSelector->setCurrentIndex(0);
        check(displaySettings().value("display/adapter").toString() == physicalKey, "adapter preference restored to physical");

        check(window.aboutButton && window.aboutButton->isVisibleTo(&window), "about / license button present");
        check(SpeedWindow::licensesPath().endsWith(QLatin1String("/licenses")), "licenses folder next to the executable");

        // 讀取失敗的空檔（NaN）不影響峰值
        window.chart->clear();
        window.chart->append(std::numeric_limits<double>::quiet_NaN(), std::numeric_limits<double>::quiet_NaN());
        window.chart->append(5e6, 1e6);
        check(window.chart->peakSpeed() == 5e6, "NaN gaps ignored by peak");
        window.chart->setUnit(SpeedUnit::Mbps);
        const traffic::Axis axis = window.chart->axisInBits();
        check(axis.top == 6e6 && axis.intervals == 3, "chart axis uses round numbers in display unit");
    }
};

int main(int argc, char **argv) {
    QApplication app(argc, argv);
    app.setOrganizationName("RealtimeNetworkSpeed");
    app.setApplicationName("RealtimeNetworkSpeed");
    QSettings::setDefaultFormat(QSettings::IniFormat);
    if (app.arguments().size() == 4 && app.arguments()[1] == "--check-preference") {
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, app.arguments()[2]);
        return Verification::restored(app.arguments()[3].toInt());
    }
    QTemporaryDir settings(QDir::currentPath() + "/settings-test-XXXXXX");
    check(settings.isValid(), "isolated preference directory");
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settings.path());
    const QString captures = app.arguments().size() > 1 ? app.arguments()[1] : QString();
    if (!captures.isEmpty()) QDir().mkpath(captures);
    Verification::run(captures, settings.path());
    std::cout << (failures ? "FAILED" : "PASS") << ": conversion, auto thresholds, UI synchronization, history, live timer, process restart preferences, adapter selection\n";
    return failures ? 1 : 0;
}
