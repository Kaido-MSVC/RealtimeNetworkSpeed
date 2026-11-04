#define main applicationMain
#include "../main.cpp"
#undef main
#include <QtTest/QTest>
#include <QTemporaryDir>
#include <QProcess>
#include <QDir>
#include <QPointer>
#include <iostream>

static int failures = 0;
static void check(bool condition, const char *message) {
    if (!condition) { std::cerr << "FAIL: " << message << '\n'; ++failures; }
}

struct Verification {
    static bool nativeVisible(QWidget *widget) {
        const auto hwnd = reinterpret_cast<HWND>(widget->winId());
        return IsWindowVisible(hwnd) && !IsIconic(hwnd);
    }
    static int lifecycle(QApplication &app, const QString &mode) {
        SpeedWindow window;
        window.show();
        window.mini->show();
        bool quitting = false;
        QObject::connect(&app, &QCoreApplication::aboutToQuit, &window, [&] {
            quitting = true;
            check(!window.mini->isVisible(), "quit hides independent mini window");
        });
        QTimer::singleShot(100, &window, [&] {
            if (mode == "mini-close") {
                window.hide();
                window.mini->close();
                QTimer::singleShot(150, &window, [&] {
                    check(!quitting && window.timer.isActive(), "closing last visible mini does not quit tray monitoring");
                    app.exit(failures ? 1 : 0);
                });
            } else if (mode == "tray-quit") {
                window.closeToTray->setChecked(true);
                window.hide();
                window.tray->contextMenu()->actions().last()->trigger();
            } else {
                window.closeToTray->setChecked(false);
                window.close();
            }
        });
        const int result = app.exec();
        check(quitting, "application exits through event loop");
        return result || failures ? 1 : 0;
    }
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
        window.chart->clear();
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

        window.topmost->setChecked(true);
        check(window.windowFlags().testFlag(Qt::WindowStaysOnTopHint) && window.mini->windowFlags().testFlag(Qt::WindowStaysOnTopHint), "both windows stay on top");
        check(displaySettings().value("display/topmost").toBool(), "topmost persisted");
        {
            SpeedWindow reopened;
            check(reopened.topmost->isChecked() && reopened.windowFlags().testFlag(Qt::WindowStaysOnTopHint), "topmost restored from settings");
        }
        window.topmost->setChecked(false);
        check(!window.windowFlags().testFlag(Qt::WindowStaysOnTopHint), "topmost removable");
        check(window.miniValue->text().contains(window.downloadValue->text()), "mini speed synchronized");
        window.mini->show();
        QTest::qWait(100);
        check(window.mini->parentWidget() == nullptr &&
              GetWindow(reinterpret_cast<HWND>(window.mini->winId()), GW_OWNER) == nullptr,
              "mini has neither Qt parent nor native owner");
        const QPoint miniPosition = window.mini->pos() + QPoint(24, 24);
        window.mini->move(miniPosition);
        for (bool pinned : {false, true}) {
            window.topmost->setChecked(pinned);
            window.showMinimized();
            QTest::qWait(200);
            check(IsIconic(reinterpret_cast<HWND>(window.winId())) && nativeVisible(window.mini),
                  "native mini stays visible while main minimized, pinned and unpinned");
            window.showNormal();
            QTest::qWait(100);
            check(nativeVisible(window.mini) && window.mini->pos() == miniPosition,
                  "restoring main retains mini visibility and position");
        }
        window.topmost->setChecked(false);
        window.closeToTray->setChecked(true);
        check(displaySettings().value("display/closeToTray").toBool(), "close behavior persisted");
        if (QSystemTrayIcon::isSystemTrayAvailable()) {
            window.timer.start();
            const auto hiddenInitial = window.chart->times.last();
            check(!window.close(), "main close ignored when sent to tray");
            QTest::qWait(150);
            check(nativeVisible(window.mini), "mini stays natively visible when main hides in tray");
            check(!window.isVisible() && window.timer.isActive(), "close hides main window without exit");
            QTest::qWait(650);
            check(window.chart->times.last() > hiddenInitial, "sampling continues while hidden in tray");
            window.mini->show();
            check(window.mini->close() && !window.mini->isVisible() && window.timer.isActive(), "mini closes independently while monitoring in tray");
            window.timer.stop();
            window.tray->contextMenu()->actions().first()->trigger();
            check(window.isVisible() && !window.mini->isVisible(), "tray restores main without reopening closed mini");
            for (auto *button : window.findChildren<QPushButton *>())
                if (button->text() == QStringLiteral("迷你視窗")) button->click();
            check(nativeVisible(window.mini), "mini can reopen after independent close");
            window.hide();
            window.mini->findChild<QPushButton *>()->click();
            check(window.isVisible() && nativeVisible(window.mini), "mini restore button retains mini visibility");
        }
        window.closeToTray->setChecked(false);
        QPointer<QWidget> ownedMini;
        {
            SpeedWindow temporary;
            ownedMini = temporary.mini;
            temporary.mini->show();
        }
        check(ownedMini.isNull(), "main destruction deletes independent mini");
        for (const QString mode : {QString("mini-close"), QString("main-close"), QString("tray-quit")}) {
            QProcess child;
            child.start(QCoreApplication::applicationFilePath(), {"--check-lifecycle", settingsDir, mode});
            const bool finished = child.waitForFinished(10000);
            check(finished && child.exitStatus() == QProcess::NormalExit && child.exitCode() == 0,
                  qPrintable("event loop lifecycle: " + mode));
            if (!finished) { child.kill(); child.waitForFinished(); }
            if (child.exitCode() != 0) std::cerr << child.readAllStandardError().constData();
        }
        window.session.down = 123;
        for (auto *button : window.findChildren<QPushButton *>())
            if (button->text() == QStringLiteral("重設本次累積流量")) button->click();
        check(window.session.down == 0 && !window.hasPrevious, "manual reset clears totals and baseline");
        window.mini->show();
        if (!captureDir.isEmpty()) {
            window.resize(780, 700);
            window.grab().save(captureDir + "/v1.1-main.png");
            window.mini->grab().save(captureDir + "/v1.1-mini.png");
        }
        window.mini->hide();
        window.session.down = 100;
        // 網卡選單：第一項是所有實體網卡、最後一項是全部介面；切換網卡會清空歷史並記住選擇
        check(window.adapterSelector->count() >= 2, "adapter selector populated");
        check(window.adapterSelector->itemData(0).toString() == physicalKey, "physical adapters first");
        const int allIndex = window.adapterSelector->count() - 1;
        check(window.adapterSelector->itemData(allIndex).toString() == allKey, "all interfaces last");
        window.chart->append(1e6, 1e6);
        window.adapterSelector->setCurrentIndex(allIndex);
        check(window.session.down == 0, "adapter switch clears session");
        check(window.chart->downloads.isEmpty(), "changing adapter clears history");
        check(displaySettings().value("display/adapter").toString() == allKey, "adapter preference saved");
        window.adapterSelector->setCurrentIndex(0);
        check(displaySettings().value("display/adapter").toString() == physicalKey, "adapter preference restored to physical");

        check(window.aboutButton && window.aboutButton->isVisibleTo(&window), "about / license button present");
        check(SpeedWindow::licensesPath().endsWith(QLatin1String("/licenses")), "licenses folder next to the executable");

        // 網卡消失／恢復時，自動切換不得混用歷史。
        traffic::InterfaceInfo fake;
        fake.guid = "{test}"; fake.hardware = true;
        window.savedAdapter = QStringLiteral("{test}");
        window.updateAdapterList({fake});
        window.chart->append(123, 456);
        window.updateAdapterList({});
        check(window.chart->downloads.isEmpty(), "automatic fallback clears history");
        window.chart->append(123, 456);
        window.updateAdapterList({fake});
        check(window.chart->downloads.isEmpty(), "automatic restore clears history");
        window.sampleValid = false;
        window.refreshDisplay();
        check(window.downloadValue->text() == QStringLiteral("—"), "invalid sample hides stale speed");
        window.chart->clear();
        const auto t = SpeedChart::Time{};
        window.chart->append(100, 100, t);
        window.chart->append(200, 200, t + std::chrono::seconds(61));
        check(window.chart->downloads.size() == 1 && window.chart->times.size() == 1,
              "history expires by elapsed time");
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
    if (app.arguments().size() == 4 && app.arguments()[1] == "--check-lifecycle") {
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, app.arguments()[2]);
        return Verification::lifecycle(app, app.arguments()[3]);
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
