#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00
#endif
#ifndef WINVER
#define WINVER 0x0A00
#endif
#ifndef NTDDI_VERSION
#define NTDDI_VERSION 0x0A000000
#endif
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX

#include <winsock2.h>
#include <ws2def.h>
#include <ws2ipdef.h>
#include <windows.h>
#include <iphlpapi.h>

#include <QApplication>
#include <QCheckBox>
#include <QCloseEvent>
#include <QMenu>
#include <QSystemTrayIcon>
#include <QComboBox>
#include <QDesktopServices>
#include <QDir>
#include <QMessageBox>
#include <QPushButton>
#include <QUrl>
#include <QSettings>
#include <QFontMetrics>
#include <QLabel>
#include <QIcon>
#include <QPainter>
#include <QPainterPath>
#include <QPaintEvent>
#include <QSignalBlocker>
#include <QTimer>
#include <QUuid>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QWidget>
#include <QVector>
#include <QStringList>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <limits>
#include <utility>
#include <vector>

#include "traffic.h"
#include "version.h"

#pragma comment(lib, "iphlpapi.lib")

// 讀取所有網路介面。是否計入統計由 traffic::counts() 決定。
static bool readInterfaces(std::vector<traffic::InterfaceInfo> &infos, QString &error)
{
    PMIB_IF_TABLE2 table = nullptr;
    const NETIO_STATUS result = GetIfTable2(&table);
    if (result != NO_ERROR || table == nullptr) {
        error = QStringLiteral("無法讀取網卡統計資料（錯誤碼 %1）").arg(result);
        return false;
    }

    infos.clear();
    infos.reserve(table->NumEntries);
    for (ULONG i = 0; i < table->NumEntries; ++i) {
        const MIB_IF_ROW2 &row = table->Table[i];
        traffic::InterfaceInfo info;
        info.luid = row.InterfaceLuid.Value;
        info.guid = QUuid(row.InterfaceGuid).toString(QUuid::WithBraces).toStdString();
        info.description = QString::fromWCharArray(row.Description).toStdString();
        info.alias = QString::fromWCharArray(row.Alias).toStdString();
        info.loopback = row.Type == IF_TYPE_SOFTWARE_LOOPBACK;
        info.filter = row.InterfaceAndOperStatusFlags.FilterInterface;
        info.hardware = row.InterfaceAndOperStatusFlags.HardwareInterface;
        info.up = row.OperStatus == IfOperStatusUp;
        info.inOctets = row.InOctets;
        info.outOctets = row.OutOctets;
        infos.push_back(std::move(info));
    }
    FreeMibTable(table);
    return true;
}

enum class SpeedUnit { Auto, Kbps, Mbps, Gbps, KBs, MBs };

// 下拉選單順序與 SpeedUnit 相同；偏好以字串儲存，調整順序也不會對錯單位。
static const char *const unitKeys[] = {"auto", "Kbps", "Mbps", "Gbps", "KB/s", "MB/s"};
static constexpr int unitCount = 6;

static const QString physicalKey = QStringLiteral("physical");
static const QString allKey = QStringLiteral("all");

static QSettings displaySettings() {
    return QSettings(QSettings::IniFormat, QSettings::UserScope,
                     QCoreApplication::organizationName(), QCoreApplication::applicationName());
}

// 讀取單位偏好。相容舊版存的整數 0～5；無效值回到「自動」。
static int unitIndexFromSetting(const QVariant &value)
{
    const QString text = value.toString();
    bool isNumber = false;
    const int legacy = text.toInt(&isNumber);
    if (isNumber)
        return legacy >= 0 && legacy < unitCount ? legacy : 0;
    for (int i = 0; i < unitCount; ++i)
        if (text == QLatin1String(unitKeys[i]))
            return i;
    return 0;
}

static SpeedUnit automaticUnit(double peak) {
    if (peak >= 1e9) return SpeedUnit::Gbps;
    if (peak >= 1e6) return SpeedUnit::Mbps;
    return SpeedUnit::Kbps;
}

static std::pair<double, const char *> unitScale(SpeedUnit unit) {
    switch (unit) {
    case SpeedUnit::Mbps: return {1e6, "Mbps"};
    case SpeedUnit::Gbps: return {1e9, "Gbps"};
    case SpeedUnit::KBs: return {8e3, "KB/s"};
    case SpeedUnit::MBs: return {8e6, "MB/s"};
    default: return {1e3, "Kbps"};
    }
}

static QString formatSpeed(double bitsPerSecond, SpeedUnit unit) {
    const auto [divisor, name] = unitScale(unit);
    const double value = bitsPerSecond / divisor;
    const int decimals = value > 0 && value < 1 ? 3 : (value >= 1 && value < 10 ? 2 : 1);
    return QStringLiteral("%1 %2").arg(value, 0, 'f', decimals).arg(QLatin1String(name));
}

class SpeedChart final : public QWidget {
    friend struct Verification;
public:
    static constexpr int capacity = 120;

    explicit SpeedChart(QWidget *parent = nullptr) : QWidget(parent) {
        setMinimumHeight(230);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
        setAutoFillBackground(true);
        QPalette p = palette();
        p.setColor(QPalette::Window, Qt::white);
        setPalette(p);
    }

    // 讀取失敗時傳入 NaN：保留時間位置，線會在該處斷開。
    using Time = std::chrono::steady_clock::time_point;
    void append(double download, double upload, Time now = std::chrono::steady_clock::now()) {
        times.append(now);
        downloads.append(download);
        uploads.append(upload);
        while (times.size() > capacity || (!times.isEmpty() && now - times.first() > std::chrono::seconds(60))) {
            times.removeFirst(); downloads.removeFirst(); uploads.removeFirst();
        }
        update();
    }

    void clear() {
        downloads.clear();
        uploads.clear();
        times.clear();
        update();
    }

    double peakSpeed() const {
        double peak = 1.0;
        for (double v : downloads) if (std::isfinite(v)) peak = std::max(peak, v);
        for (double v : uploads) if (std::isfinite(v)) peak = std::max(peak, v);
        return peak;
    }

    void setUnit(SpeedUnit value) {
        if (unit == value) return;
        unit = value;
        update();
    }

    // 縱軸最大值（bit/s）與格數：以顯示單位取整齊數字，最低 1 Kbps。
    traffic::Axis axisInBits() const {
        const double divisor = unitScale(unit).first;
        traffic::Axis axis = traffic::niceAxis(std::max(peakSpeed(), 1e3) / divisor);
        axis.top *= divisor;
        return axis;
    }

protected:
    void paintEvent(QPaintEvent *) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        p.fillRect(rect(), Qt::white);
        const traffic::Axis axis = axisInBits();
        const double top = axis.top;
        p.setFont(QFont(QStringLiteral("Microsoft JhengHei UI"), 8));
        const QFontMetrics metrics(p.font());
        int labelWidth = 76;
        for (int i = 0; i <= axis.intervals; ++i)
            labelWidth = std::max(labelWidth, metrics.horizontalAdvance(formatSpeed(top * (1.0 - double(i) / axis.intervals), unit)) + 16);
        const int left = labelWidth, right = width() - 18, topY = 18, bottom = height() - 32;
        if (right <= left || bottom <= topY) return;

        for (int i = 0; i <= axis.intervals; ++i) {
            const int y = topY + (bottom - topY) * i / axis.intervals;
            p.setPen(QColor("#e8edf4"));
            p.drawLine(left, y, right, y);
            const QString scale = formatSpeed(top * (1.0 - double(i) / axis.intervals), unit);
            p.setPen(QColor("#8793a5"));
            p.drawText(QRect(2, y - 9, left - 9, 18), Qt::AlignRight | Qt::AlignVCenter, scale);
        }
        p.setPen(QColor("#8793a5"));
        p.drawText(QPoint(left, height() - 8), QStringLiteral("60 秒前"));
        p.drawText(QRect(right - 48, height() - 24, 48, 18), Qt::AlignRight | Qt::AlignVCenter, QStringLiteral("現在"));

        auto drawSeries = [&](const QVector<double> &values, const QColor &color) {
            QPainterPath path;
            bool penDown = false;
            for (int i = 0; i < values.size(); ++i) {
                if (!std::isfinite(values[i])) { penDown = false; continue; }
                if (i > 0 && times[i] - times[i-1] > std::chrono::seconds(2)) penDown = false;
                const double age = std::chrono::duration<double>(times.last() - times[i]).count();
                const qreal x = right - (right - left) * age / 60.0;
                const qreal y = bottom - std::min(1.0, values[i] / top) * (bottom - topY);
                if (penDown) path.lineTo(x, y); else path.moveTo(x, y);
                penDown = true;
            }
            p.setPen(QPen(color, 2));
            p.drawPath(path);
        };
        drawSeries(downloads, QColor("#0878e8"));
        drawSeries(uploads, QColor("#13a878"));
    }

private:
    SpeedUnit unit = SpeedUnit::Kbps;
    QVector<Time> times;
    QVector<double> downloads;
    QVector<double> uploads;
};

class SpeedWindow final : public QWidget {
    friend struct Verification;
public:
    SpeedWindow() {
        setWindowTitle(QStringLiteral("即時網路速度"));
        setWindowIcon(QIcon(QStringLiteral(":/app.ico")));
        resize(780, 570);
        setMinimumSize(650, 500);
        setStyleSheet(QStringLiteral("QWidget { background:#f3f6fb; color:#172033; font-family:'Microsoft JhengHei UI'; }"));

        auto *layout = new QVBoxLayout(this);
        layout->setContentsMargins(26, 22, 26, 18);
        layout->setSpacing(10);

        auto *title = new QLabel(QStringLiteral("即時網路速度"));
        title->setStyleSheet(QStringLiteral("font-size:22px;font-weight:700;"));
        layout->addWidget(title);
        auto *subtitle = new QLabel(QStringLiteral("監看這台電腦網路卡目前的傳輸速度"));
        subtitle->setStyleSheet(QStringLiteral("color:#617089;"));
        layout->addWidget(subtitle);

        // QComboBox 只自訂外框時，系統樣式畫的箭頭區塊會多出深色邊框；
        // 依 Qt 文件，下拉按鈕與箭頭也要一併自訂。
        const QString comboStyle = QStringLiteral(
            "QComboBox { background:white; padding:5px 30px 5px 10px; border:1px solid #dce4ef; }"
            "QComboBox:hover, QComboBox:focus, QComboBox:on { border-color:#9fb3cc; }"
            "QComboBox::drop-down { subcontrol-origin:padding; subcontrol-position:center right;"
            " width:26px; border:0; background:transparent; }"
            "QComboBox::down-arrow { image:url(:/arrow-down.png); width:10px; height:6px; }"
            "QComboBox QAbstractItemView { background:white; border:1px solid #dce4ef; outline:0;"
            " selection-background-color:#e6f0fd; selection-color:#172033; }");
        auto settings = displaySettings();

        auto *optionRow = new QHBoxLayout;
        auto *adapterLabel = new QLabel(QStringLiteral("網卡"));
        adapterSelector = new QComboBox;
        adapterSelector->setObjectName(QStringLiteral("adapterSelector"));
        adapterSelector->setAccessibleName(QStringLiteral("網卡"));
        adapterSelector->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
        adapterSelector->setMinimumContentsLength(24);
        adapterSelector->setStyleSheet(comboStyle);
        adapterLabel->setBuddy(adapterSelector);
        savedAdapter = settings.value(QStringLiteral("display/adapter"), physicalKey).toString();

        auto *unitLabel = new QLabel(QStringLiteral("顯示單位"));
        unitSelector = new QComboBox;
        unitSelector->setObjectName(QStringLiteral("unitSelector"));
        unitSelector->addItem(QStringLiteral("自動"), QLatin1String(unitKeys[0]));
        for (int i = 1; i < unitCount; ++i)
            unitSelector->addItem(QLatin1String(unitKeys[i]), QLatin1String(unitKeys[i]));
        unitSelector->setAccessibleName(QStringLiteral("顯示單位"));
        unitLabel->setBuddy(unitSelector);
        unitSelector->setMinimumWidth(110);
        unitSelector->setStyleSheet(comboStyle);
        unitSelector->setCurrentIndex(unitIndexFromSetting(settings.value(QStringLiteral("display/unit"))));

        optionRow->addWidget(adapterLabel);
        optionRow->addWidget(adapterSelector, 1);
        optionRow->addSpacing(16);
        optionRow->addWidget(unitLabel);
        optionRow->addWidget(unitSelector);
        layout->addLayout(optionRow);

        auto *cards = new QHBoxLayout;
        cards->setSpacing(14);
        downloadValue = addCard(cards, QStringLiteral("↓ 下載速度"), QStringLiteral("#0878e8"));
        uploadValue = addCard(cards, QStringLiteral("↑ 上傳速度"), QStringLiteral("#13a878"));
        layout->addLayout(cards);

        status = new QLabel(QStringLiteral("正在讀取網卡資料…"));
        status->setStyleSheet(QStringLiteral("color:#617089;"));
        layout->addWidget(status);

        auto *chartBox = new QWidget;
        chartBox->setStyleSheet(QStringLiteral("background:white;border:1px solid #dce4ef;"));
        auto *chartLayout = new QVBoxLayout(chartBox);
        chartLayout->setContentsMargins(14, 10, 14, 12);
        auto *chartHeader = new QHBoxLayout;
        auto *chartTitle = new QLabel(QStringLiteral("速度趨勢（最近 60 秒）"));
        chartTitle->setStyleSheet(QStringLiteral("font-weight:600;border:0;"));
        auto *legend = new QLabel(QStringLiteral("藍色：下載　 綠色：上傳"));
        legend->setStyleSheet(QStringLiteral("color:#617089;border:0;"));
        chartHeader->addWidget(chartTitle);
        chartHeader->addStretch();
        chartHeader->addWidget(legend);
        chartLayout->addLayout(chartHeader);
        chart = new SpeedChart;
        chartLayout->addWidget(chart);
        layout->addWidget(chartBox, 1);

        auto *bottomRow = new QHBoxLayout;
        note = new QLabel;
        note->setStyleSheet(QStringLiteral("color:#617089;"));
        note->setWordWrap(true);
        bottomRow->addWidget(note, 1);
        aboutButton = new QPushButton(QStringLiteral("關於／授權"));
        aboutButton->setObjectName(QStringLiteral("aboutButton"));
        aboutButton->setCursor(Qt::PointingHandCursor);
        aboutButton->setStyleSheet(QStringLiteral(
            "QPushButton { color:#0878e8; background:transparent; border:0; padding:2px 4px; }"
            "QPushButton:hover { text-decoration:underline; }"));
        bottomRow->addWidget(aboutButton, 0, Qt::AlignRight | Qt::AlignTop);
        layout->addLayout(bottomRow);
        connect(aboutButton, &QPushButton::clicked, this, [this] { showAbout(); });

        connect(unitSelector, &QComboBox::currentIndexChanged, this, [this](int index) {
            if (index < 0) return;
            savePreference(QStringLiteral("display/unit"), unitSelector->itemData(index));
            refreshDisplay();
        });
        connect(adapterSelector, &QComboBox::currentIndexChanged, this, [this](int index) {
            if (index < 0) return;
            savedAdapter = adapterSelector->itemData(index).toString();
            savePreference(QStringLiteral("display/adapter"), savedAdapter);
            // 換網卡後舊的歷史屬於另一張網卡，清空重新累積
            session = {};
            chart->clear();
            currentDown = currentUp = 0;
            hasPrevious = false;
            updateStatus();
            refreshDisplay();
        });

        auto *controls = new QHBoxLayout;
        auto *miniButton = new QPushButton(QStringLiteral("迷你視窗"));
        topmost = new QCheckBox(QStringLiteral("視窗置頂"));
        closeToTray = new QCheckBox(QStringLiteral("關閉時縮至系統匣"));
        topmost->setChecked(settings.value("display/topmost", false).toBool());
        closeToTray->setChecked(settings.value("display/closeToTray", false).toBool());
        controls->addWidget(miniButton);
        controls->addWidget(topmost);
        controls->addWidget(closeToTray);
        layout->addLayout(controls);
        totalsLabel = new QLabel;
        totalsLabel->setWordWrap(true);
        layout->addWidget(totalsLabel);
        auto *reset = new QPushButton(QStringLiteral("重設本次累積流量"));
        layout->addWidget(reset);
        mini = new QWidget(this, Qt::Window);
        mini->setWindowTitle(QStringLiteral("即時網速"));
        mini->setWindowIcon(windowIcon());
        auto *miniLayout = new QVBoxLayout(mini);
        miniValue = new QLabel;
        miniValue->setMinimumWidth(240);
        miniLayout->addWidget(miniValue);
        auto *restore = new QPushButton(QStringLiteral("還原主視窗"));
        miniLayout->addWidget(restore);
        connect(restore, &QPushButton::clicked, this, [this] { showNormal(); raise(); activateWindow(); });
        connect(miniButton, &QPushButton::clicked, this, [this] { mini->show(); });
        connect(reset, &QPushButton::clicked, this, [this] { session = {}; hasPrevious = false; refreshDisplay(); });
        connect(topmost, &QCheckBox::toggled, this, [this](bool on) {
            savePreference("display/topmost", on);
            applyTopmost(on);
        });
        connect(closeToTray, &QCheckBox::toggled, this, [this](bool on) { savePreference("display/closeToTray", on); });
        tray = new QSystemTrayIcon(windowIcon(), this);
        auto *menu = new QMenu(this);
        menu->addAction(QStringLiteral("還原"), this, [this] { showNormal(); raise(); activateWindow(); });
        menu->addAction(QStringLiteral("結束"), qApp, &QApplication::quit);
        tray->setContextMenu(menu);
        connect(tray, &QSystemTrayIcon::activated, this, [this](QSystemTrayIcon::ActivationReason reason) {
            if (reason == QSystemTrayIcon::DoubleClick) { showNormal(); raise(); activateWindow(); }
        });
        tray->show();
        applyTopmost(topmost->isChecked());

        refreshDisplay();
        connect(&timer, &QTimer::timeout, this, [this] { sample(); });
        timer.start(500);
        sample();
    }

private:
    QLabel *addCard(QHBoxLayout *layout, const QString &caption, const QString &accent) {
        auto *card = new QWidget;
        card->setStyleSheet(QStringLiteral("background:white;border:1px solid #dce4ef;"));
        auto *box = new QVBoxLayout(card);
        box->setContentsMargins(16, 12, 16, 12);
        auto *label = new QLabel(caption);
        label->setStyleSheet(QStringLiteral("color:%1;font-size:12pt;font-weight:700;border:0;").arg(accent));
        auto *value = new QLabel(QStringLiteral("讀取中…"));
        value->setStyleSheet(QStringLiteral("font-size:22pt;font-weight:700;border:0;"));
        box->addWidget(label);
        box->addWidget(value);
        layout->addWidget(card, 1);
        return value;
    }

    static QString licensesPath() {
        return QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("licenses"));
    }

    // LGPL 要求明確告知使用了 Qt，並提供授權全文與原始碼取得方式
    void showAbout() {
        const QString qt = QString::fromLatin1(qVersion());
        QMessageBox box(this);
        box.setWindowTitle(QStringLiteral("關於即時網路速度 ") + QStringLiteral(APP_VERSION));
        box.setIconPixmap(windowIcon().pixmap(48, 48));
        box.setTextFormat(Qt::RichText);
        box.setTextInteractionFlags(Qt::TextBrowserInteraction);
        box.setText(QStringLiteral(
            "<p><b>即時網路速度</b><br>作者：Kaido-MSVC<br>Copyright (c) 2026 Kaido-MSVC，以 MIT 授權發佈。</p>"
            "<p>本程式使用 Qt 官方發行、未經修改的 Qt %1，依 GNU Lesser General Public License 第 3 版（LGPL-3.0）授權。"
            "Qt 以獨立 DLL 動態連結，可自行替換為相容版本。<br>"
            "Qt 原始碼：與本程式一起發佈的 qtbase-everywhere-src-%1.tar.xz，或 "
            "<a href=\"https://download.qt.io/official_releases/qt/\">download.qt.io</a></p>"
            "<p>Qt 內含 zlib、PCRE2、libpng、FreeType、HarfBuzz 等第三方程式碼，"
            "著作權聲明與授權全文見程式資料夾中的 licenses\\授權說明.txt 與 licenses\\Qt\\Qt第三方元件.txt。</p>"
            "<p>Portions of this software are copyright © 2026 The FreeType Project "
            "(<a href=\"https://freetype.org\">freetype.org</a>). All rights reserved.</p>").arg(qt));
        QPushButton *openLicenses = nullptr;
        if (QDir(licensesPath()).exists())
            openLicenses = box.addButton(QStringLiteral("開啟授權資料夾"), QMessageBox::ActionRole);
        QPushButton *aboutQt = box.addButton(QStringLiteral("關於 Qt"), QMessageBox::ActionRole);
        box.addButton(QMessageBox::Close);
        box.exec();
        if (openLicenses && box.clickedButton() == openLicenses)
            QDesktopServices::openUrl(QUrl::fromLocalFile(licensesPath()));
        else if (box.clickedButton() == aboutQt)
            QMessageBox::aboutQt(this, QStringLiteral("關於 Qt"));
    }

    void savePreference(const QString &key, const QVariant &value) {
        auto settings = displaySettings();
        settings.setValue(key, value);
        settings.sync();
        // 失敗訊息保留到下一次成功儲存，不會被每 0.5 秒的更新蓋掉
        saveFailed = settings.status() != QSettings::NoError;
    }

    traffic::Selection selection() const {
        const QString key = adapterSelector->currentData().toString();
        if (key == allKey) return {traffic::Scope::All, {}};
        if (key.isEmpty() || key == physicalKey) return {traffic::Scope::Physical, {}};
        return {traffic::Scope::Single, key.toStdString()};
    }

    static QString adapterName(const traffic::InterfaceInfo &i) {
        const QString description = QString::fromStdString(i.description);
        const QString alias = QString::fromStdString(i.alias);
        return alias.isEmpty() || alias == description ? description
                                                       : QStringLiteral("%1（%2）").arg(description, alias);
    }

    // 網卡清單有變（例如插上 USB 網卡）才重建選單，並保留目前選擇。
    void updateAdapterList(const std::vector<traffic::InterfaceInfo> &infos) {
        QStringList keys, names;
        for (const traffic::InterfaceInfo &i : infos) {
            if (!traffic::isSelectableAdapter(i)) continue;
            const QString guid = QString::fromStdString(i.guid);
            if (keys.contains(guid)) continue;
            keys << guid;
            names << adapterName(i);
        }
        if (keys == adapterKeys && names == adapterNames && adapterSelector->count() > 0)
            return;
        adapterKeys = keys;
        adapterNames = names;

        const QString previousKey = adapterSelector->currentData().toString();
        const QSignalBlocker blocker(adapterSelector);
        adapterSelector->clear();
        adapterSelector->addItem(QStringLiteral("所有實體網卡"), physicalKey);
        for (int i = 0; i < keys.size(); ++i)
            adapterSelector->addItem(names[i], keys[i]);
        adapterSelector->addItem(QStringLiteral("全部介面（含 VPN／虛擬網卡，可能重複計算）"), allKey);
        const int index = adapterSelector->findData(savedAdapter);
        // 找不到已存的網卡（例如暫時拔除）時先顯示實體網卡，但不改寫偏好
        adapterSelector->setCurrentIndex(index >= 0 ? index : 0);
        if (!previousKey.isEmpty() && previousKey != adapterSelector->currentData().toString()) {
            session = {};
            chart->clear();
            currentDown = currentUp = 0;
            hasPrevious = false;
        }
    }

    void updateStatus() {
        if (lastInfos.empty()) return;
        const traffic::Selection current = selection();
        QStringList counted;
        for (const traffic::InterfaceInfo &i : lastInfos)
            if (traffic::counts(i, current))
                counted << QString::fromStdString(i.description);
        QString scope;
        switch (current.scope) {
        case traffic::Scope::All:
            scope = QStringLiteral("全部介面 %1 個").arg(counted.size());
            break;
        case traffic::Scope::Single:
            scope = counted.isEmpty() ? QStringLiteral("所選網卡目前未連線") : counted.first();
            break;
        case traffic::Scope::Physical:
            scope = counted.isEmpty() ? QStringLiteral("目前沒有已連線的實體網卡")
                  : counted.size() == 1 ? counted.first()
                  : QStringLiteral("實體網卡 %1 張").arg(counted.size());
            break;
        }
        status->setText(QStringLiteral("監看中・每 0.5 秒更新　|　%1").arg(scope));
    }

    void sample() {
        std::vector<traffic::InterfaceInfo> infos;
        QString error;
        if (!readInterfaces(infos, error)) {
            status->setText(error + QStringLiteral("（將自動重試）"));
            const double gap = std::numeric_limits<double>::quiet_NaN();
            chart->append(gap, gap);
            hasPrevious = false;
            sampleValid = false;
            refreshDisplay();
            return;
        }
        updateAdapterList(infos);

        const auto now = std::chrono::steady_clock::now();
        traffic::Rates rates;
        if (hasPrevious) {
            const double seconds = std::chrono::duration<double>(now - previousTime).count();
            const traffic::Selection current = selection();
            rates = traffic::computeRates(traffic::selectCounters(lastInfos, current),
                                          traffic::selectCounters(infos, current), seconds);
            session.add(rates);
        }
        lastInfos = std::move(infos);
        previousTime = now;
        hasPrevious = true;
        sampleValid = true;
        currentDown = rates.down;
        currentUp = rates.up;
        updateStatus();
        chart->append(rates.down, rates.up);
        refreshDisplay();
    }

    void refreshDisplay() {
        const SpeedUnit selected = static_cast<SpeedUnit>(std::max(0, unitSelector->currentIndex()));
        const SpeedUnit display = selected == SpeedUnit::Auto
            ? automaticUnit(std::max({currentDown, currentUp, chart->peakSpeed()})) : selected;
        downloadValue->setText(sampleValid ? formatSpeed(currentDown, display) : QStringLiteral("—"));
        uploadValue->setText(sampleValid ? formatSpeed(currentUp, display) : QStringLiteral("—"));
        if (miniValue) miniValue->setText(QStringLiteral("↓ %1\n↑ %2").arg(downloadValue->text(), uploadValue->text()));
        if (totalsLabel) totalsLabel->setText(QStringLiteral("本次累積：下載 %1 MB　上傳 %2 MB　合計 %3 MB（切換網卡範圍歸零）")
            .arg(double(session.down)/1e6, 0, 'f', 3).arg(double(session.up)/1e6, 0, 'f', 3)
            .arg((double(session.down)+double(session.up))/1e6, 0, 'f', 3));
        chart->setUnit(display);
        QString text = QStringLiteral("%1；每 0.5 秒更新。1 KB/s = 8 Kbps，1 MB/s = 8 Mbps（十進位）。")
            .arg(selected == SpeedUnit::Auto ? QStringLiteral("自動：依目前速度及 60 秒峰值統一單位")
                                            : QStringLiteral("下載、上傳與圖表使用所選單位"));
        if (saveFailed)
            text += QStringLiteral("（偏好儲存失敗）");
        note->setText(text);
    }

    void applyTopmost(bool on) {
        for (QWidget *w : {static_cast<QWidget *>(this), mini}) {
            const bool visible = w->isVisible();
            w->setWindowFlag(Qt::WindowStaysOnTopHint, on);
            if (visible) w->show();
        }
    }
    void closeEvent(QCloseEvent *event) override {
        if (closeToTray->isChecked() && QSystemTrayIcon::isSystemTrayAvailable()) {
            hide(); event->ignore();
        } else {
            event->accept(); qApp->quit();
        }
    }
    traffic::Session session;
    QWidget *mini = nullptr;
    QLabel *miniValue = nullptr;
    QLabel *totalsLabel = nullptr;
    QCheckBox *topmost = nullptr;
    QCheckBox *closeToTray = nullptr;
    QSystemTrayIcon *tray = nullptr;
    QComboBox *unitSelector = nullptr;
    QComboBox *adapterSelector = nullptr;
    QLabel *note = nullptr;
    QPushButton *aboutButton = nullptr;
    double currentDown = 0, currentUp = 0;
    QLabel *downloadValue = nullptr;
    QLabel *uploadValue = nullptr;
    QLabel *status = nullptr;
    SpeedChart *chart = nullptr;
    QTimer timer;
    std::vector<traffic::InterfaceInfo> lastInfos;
    QStringList adapterKeys, adapterNames;
    QString savedAdapter;
    bool saveFailed = false;
    std::chrono::steady_clock::time_point previousTime;
    bool hasPrevious = false;
    bool sampleValid = true;
};

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("RealtimeNetworkSpeed"));
    QCoreApplication::setApplicationName(QStringLiteral("RealtimeNetworkSpeed"));
    SpeedWindow window;
    window.show();
    return app.exec();
}
