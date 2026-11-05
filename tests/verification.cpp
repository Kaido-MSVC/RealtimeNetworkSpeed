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
    static void verifyV12(SpeedWindow &w, const QString &captureDir) {
        w.timer.stop();
        w.adapterSelector->setCurrentIndex(0);
        w.clearStatistics();
        traffic::InterfaceInfo nic;
        nic.luid = 1234; nic.guid = "{vpn-test}"; nic.alias = "測試,\"VPN\"\n介面";
        nic.description = "測試 VPN"; nic.up = true;
        nic.receiveLink = 1000000000; nic.transmitLink = 100000000;
        nic.mac = "00:11:22:33:44:55";
        nic.index = 876; nic.operStatus = 1; nic.adminStatus = 1;
        w.savedAdapter = "{vpn-test}";
        w.updateAdapterList({nic});
        check(w.selection().scope == traffic::Scope::Single, "virtual adapter individually selected");
        const auto t = std::chrono::steady_clock::now();
        auto sample = [&](int ms, std::uint64_t down, std::uint64_t up) {
            nic.inOctets = down; nic.outOctets = up;
            w.processSample({nic}, true, t + std::chrono::milliseconds(ms));
        };
        sample(0, 1000000000, 2000000000);
        check(!w.sampleValid && w.records.empty(), "first sample only establishes baseline");
        sample(500, 1000000500, 2000000100);
        sample(1500, 1000002500, 2000000500);
        check(w.session.down == 2500 && w.session.up == 500 && w.session.seconds == 1.5,
              "byte totals and time-weighted average duration");
        check(std::abs(w.session.averageDown() - 2500 * 8.0 / 1.5) < 1e-6 && w.session.peakDown == 16000,
              "weighted average and instantaneous peak");
        check(w.records.size() == 2 && w.records.back().session.down == 2500,
              "CSV records hold exact session snapshots");
        check(w.downloadValue->toolTip().contains(QStringLiteral("平均"))
              && w.miniValue->toolTip() == w.downloadValue->toolTip(), "hover statistics synced to mini");
        const QString frozen = w.downloadValue->text();
        w.setPaused(true);
        sample(2000, 9000000000, 9000000000);
        check(w.records.size() == 2 && w.session.down == 2500 && w.downloadValue->text() == frozen
              && !w.timer.isActive() && !w.hasPrevious, "pause freezes numbers and counters");
        check(w.mini->windowTitle().contains(QStringLiteral("已暫停")), "mini pause status visible");
        w.setPaused(false); // real baseline read must also be discarded before simulated sequence
        w.timer.stop(); w.hasPrevious = false;
        sample(2500, 9000000000, 9000000000);
        sample(3000, 9000000100, 9000000050);
        check(w.session.down == 2600 && w.records.size() == 3, "resume excludes pause traffic");
        const auto count = w.records.size();
        w.processSample({}, true, t + std::chrono::milliseconds(3500));
        check(w.adapterSelector->currentData().toString() == "{vpn-test}" && !w.sampleValid,
              "adapter unplug keeps selection and marks unavailable");
        sample(4000, 19000000000, 19000000000);
        check(w.records.size() == count && w.session.down == 2600, "reconnect baseline prevents spike");
        sample(4500, 19000000200, 19000000100);
        check(w.session.down == 2800, "reconnect subsequent delta counted");
        w.processSample({}, false, t + std::chrono::milliseconds(5000), "test read failure");
        sample(5500, 29000000000, 29000000000);
        check(w.session.down == 2800 && !w.sampleValid, "read failure recovery discards missing interval");
        sample(6000, 29000000300, 29000000200);
        sample(10000, 39000000000, 39000000000);
        check(w.session.down == 3100 && !w.sampleValid, "sleep gap excluded and unavailable");
        sample(10500, 39000000100, 39000000050);
        sample(11000, 10, 20);
        check(w.session.down == 3200 && !w.sampleValid, "counter reset excludes invalid delta");
        sample(11500, 110, 70);
        check(w.session.down == 3300 && w.session.peakDown == 16000, "counter reset re-baselines next interval");
        check(w.adapterDetails(w.lastInfos, {}).contains(QStringLiteral("00:11:22:33:44:55"))
              && w.adapterDetails(w.lastInfos, {}).contains(QStringLiteral("1.00 Gbps")), "adapter MAC and link details");
        QString error;
        const QString csv = captureDir.isEmpty() ? QDir::currentPath() + "/v12-test.csv" : captureDir + "/v12-test.csv";
        check(writeCsv(csv, w.records, error), "CSV atomic write");
        QFile file(csv); check(file.open(QIODevice::ReadOnly), "CSV read back");
        const QByteArray bytes = file.readAll(); file.close();
        check(bytes.startsWith(QByteArray::fromHex("efbbbf")) && bytes.contains("Download_Bps"), "CSV UTF-8 BOM and fixed header");
        check(bytes.contains(QStringLiteral("測試").toUtf8()) && bytes.contains("\"\"VPN\"\""), "CSV unicode and quotes escaped");
        check(csvText("=1+1") == QStringLiteral("\"'=1+1\"") && csvText("  @SUM(1)").contains(QChar(0x27)),
              "CSV formula-like names neutralized");
        check(!writeCsv(csv + "/missing.csv", w.records, error), "CSV path failure reported");
        QFile unchanged(csv); check(unchanged.open(QIODevice::ReadOnly), "read existing CSV after failure");
        check(unchanged.readAll() == bytes, "failed export preserves existing file"); unchanged.close();
        // Clean up only the temporary fixture when no caller requested capture artifacts.
        if (captureDir.isEmpty()) QFile::remove(csv);
        if (!captureDir.isEmpty()) {
            w.resize(850, 760); w.show(); w.mini->show(); QTest::qWait(50);
            w.grab().save(captureDir + "/v1.2-main.png");
            w.mini->grab().save(captureDir + "/v1.2-mini.png");
            w.resize(w.minimumSize()); QTest::qWait(50);
            w.grab().save(captureDir + "/v1.2-minimum.png");
        }
        w.adapterSelector->setCurrentIndex(0);
        const auto baselineInfos = w.lastInfos;
        QTimer::singleShot(100, &w, [&] {
            auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
            check(dialog != nullptr, "adapter info opens native dialog");
            if (dialog) {
                auto *text = dialog->findChild<QPlainTextEdit *>();
                check(text && text->isReadOnly() && !text->toPlainText().isEmpty(), "adapter info readable and copyable");
                check(text && text->toPlainText().contains(QStringLiteral("MAC："))
                      && text->toPlainText().contains(QStringLiteral("IP：")), "live adapter IP and MAC details displayed");
                if (!captureDir.isEmpty()) dialog->grab().save(captureDir + "/v1.2-info.png");
                dialog->accept();
            }
        });
        w.showAdapterInfo();
        check(w.lastInfos.size() == baselineInfos.size() && w.lastInfos.front().inOctets == baselineInfos.front().inOctets,
              "viewing adapter info does not alter sampling counters");
        w.setPaused(true);
        w.clearStatistics();
        check(w.session.down == 0 && w.session.up == 0 && w.session.seconds == 0
              && w.session.peakDown == 0 && w.session.peakUp == 0 && w.session.averageDown() == 0
              && w.records.empty() && w.chart->downloads.empty() && !w.hasPrevious && w.paused,
              "reset during pause clears all stats history and CSV but keeps paused state");
        w.setPaused(false); w.timer.stop();
        w.adapterSelector->setCurrentIndex(0);
        w.mini->hide();
    }

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
        check(window.windowFlags().testFlag(Qt::WindowStaysOnTopHint) && window.mini->windowFlags().testFlag(Qt::WindowStaysOnTopHint), "main topmost enabled; mini always topmost");
        check(displaySettings().value("display/topmost").toBool(), "topmost persisted");
        {
            SpeedWindow reopened;
            check(reopened.topmost->isChecked() && reopened.windowFlags().testFlag(Qt::WindowStaysOnTopHint), "topmost restored from settings");
        }
        window.topmost->setChecked(false);
        check(!window.windowFlags().testFlag(Qt::WindowStaysOnTopHint), "main topmost removable");
        check(window.mini->windowFlags().testFlag(Qt::WindowStaysOnTopHint)
              && window.mini->windowType() == Qt::Tool, "mini is an independent always-topmost tool");
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
            check(nativeVisible(window.mini), "native mini stays visible while main minimized");
            if (QSystemTrayIcon::isSystemTrayAvailable())
                check(!IsWindowVisible(reinterpret_cast<HWND>(window.winId())),
                      "minimized main is hidden and removed from taskbar");
            window.showNormal();
            QTest::qWait(100);
            check(nativeVisible(window.mini) && window.mini->pos() == miniPosition,
                  "restoring main retains mini visibility and position");
        }
        window.topmost->setChecked(false);
        const auto miniHwnd = reinterpret_cast<HWND>(window.mini->winId());
        const auto miniStyle = GetWindowLongPtr(miniHwnd, GWL_EXSTYLE);
        check((miniStyle & WS_EX_TOPMOST) && (miniStyle & WS_EX_TOOLWINDOW)
              && !(miniStyle & WS_EX_APPWINDOW), "native mini is topmost without taskbar entry");
        QWidget other;
        other.resize(700, 400);
        other.show();
        other.raise();
        other.activateWindow();
        QTest::qWait(200);
        check(nativeVisible(window.mini) && !window.mini->isActiveWindow()
              && (GetWindowLongPtr(miniHwnd, GWL_EXSTYLE) & WS_EX_TOPMOST),
              "mini remains visible and topmost when another window gets focus");
        other.hide();
        // Restoring before the queued minimize handler runs must win.
        window.showMinimized();
        window.showNormal();
        QTest::qWait(100);
        check(nativeVisible(&window) && nativeVisible(window.mini), "immediate restore is not hidden by queued minimize");
        check(!(GetWindowLongPtr(reinterpret_cast<HWND>(window.winId()), GWL_EXSTYLE) & WS_EX_TOOLWINDOW),
              "restored main retains normal taskbar window style");
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
            if (button->text() == QStringLiteral("清空本次統計")) button->click();
        check(window.session.down == 0 && !window.hasPrevious, "manual reset clears totals and baseline");
        window.mini->show();
        if (!captureDir.isEmpty()) {
            window.resize(780, 700);
            window.grab().save(captureDir + "/v1.1-main.png");
            window.mini->grab().save(captureDir + "/v1.1-mini.png");
        }
        window.mini->hide();
        verifyV12(window, captureDir);
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
        check(window.adapterSelector->currentData().toString() == "{test}" && window.chart->downloads.size() == 1,
              "missing selected adapter retained without fallback or clearing session");
        window.chart->append(123, 456);
        window.updateAdapterList({fake});
        check(window.adapterSelector->currentData().toString() == "{test}" && window.chart->downloads.size() == 2,
              "returning adapter retains selection and history");
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
