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
#include <QScrollArea>
#include <QScreen>
#include <QLockFile>
#include <QLocalServer>
#include <QLocalSocket>
#include <QCryptographicHash>
#include <QElapsedTimer>
#include <QThread>
#include <QDateTime>
#include <QDialog>
#include <QDateEdit>
#include <QPlainTextEdit>
#include <QFileDialog>
#include <QSaveFile>
#include <QFileInfo>
#include <QNetworkInterface>
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
#include <deque>

#include "traffic.h"
#include "history.h"
#include <QSlider>
#include <QTimeZone>
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
        info.index = row.InterfaceIndex;
        info.operStatus = row.OperStatus;
        info.adminStatus = row.AdminStatus;
        info.receiveLink = row.ReceiveLinkSpeed;
        info.transmitLink = row.TransmitLinkSpeed;
        QStringList mac;
        for (ULONG b = 0; b < std::min<ULONG>(row.PhysicalAddressLength, IF_MAX_PHYS_ADDRESS_LENGTH); ++b)
            mac << QStringLiteral("%1").arg(row.PhysicalAddress[b], 2, 16, QLatin1Char('0'));
        info.mac = mac.join(QLatin1Char(':')).toUpper().toStdString();
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

struct CsvRecord {
    QString timestamp, adapter, key;
    traffic::Rates rates;
    traffic::Session session;
    std::chrono::steady_clock::time_point recordedAt = std::chrono::steady_clock::now();
};

static bool writeCsv(const QString &path, const std::deque<CsvRecord> &records, QString &error, quint64 discarded = 0) {
    QSaveFile file(path); // default: no direct-write fallback, preserve existing file on failure
    if (!file.open(QIODevice::WriteOnly)) { error = file.errorString(); return false; }
    auto write = [&](const QByteArray &bytes) { return file.write(bytes) == bytes.size(); };
    bool ok = write(QByteArray::fromHex("efbbbf")) && write(
        "Timestamp,Adapter,Download_Bps,Upload_Bps,Total_Download,Total_Upload,Adapter_ID,Sample_Seconds,Active_Seconds,Average_Download_Bps,Average_Upload_Bps,Peak_Download_Bps,Peak_Upload_Bps,Retained_From,Retained_To,Retention_Hours,Retention_Max_Records,Discarded_Records\r\n");
    for (const auto &r : records) {
        if (!ok) break;
        const auto n = [](double value) { return QString::number(value, 'g', 17); };
        const QStringList fields = {history::csv(r.timestamp), history::csv(r.adapter), n(r.rates.down / 8), n(r.rates.up / 8),
            QString::number(qulonglong(r.session.down)), QString::number(qulonglong(r.session.up)), history::csv(r.key),
            n(r.rates.seconds), n(r.session.seconds), n(r.session.averageDown() / 8), n(r.session.averageUp() / 8),
            n(r.session.peakDown / 8), n(r.session.peakUp / 8),
            history::csv(records.front().timestamp), history::csv(records.back().timestamp),
            QStringLiteral("24"), QStringLiteral("172800"), QString::number(discarded)};
        ok = write((fields.join(QLatin1Char(',')) + QStringLiteral("\r\n")).toUtf8());
    }
    if (!ok) { error = file.errorString(); file.cancelWriting(); return false; }
    if (!file.commit()) { error = file.errorString(); return false; }
    return true;
}

class SpeedChart final : public QWidget {
    friend struct Verification;
public:
    static constexpr int capacity = 120;

    explicit SpeedChart(QWidget *parent = nullptr) : QWidget(parent) {
        setMinimumHeight(180);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
        setAutoFillBackground(true);
        QPalette p = palette();
        p.setColor(QPalette::Window, Qt::white);
        setPalette(p);
    }

    // 讀取失敗時傳入 NaN：保留時間位置，線會在該處斷開。
    using Time = std::chrono::steady_clock::time_point;
    struct Point { Time time; double down, up; };
    std::deque<Point> longSamples;
    int windowSeconds = 60;
    void setWindowSeconds(int seconds) { windowSeconds = seconds; update(); }
    std::vector<Point> plotted() const {
        std::vector<Point> result;
        if (windowSeconds == 60) {
            for (int i=0;i<times.size();++i) result.push_back({times[i],downloads[i],uploads[i]});
            return result;
        }
        if (longSamples.empty()) return result;
        const int bucketSeconds=windowSeconds==300 ? 2 : 10;
        // Buckets containing a gap stay gaps; no line bridges a failed interval.
        qint64 lastBucket=-1; double down=0,up=0; int n=0; bool gap=false; Time time{};
        auto flush=[&] { if(n) result.push_back({time,gap ? NAN : down/n,gap ? NAN : up/n}); };
        Time previous{};
        for(const auto &p:longSamples) {
            if(longSamples.back().time-p.time>std::chrono::seconds(windowSeconds)) continue;
            const auto b=std::chrono::duration_cast<std::chrono::seconds>(p.time.time_since_epoch()).count()/bucketSeconds;
            if(b!=lastBucket) { flush(); down=up=0; n=0; gap=false; lastBucket=b; }
            gap=gap || !std::isfinite(p.down) || !std::isfinite(p.up)
                || (previous!=Time{} && p.time-previous>std::chrono::seconds(2));
            down+=std::isfinite(p.down)?p.down:0; up+=std::isfinite(p.up)?p.up:0; ++n; time=p.time; previous=p.time;
        }
        flush(); return result;
    }
    void append(double download, double upload, Time now = std::chrono::steady_clock::now()) {
        longSamples.push_back({now,download,upload});
        while(longSamples.size()>3600 || (!longSamples.empty() && now-longSamples.front().time>std::chrono::minutes(30))) longSamples.pop_front();
        times.append(now);
        downloads.append(download);
        uploads.append(upload);
        while (times.size() > capacity || (!times.isEmpty() && now - times.first() > std::chrono::seconds(60))) {
            times.removeFirst(); downloads.removeFirst(); uploads.removeFirst();
        }
        update();
    }

    void clear() {
        longSamples.clear();
        downloads.clear();
        uploads.clear();
        times.clear();
        update();
    }

    double peakSpeed() const {
        double peak = 1.0;
        for (const auto &p : plotted()) { if (std::isfinite(p.down)) peak=std::max(peak,p.down); if (std::isfinite(p.up)) peak=std::max(peak,p.up); }
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
        p.drawText(QPoint(left, height() - 8), QStringLiteral("%1 秒前").arg(windowSeconds));
        p.drawText(QRect(right - 48, height() - 24, 48, 18), Qt::AlignRight | Qt::AlignVCenter, QStringLiteral("現在"));

        const auto points=plotted();
        auto drawSeries = [&](bool upload, const QColor &color) {
            QPainterPath path; bool penDown=false;
            for (size_t i=0;i<points.size();++i) {
                const auto &v=points[i]; const double value=upload?v.up:v.down;
                if(!std::isfinite(value)) { penDown=false; continue; }
                if(i && v.time-points[i-1].time>std::chrono::seconds(windowSeconds==60?2:windowSeconds==300?4:20)) penDown=false;
                const double age=std::chrono::duration<double>(points.back().time-v.time).count();
                const qreal x=right-(right-left)*age/windowSeconds;
                const qreal y=bottom-std::min(1.0,value/top)*(bottom-topY);
                if(penDown) path.lineTo(x,y); else path.moveTo(x,y); penDown=true;
            }
            p.setPen(QPen(color,2)); p.drawPath(path);
        };
        drawSeries(false,QColor("#0878e8")); drawSeries(true,QColor("#13a878"));
    }

private:
    SpeedUnit unit = SpeedUnit::Kbps;
    QVector<Time> times;
    QVector<double> downloads;
    QVector<double> uploads;
};

class HistoryChart final : public QWidget {
public:
    history::Store *store; QString scope; int count=7;
    static QString axisLabel(double bytes) {
        const bool gb=bytes>=1e9;
        return QStringLiteral("%1 %2").arg(bytes/(gb?1e9:1e6),0,'f',gb?2:1).arg(gb?QStringLiteral("GB"):QStringLiteral("MB"));
    }
    explicit HistoryChart(history::Store *s,QWidget *p=nullptr):QWidget(p),store(s) { setMinimumHeight(230); }
protected:
    void paintEvent(QPaintEvent *) override {
        QPainter p(this); p.fillRect(rect(),Qt::white); p.setRenderHint(QPainter::Antialiasing);
        if (count<=0 || width()<=84 || height()<=78) return;
        const QDate today=QDate::currentDate(); double peak=1;
        for(int i=0;i<count;++i) { auto d=store->totals(scope,today.addDays(-i),today.addDays(-i)); peak=std::max({peak,double(d.down),double(d.up)}); }
        const double top=traffic::niceAxis(peak/1e6).top*1e6;
        const QString label=axisLabel(top);
        const int left=std::max(72,p.fontMetrics().horizontalAdvance(label)+8),bottom=height()-48;
        if (width()<=left+12) return;
        const double slot=double(width()-left-12)/count;
        p.setPen(QColor("#617089")); p.drawText(QRect(0,0,left,24),Qt::AlignCenter,label);
        p.drawLine(left,24,left,bottom); p.drawLine(left,bottom,width()-12,bottom);
        for(int i=0;i<count;++i) {
            const QDate date=today.addDays(i-count+1); const auto d=store->totals(scope,date,date);
            const double x=left+i*slot;
            p.fillRect(QRectF(x+slot*.12,bottom-(bottom-30)*d.down/top,slot*.34,(bottom-30)*d.down/top),QColor("#0878e8"));
            p.fillRect(QRectF(x+slot*.5,bottom-(bottom-30)*d.up/top,slot*.34,(bottom-30)*d.up/top),QColor("#13a878"));
            if(count==7 || i%5==0 || i==count-1) { p.setPen(QColor("#617089")); p.drawText(QRectF(x-8,bottom+6,std::max(45.0,slot+16),30),Qt::AlignLeft,date.toString("MM/dd")); }
        }
    }
};

static QString startupCommand(const QString &exe) {
    if (!QFileInfo(exe).isAbsolute() || exe.contains('"') || exe.contains('\n') || exe.contains('\r')) return {};
    return '"'+QDir::toNativeSeparators(QFileInfo(exe).absoluteFilePath())+QStringLiteral("\" --startup");
}
static QSettings startupSettings() { return QSettings(QStringLiteral("HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Run"),QSettings::NativeFormat); }
static bool setStartup(QSettings &registry, bool on, const QString &exe, QString &error) {
    const QString command=startupCommand(exe);
    if(on && (command.isEmpty() || !QFileInfo(exe).isFile() || command.size()>260)) { error=QStringLiteral("執行檔路徑無效或過長，無法設定登入啟動。"); return false; }
    if(on) registry.setValue("RealtimeNetworkSpeed",command); else registry.remove("RealtimeNetworkSpeed");
    registry.sync();
    if(registry.status()!=QSettings::NoError || (on && registry.value("RealtimeNetworkSpeed").toString()!=command) || (!on && registry.contains("RealtimeNetworkSpeed"))) { error=QStringLiteral("無法更新登入啟動設定。"); return false; }
    return true;
}

class SpeedWindow final : public QWidget {
    friend struct Verification;
public:
    SpeedWindow() : historyStore(QDir(QFileInfo(displaySettings().fileName()).absolutePath()).filePath("traffic-history.json")) {
        historyStore.load();
        setWindowTitle(QStringLiteral("即時網路速度"));
        setWindowIcon(QIcon(QStringLiteral(":/app.ico")));
        resize(850, 760);
        setMinimumSize(420, 300);
        setStyleSheet(QStringLiteral("QWidget { background:#f3f6fb; color:#172033; font-family:'Microsoft JhengHei UI'; }"));

        auto *outer = new QVBoxLayout(this);
        outer->setContentsMargins(0, 0, 0, 12);
        auto *scroll = new QScrollArea(this);
        scroll->setObjectName(QStringLiteral("contentScroll"));
        scroll->setWidgetResizable(true);
        scroll->setFrameShape(QFrame::NoFrame);
        auto *content = new QWidget;
        scroll->setWidget(content);
        outer->addWidget(scroll, 1);
        auto *layout = new QVBoxLayout(content);
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
        auto *infoButton = new QPushButton(QStringLiteral("網卡資訊"));
        infoButton->setObjectName(QStringLiteral("adapterInfoButton"));
        optionRow->addWidget(infoButton);
        connect(infoButton, &QPushButton::clicked, this, [this] { showAdapterInfo(); });
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
        auto *chartTitle = new QLabel(QStringLiteral("速度趨勢"));
        auto *interval = new QComboBox;
        interval->setObjectName("chartInterval"); interval->setAccessibleName(QStringLiteral("速度圖表區間"));
        interval->addItem(QStringLiteral("60 秒"),60); interval->addItem(QStringLiteral("5 分鐘"),300); interval->addItem(QStringLiteral("30 分鐘"),1800);
        chartHeader->addWidget(interval);
        connect(interval,&QComboBox::currentIndexChanged,this,[this,interval] { chart->setWindowSeconds(interval->currentData().toInt()); refreshDisplay(); });
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
            const QString requested = adapterSelector->itemData(index).toString();
            // Revert before entering any nested dialog: sampling must keep its old scope.
            { const QSignalBlocker blocker(adapterSelector);
              adapterSelector->setCurrentIndex(adapterSelector->findData(savedAdapter)); }
            if (!confirmDiscard(QStringLiteral("切換網卡範圍"))) return;
            { const QSignalBlocker blocker(adapterSelector);
              adapterSelector->setCurrentIndex(adapterSelector->findData(requested)); }
            savedAdapter = requested;
            savePreference(QStringLiteral("display/adapter"), savedAdapter);
            // 換網卡後舊的歷史屬於另一張網卡，清空重新累積
            clearStatistics();
            updateStatus();
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
        auto *extras=new QHBoxLayout;
        auto *historyButton=new QPushButton(QStringLiteral("歷史流量…")); historyButton->setObjectName("historyButton");
        auto *miniOptions=new QPushButton(QStringLiteral("迷你設定…")); miniOptions->setObjectName("miniOptions");
        auto *clearHistory=new QPushButton(QStringLiteral("清除歷史流量…")); clearHistory->setObjectName("clearHistoryButton");
        clearHistory->setShortcut(QKeySequence(Qt::Key_F10));
        connect(clearHistory,&QPushButton::clicked,this,[this]{ showClearHistory(this); });
        extras->addWidget(historyButton); extras->addWidget(clearHistory); extras->addWidget(miniOptions); extras->addStretch(); layout->addLayout(extras);
        confirmHistoryDate=new QPushButton(QStringLiteral("確認歷史日期…")); confirmHistoryDate->setObjectName("confirmHistoryDate");
        layout->addWidget(confirmHistoryDate);
        connect(confirmHistoryDate,&QPushButton::clicked,this,[this] {
            const QDate date=QDate::currentDate();
            QMessageBox confirm(QMessageBox::Warning,QStringLiteral("確認歷史日期"),
                QStringLiteral("確認系統日期 %1 正確？將先保留完整歷史快照，再重新設定日期基準。超過 365 天及未來日期的紀錄只保留在快照中。即時統計與 CSV 不受影響。").arg(date.toString(Qt::ISODate)),QMessageBox::Yes|QMessageBox::Cancel,this);
            confirm.setDefaultButton(QMessageBox::Cancel); confirm.setEscapeButton(QMessageBox::Cancel);
            if(confirm.exec()!=QMessageBox::Yes) return;
            QString error; if(!historyStore.confirmDate(date,error)) QMessageBox::warning(this,QStringLiteral("日期確認失敗"),error);
            refreshDisplay();
        });
        connect(historyButton,&QPushButton::clicked,this,[this]{ showHistory(); });
        connect(miniOptions,&QPushButton::clicked,this,[this]{ showMiniOptions(); });
        auto *preferences=new QHBoxLayout;
        startup=new QCheckBox(QStringLiteral("登入時啟動（縮至系統匣）")); startup->setObjectName("startup");
        auto registry=startupSettings(); const QString registered=registry.value("RealtimeNetworkSpeed").toString();
        startup->setChecked(!registered.isEmpty());
        startup->setToolTip(registered.isEmpty()?QStringLiteral("預設關閉"):registered);
        preferences->addWidget(startup);
        if(!registered.isEmpty() && registered!=startupCommand(QCoreApplication::applicationFilePath())) {
            auto *moved=new QLabel(QStringLiteral("登入啟動指向其他位置；取消後重新勾選可更新為目前程式。"));
            moved->setWordWrap(true); layout->addWidget(moved);
        }
        notifications=new QCheckBox(QStringLiteral("網卡狀態通知")); notifications->setObjectName("notifications");
        notifications->setChecked(settings.value("notifications/enabled",false).toBool()); preferences->addWidget(notifications); preferences->addStretch(); layout->addLayout(preferences);
        connect(notifications,&QCheckBox::toggled,this,[this](bool on){ notificationState.reset(); savePreference("notifications/enabled",on); });
        connect(startup,&QCheckBox::toggled,this,[this](bool on){ auto registry=startupSettings(); QString error;
            if(!setStartup(registry,on,QCoreApplication::applicationFilePath(),error)) { QSignalBlocker block(startup); startup->setChecked(!on); QMessageBox::warning(this,QStringLiteral("登入啟動"),error); }
            else startup->setToolTip(on?startupCommand(QCoreApplication::applicationFilePath()):QStringLiteral("已關閉"));
        });
        historySummary=new QLabel; historySummary->setWordWrap(true); layout->addWidget(historySummary);
        totalsLabel = new QLabel;
        totalsLabel->setWordWrap(true);
        layout->addWidget(totalsLabel);
        statsLabel = new QLabel;
        statsLabel->setWordWrap(true);
        layout->addWidget(statsLabel);
        retentionLabel = new QLabel;
        retentionLabel->setWordWrap(true);
        layout->addWidget(retentionLabel);
        auto *actions = new QHBoxLayout;
        actions->setContentsMargins(16, 0, 16, 0);
        pauseButton = new QPushButton(QStringLiteral("暫停監控"));
        pauseButton->setObjectName(QStringLiteral("pauseButton"));
        auto *reset = new QPushButton(QStringLiteral("清空本次統計"));
        reset->setObjectName(QStringLiteral("resetButton"));
        exportButton = new QPushButton(QStringLiteral("匯出 CSV…"));
        exportButton->setObjectName(QStringLiteral("exportButton"));
        actions->addWidget(pauseButton);
        actions->addWidget(reset);
        actions->addStretch();
        actions->addWidget(exportButton);
        outer->addLayout(actions);
        connect(pauseButton, &QPushButton::clicked, this, [this] { setPaused(!paused); });
        connect(exportButton, &QPushButton::clicked, this, [this] { exportCsv(); });
        // No Qt parent/native owner: main-window minimize/hide cannot hide mini.
        // Tool keeps it out of the taskbar/Show Desktop; topmost is independent
        // of the main window's saved preference. This is not a Qt::Popup.
        mini = new QWidget(nullptr, Qt::Tool | Qt::WindowStaysOnTopHint);
        mini->setAttribute(Qt::WA_QuitOnClose, false);
        mini->setStyleSheet(styleSheet());
        connect(qApp, &QCoreApplication::aboutToQuit, mini, &QWidget::hide);
        mini->setWindowTitle(QStringLiteral("即時網速"));
        mini->setWindowIcon(windowIcon());
        auto *miniLayout = new QVBoxLayout(mini);
        miniValue = new QLabel;
        miniValue->setMinimumWidth(240);
        miniLayout->addWidget(miniValue);
        auto *restore = new QPushButton(QStringLiteral("還原主視窗"));
        miniLayout->addWidget(restore);
        connect(restore, &QPushButton::clicked, this, [this] { activate(); });
        connect(miniButton, &QPushButton::clicked, this, [this] { ensureVisible(mini); mini->show(); });
        connect(reset, &QPushButton::clicked, this, [this] { if (confirmDiscard(QStringLiteral("清空本次統計"))) clearStatistics(); });
        connect(topmost, &QCheckBox::toggled, this, [this](bool on) {
            savePreference("display/topmost", on);
            applyTopmost(on);
        });
        connect(closeToTray, &QCheckBox::toggled, this, [this](bool on) { savePreference("display/closeToTray", on); });
        tray = new QSystemTrayIcon(windowIcon(), this);
        auto *menu = new QMenu(this);
        menu->addAction(QStringLiteral("還原"), this, [this] { activate(); });
        menu->addAction(QStringLiteral("結束"), qApp, &QApplication::quit);
        tray->setContextMenu(menu);
        connect(tray, &QSystemTrayIcon::activated, this, [this](QSystemTrayIcon::ActivationReason reason) {
            if (reason == QSystemTrayIcon::DoubleClick || reason == QSystemTrayIcon::Trigger) { activate(); }
        });
        tray->show();
        applyTopmost(topmost->isChecked());

        applyMiniSettings();
        mini->adjustSize();
        restoreWindows();
        connect(qApp, &QCoreApplication::aboutToQuit, this, [this] { saveWindows(); saveHistory(); });
        connect(&retentionTimer, &QTimer::timeout, this, [this] {
            pruneRecords(std::chrono::steady_clock::now()); historyStore.prune(QDate::currentDate()); saveHistory(); refreshDisplay();
        });
        retentionTimer.start(60000);
        refreshDisplay();
        connect(&timer, &QTimer::timeout, this, [this] { sample(); });
        timer.start(500);
        sample();
    }

    ~SpeedWindow() override { saveWindows(); saveHistory(); delete mini; }

    void activate() {
        ensureVisible(this);
        showNormal(); raise(); activateWindow();
        if (auto *dialog = QApplication::activeModalWidget()) { dialog->raise(); dialog->activateWindow(); }
    }

private:
    // Scope guards keep dialogs/export stable without copying the entire record buffer.
    struct SamplingGuard {
        SpeedWindow &w;
        bool running;
        explicit SamplingGuard(SpeedWindow &window) : w(window), running(w.timer.isActive()) { w.timer.stop(); }
        ~SamplingGuard() { if (running && !w.paused) { w.hasPrevious = false; w.timer.start(500); } }
    };

    static void ensureVisible(QWidget *w) {
        QScreen *screen = QGuiApplication::screenAt(w->frameGeometry().center());
        if (!screen) screen = QGuiApplication::primaryScreen();
        if (!screen || w->isMaximized()) return;
        const QRect area = screen->availableGeometry();
        w->resize(w->size().boundedTo(QSize(std::max(1, area.width() - 24), std::max(1, area.height() - 48))));
        const QRect frame = w->frameGeometry();
        w->move(w->pos() + QPoint(std::clamp(frame.x(), area.left(), std::max(area.left(), area.right() - frame.width() + 1)) - frame.x(),
                                 std::clamp(frame.y(), area.top(), std::max(area.top(), area.bottom() - frame.height() + 1)) - frame.y()));
    }
    void restoreWindows() {
        auto settings = displaySettings();
        restoreGeometry(settings.value("window/mainGeometry").toByteArray());
        mini->restoreGeometry(settings.value("window/miniGeometry").toByteArray());
        ensureVisible(this); ensureVisible(mini);
    }
    void saveWindows() {
        if (!mini) return;
        savePreference("window/mainGeometry", saveGeometry());
        savePreference("window/miniGeometry", mini->saveGeometry());
    }
    static constexpr size_t recordLimit = 172800;
    void pruneRecords(std::chrono::steady_clock::time_point now) {
        while (!records.empty() && (records.size() > recordLimit || now - records.front().recordedAt >= std::chrono::hours(24))) {
            records.pop_front(); ++discardedRecords;
        }
    }
    bool confirmDiscard(const QString &action) {
        if (!unexportedRecords) return true;
        SamplingGuard guard(*this);
        QMessageBox box(QMessageBox::Warning, action,
            QStringLiteral("這會清除本次累積統計及尚未匯出的逐筆紀錄。\n逐筆只保留最近 24 小時（最多 172800 筆），較舊資料無法復原。"),
            QMessageBox::NoButton, this);
        auto *save = box.addButton(QStringLiteral("先匯出再繼續"), QMessageBox::AcceptRole);
        auto *discard = box.addButton(QStringLiteral("捨棄並繼續"), QMessageBox::DestructiveRole);
        auto *cancel = box.addButton(QStringLiteral("取消"), QMessageBox::RejectRole);
        box.setDefaultButton(cancel); box.setEscapeButton(cancel);
        box.exec();
        if (box.clickedButton() == save) return exportCsv();
        return box.clickedButton() == discard;
    }
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

        const QSignalBlocker blocker(adapterSelector);
        adapterSelector->clear();
        adapterSelector->addItem(QStringLiteral("所有實體網卡"), physicalKey);
        for (int i = 0; i < keys.size(); ++i)
            adapterSelector->addItem(names[i].simplified(), keys[i]);
        if (savedAdapter != physicalKey && savedAdapter != allKey && !savedAdapter.isEmpty()
            && !keys.contains(savedAdapter))
            adapterSelector->addItem(QStringLiteral("所選網卡目前不存在（%1）").arg(savedAdapter), savedAdapter);
        adapterSelector->addItem(QStringLiteral("全部介面（含 VPN／虛擬網卡，可能重複計算）"), allKey);
        const int index = adapterSelector->findData(savedAdapter);
        adapterSelector->setCurrentIndex(index >= 0 ? index : 0);
    }

    void updateStatus() {

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
        status->setText(QStringLiteral("%1　|　%2").arg(paused ? QStringLiteral("已暫停・數值凍結")
            : QStringLiteral("監看中・每 0.5 秒更新"), scope));
    }

    void sample() {
        if (paused) return;
        std::vector<traffic::InterfaceInfo> infos;
        QString error;
        const bool ok = readInterfaces(infos, error);
        processSample(std::move(infos), ok, std::chrono::steady_clock::now(), error);
    }

    void processSample(std::vector<traffic::InterfaceInfo> infos, bool ok, SpeedChart::Time now, const QString &error = {}, QDateTime wall = QDateTime::currentDateTime()) {
        if (paused) return;
        pruneRecords(now);
        if (!ok) {
            observeState(2);
            status->setText(error + QStringLiteral("（將自動重試）"));
            const double gap = std::numeric_limits<double>::quiet_NaN();
            chart->append(gap, gap, now);
            hasPrevious = false;
            sampleValid = false;
            refreshDisplay();
            return;
        }
        updateAdapterList(infos);

        const auto counters=traffic::selectCounters(infos,selection());
        observeState(counters.empty()?0:1);
        traffic::Rates rates;
        if (hasPrevious) {
            const double seconds = std::chrono::duration<double>(now - previousTime).count();
            const traffic::Selection current = selection();
            const double wallSeconds=previousWall.msecsTo(wall)/1000.0;
            if (std::abs(wallSeconds-seconds)>0.25 || wall.offsetFromUtc()!=previousWall.offsetFromUtc()) hasPrevious=false;
            rates = hasPrevious ? traffic::computeRates(traffic::selectCounters(lastInfos, current),
                                          traffic::selectCounters(infos, current), seconds) : traffic::Rates{};
            session.add(rates);
            historyStore.add(adapterSelector->currentData().toString(),adapterSelector->currentText(),rates,previousWall,wall,now);
        }
        lastInfos = std::move(infos);
        previousTime = now;
        previousWall = wall;
        hasPrevious = true;
        sampleValid = rates.seconds > 0;
        if (sampleValid) {
            QString name = adapterSelector->currentText();
            const auto scope = selection();
            if (scope.scope == traffic::Scope::Single)
                for (const auto &i : lastInfos) if (i.guid == scope.guid) { name = adapterName(i); break; }
            const auto timestamp = wall;
            records.push_back({timestamp.toOffsetFromUtc(timestamp.offsetFromUtc()).toString(Qt::ISODateWithMs),
                name, adapterSelector->currentData().toString(), rates, session, now});
            unexportedRecords = true;
            pruneRecords(now);
        }
        currentDown = rates.down;
        currentUp = rates.up;
        updateStatus();
        const double gap = std::numeric_limits<double>::quiet_NaN();
        chart->append(sampleValid ? rates.down : gap, sampleValid ? rates.up : gap, now);
        refreshDisplay();
    }

    void refreshDisplay() {
        const SpeedUnit selected = static_cast<SpeedUnit>(std::max(0, unitSelector->currentIndex()));
        const SpeedUnit display = selected == SpeedUnit::Auto
            ? automaticUnit(std::max({currentDown, currentUp, chart->peakSpeed()})) : selected;
        downloadValue->setText(sampleValid ? formatSpeed(currentDown, display) : QStringLiteral("—"));
        uploadValue->setText(sampleValid ? formatSpeed(currentUp, display) : QStringLiteral("—"));
        if (miniValue) {
            QString text=QStringLiteral("↓ %1\n↑ %2").arg(downloadValue->text(),uploadValue->text());
            if(miniShowAdapter) text+="\n"+adapterSelector->currentText();
            if(miniShowTotals) text+=QStringLiteral("\n本次 ↓ %1 MB / ↑ %2 MB").arg(double(session.down)/1e6,0,'f',2).arg(double(session.up)/1e6,0,'f',2);
            miniValue->setText(text);
        }
        refreshHistorySummary();
        if (totalsLabel) totalsLabel->setText(QStringLiteral("本次累積：下載 %1 MB　上傳 %2 MB　合計 %3 MB（切換網卡範圍歸零）")
            .arg(double(session.down)/1e6, 0, 'f', 3).arg(double(session.up)/1e6, 0, 'f', 3)
            .arg((double(session.down)+double(session.up))/1e6, 0, 'f', 3));
        if (statsLabel) statsLabel->setText(QStringLiteral("平均：↓ %1　↑ %2\n峰值：↓ %3　↑ %4　｜有效取樣 %5 秒")
            .arg(formatSpeed(session.averageDown(), display), formatSpeed(session.averageUp(), display),
                 formatSpeed(session.peakDown, display), formatSpeed(session.peakUp, display))
            .arg(session.seconds, 0, 'f', 1));
        const QString tooltip = QStringLiteral("%1\n%2\n%3")
            .arg(status->text(), totalsLabel->text(), statsLabel->text());
        downloadValue->setToolTip(tooltip);
        uploadValue->setToolTip(tooltip);
        miniValue->setToolTip(tooltip);
        tray->setToolTip(QStringLiteral("%1\n↓ %2　↑ %3").arg(paused ? QStringLiteral("已暫停") : QStringLiteral("監看中"), downloadValue->text(), uploadValue->text()));
        mini->setWindowTitle(paused ? QStringLiteral("即時網速（已暫停）") : QStringLiteral("即時網速"));
        exportButton->setEnabled(!records.empty());
        retentionLabel->setText(QStringLiteral("CSV：最近 24 小時，最多 172800 筆；目前 %1 筆，已淘汰 %2 筆。累積統計不受影響。")
            .arg(records.size()).arg(discardedRecords));
        chart->setUnit(display);
        QString text = QStringLiteral("%1；每 0.5 秒更新。1 KB/s = 8 Kbps，1 MB/s = 8 Mbps（十進位）。")
            .arg(selected == SpeedUnit::Auto ? QStringLiteral("自動：依目前速度及所選區間峰值統一單位")
                                            : QStringLiteral("下載、上傳與圖表使用所選單位"));
        if (saveFailed)
            text += QStringLiteral("（偏好儲存失敗）");
        notifyHistoryBlocked();
        if (confirmHistoryDate) confirmHistoryDate->setVisible(!historyStore.clockWarning.isEmpty());
        if (!historyStore.clockWarning.isEmpty()) text+="\n"+historyStore.clockWarning;
        if (!historyStore.intervalWarning.isEmpty()) text+="\n"+historyStore.intervalWarning;
        if (!historyStore.warning.isEmpty()) text+="\n"+historyStore.warning;
        if (!historyStore.dateNotice.isEmpty()) text+="\n"+historyStore.dateNotice;
        if (!historySaveError.isEmpty()) text+=QStringLiteral("\n歷史保存失敗：%1（下次自動重試）").arg(historySaveError);
        note->setText(text);
    }

    void clearStatistics() {
        saveHistory(); notificationState.reset();
        session = {};
        records.clear();
        discardedRecords = 0;
        unexportedRecords = false;
        chart->clear();
        currentDown = currentUp = 0;
        hasPrevious = false;
        sampleValid = false;
        updateStatus();
        refreshDisplay();
    }

    void setPaused(bool value) {
        if (paused == value) return;
        paused = value;
        saveHistory(); notificationState.reset();
        hasPrevious = false;
        pauseButton->setText(paused ? QStringLiteral("恢復監控") : QStringLiteral("暫停監控"));
        if (paused) timer.stop();
        else { sampleValid = false; timer.start(500); sample(); }
        updateStatus();
        refreshDisplay();
    }

    QString adapterDetails(const std::vector<traffic::InterfaceInfo> &infos, const QList<QNetworkInterface> &networks) const {
        QStringList details;
        const auto current = selection();
        for (const auto &i : infos) {
            if (!traffic::isSelectableAdapter(i)) continue;
            if (current.scope == traffic::Scope::Single && i.guid != current.guid) continue;
            if (current.scope == traffic::Scope::Physical && !i.hardware) continue;
            QStringList ips;
            for (const auto &network : networks) {
                if (network.index() != int(i.index)) continue;
                for (const auto &entry : network.addressEntries()) ips << entry.ip().toString();
            }
            const auto link = [](std::uint64_t speed) {
                return speed == 0 || speed == std::numeric_limits<std::uint64_t>::max()
                    ? QStringLiteral("未知") : formatSpeed(double(speed), automaticUnit(double(speed)));
            };
            const QStringList states = {QStringLiteral("已連線"), QStringLiteral("未連線"), QStringLiteral("測試中"),
                QStringLiteral("未知"), QStringLiteral("等待連線"), QStringLiteral("裝置不存在"), QStringLiteral("下層連線中斷")};
            const QString state = i.operStatus >= 1 && i.operStatus <= 7 ? states[int(i.operStatus) - 1] : QStringLiteral("未知");
            const QString enabled = i.adminStatus == 1 ? QStringLiteral("已啟用")
                : i.adminStatus == 2 ? QStringLiteral("已停用") : QStringLiteral("其他狀態");
            details << QStringLiteral("%1\nGUID：%2\n類型：%3　狀態：%4（%5）\nIP：%6\nMAC：%7\n連線速度：接收 %8／傳送 %9")
                .arg(adapterName(i), QString::fromStdString(i.guid),
                    i.hardware ? QStringLiteral("實體") : QStringLiteral("VPN／虛擬"),
                    state).arg(enabled)
                .arg(ips.isEmpty() ? QStringLiteral("無可用 IPv4／IPv6 位址") : ips.join(QStringLiteral("、")),
                     i.mac.empty() ? QStringLiteral("無") : QString::fromStdString(i.mac), link(i.receiveLink), link(i.transmitLink));
        }
        return details.isEmpty() ? QStringLiteral("所選範圍目前沒有可用網卡；選擇會保留，等待網卡重新出現。")
            : details.join(QStringLiteral("\n\n"));
    }

    void showAdapterInfo() {
        // Explicit refresh also works while monitoring is paused; no traffic is accumulated.
        std::vector<traffic::InterfaceInfo> infos;
        QString error;
        if (!readInterfaces(infos, error)) { QMessageBox::warning(this, QStringLiteral("網卡資訊"), error); return; }
        const QString details = adapterDetails(infos, QNetworkInterface::allInterfaces());
        QDialog box(this);
        box.setWindowTitle(QStringLiteral("網卡資訊"));
        box.resize(660, 460);
        auto *layout = new QVBoxLayout(&box);
        auto *text = new QPlainTextEdit(&box);
        text->setReadOnly(true);
        text->setPlainText(QStringLiteral("目前監看範圍：%1\n\n%2")
            .arg(adapterSelector->currentText(), details));
        layout->addWidget(text);
        auto *close = new QPushButton(QStringLiteral("關閉"), &box);
        layout->addWidget(close, 0, Qt::AlignRight);
        connect(close, &QPushButton::clicked, &box, &QDialog::accept);
        box.exec();
    }

    bool exportCsv() {
        SamplingGuard guard(*this);
        pruneRecords(std::chrono::steady_clock::now());
        if (records.empty()) {
            QMessageBox::information(this, QStringLiteral("匯出 CSV"), QStringLiteral("目前沒有保留範圍內的紀錄可匯出。"));
            return false;
        }
        const QString path = QFileDialog::getSaveFileName(this, QStringLiteral("匯出最近 24 小時保留紀錄"),
            QStringLiteral("網速統計-%1.csv").arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss"))),
            QStringLiteral("CSV 檔案 (*.csv)"));
        if (path.isEmpty()) return false;
        pruneRecords(std::chrono::steady_clock::now());
        if (records.empty()) {
            QMessageBox::information(this, QStringLiteral("匯出 CSV"), QStringLiteral("保留紀錄已超過 24 小時，沒有可匯出的資料。"));
            return false;
        }
        // No nested event loop during writing; the deque is stable on the UI thread.
        QString error;
        if (!writeCsv(path, records, error, discardedRecords)) {
            QMessageBox::warning(this, QStringLiteral("匯出失敗"), QStringLiteral("無法儲存 CSV：%1").arg(error));
            return false;
        }
        unexportedRecords = false;
        QMessageBox::information(this, QStringLiteral("匯出完成"),
            QStringLiteral("已儲存 %1 筆保留紀錄（已淘汰 %2 筆）\n範圍：%3 至 %4\n累積欄位包含本次全部有效取樣。\n%5")
                .arg(records.size()).arg(discardedRecords).arg(records.front().timestamp, records.back().timestamp, path));
        return true;
    }

    void applyTopmost(bool on) {
        const bool visible = isVisible();
        setWindowFlag(Qt::WindowStaysOnTopHint, on);
        if (visible) show();
    }
    void changeEvent(QEvent *event) override {
        QWidget::changeEvent(event);
        if (event->type() == QEvent::WindowStateChange && isMinimized()
            && QSystemTrayIcon::isSystemTrayAvailable()) {
            // showMinimized() can call show() after sending WindowStateChange.
            // Hide after that transition, but never undo a restore in between.
            QTimer::singleShot(0, this, [this] {
                if (isMinimized()) hide();
            });
        }
    }
    void closeEvent(QCloseEvent *event) override {
        saveWindows();
        if (closeToTray->isChecked() && QSystemTrayIcon::isSystemTrayAvailable()) {
            hide(); event->ignore();
        } else {
            event->accept(); qApp->quit();
        }
    }
    QPushButton *confirmHistoryDate=nullptr;
    history::Store historyStore;
    QString historySaveError;
    bool historyBlockedNotified=false;
    bool notifyHistoryBlocked() {
        const bool blocked=!historyStore.clockWarning.isEmpty() || !historyStore.writable || !historySaveError.isEmpty();
        if (!blocked) { historyBlockedNotified=false; return false; }
        if (historyBlockedNotified || !tray || !QSystemTrayIcon::isSystemTrayAvailable() || !QSystemTrayIcon::supportsMessages()) return false;
        historyBlockedNotified=true;
        const QString reason=!historyStore.clockWarning.isEmpty()?historyStore.clockWarning:
            !historyStore.writable?historyStore.warning:QStringLiteral("歷史保存失敗：%1").arg(historySaveError);
        tray->showMessage(QStringLiteral("歷史流量記錄受阻"),reason,QSystemTrayIcon::Warning,6000);
        return true;
    }
    QLabel *historySummary=nullptr;
    QCheckBox *startup=nullptr,*notifications=nullptr;
    history::Notifications notificationState;
    QDateTime previousWall;
    void saveHistory() { QString error; if(!historyStore.save(error)) historySaveError=error; else historySaveError.clear(); }
    void refreshHistorySummary() {
        if(!historySummary) return;
        const QDate now=QDate::currentDate(); const auto scope=adapterSelector->currentData().toString();
        const auto today=historyStore.totals(scope,now,now), month=historyStore.totals(scope,QDate(now.year(),now.month(),1),now);
        historySummary->setText(QStringLiteral("有效監控流量｜%1\n今日 ↓ %2 MB　↑ %3 MB　合計 %4 MB\n本月 ↓ %5 MB　↑ %6 MB　合計 %7 MB")
            .arg(adapterSelector->currentText()).arg(double(today.down)/1e6,0,'f',3).arg(double(today.up)/1e6,0,'f',3).arg((double(today.down)+today.up)/1e6,0,'f',3)
            .arg(double(month.down)/1e6,0,'f',3).arg(double(month.up)/1e6,0,'f',3).arg((double(month.down)+month.up)/1e6,0,'f',3));
    }
    void observeState(int state) {
        if(!notifications || !notifications->isChecked()) return;
        const int event=notificationState.observe(state);
        if(event<0 || !tray || !QSystemTrayIcon::isSystemTrayAvailable() || !QSystemTrayIcon::supportsMessages()) return;
        const QString text=event==0?QStringLiteral("監控範圍目前沒有已連線網卡"):event==1?QStringLiteral("監控範圍已恢復連線／讀取"):QStringLiteral("網卡資料讀取失敗，將自動重試");
        tray->showMessage(QStringLiteral("即時網路速度"),text,QSystemTrayIcon::Information,4000);
    }
    bool miniShowAdapter=false, miniShowTotals=false;
    void applyMiniSettings() {
        auto settings=displaySettings();
        miniShowAdapter=settings.value("mini/showAdapter",false).toBool();
        miniShowTotals=settings.value("mini/showTotals",false).toBool();
        mini->setWindowOpacity(std::clamp(settings.value("mini/opacity",100).toInt(),50,100)/100.0);
        const int size=std::clamp(settings.value("mini/font",1).toInt(),0,2);
        miniValue->setFont(QFont(QStringLiteral("Microsoft JhengHei UI"),size==0?9:size==1?11:15));
        miniValue->setWordWrap(true); miniValue->setMaximumWidth(420);
        mini->adjustSize(); ensureVisible(mini);
    }
    void showMiniOptions() {
        QDialog dialog(this); dialog.setWindowTitle(QStringLiteral("迷你視窗設定")); auto *layout=new QVBoxLayout(&dialog);
        auto settings=displaySettings(); auto *label=new QLabel; layout->addWidget(label);
        auto *opacity=new QSlider(Qt::Horizontal); opacity->setRange(50,100); opacity->setSingleStep(5); opacity->setAccessibleName(QStringLiteral("透明度")); opacity->setValue(std::clamp(settings.value("mini/opacity",100).toInt(),50,100)); layout->addWidget(opacity);
        auto updateLabel=[label](int v){label->setText(QStringLiteral("透明度 %1%（50–100%）").arg(v));}; updateLabel(opacity->value());
        connect(opacity,&QSlider::valueChanged,&dialog,[this,updateLabel](int v){ updateLabel(v); savePreference("mini/opacity",v); applyMiniSettings(); });
        auto *font=new QComboBox; font->addItems({QStringLiteral("小"),QStringLiteral("中"),QStringLiteral("大")}); font->setAccessibleName(QStringLiteral("字體大小")); font->setCurrentIndex(std::clamp(settings.value("mini/font",1).toInt(),0,2)); layout->addWidget(new QLabel(QStringLiteral("字體大小"))); layout->addWidget(font);
        connect(font,&QComboBox::currentIndexChanged,&dialog,[this](int v){savePreference("mini/font",v); applyMiniSettings();});
        for(const auto &entry: {std::pair<const char*,QString>{"mini/showAdapter",QStringLiteral("顯示目前網卡")},{"mini/showTotals",QStringLiteral("顯示本次累積流量")}}) {
            auto *box=new QCheckBox(entry.second); box->setChecked(settings.value(entry.first,false).toBool()); layout->addWidget(box);
            const QString key=QLatin1String(entry.first); connect(box,&QCheckBox::toggled,&dialog,[this,key](bool on){savePreference(key,on); applyMiniSettings(); refreshDisplay(); mini->adjustSize();ensureVisible(mini);});
        }
        auto *close=new QPushButton(QStringLiteral("完成")); layout->addWidget(close); connect(close,&QPushButton::clicked,&dialog,&QDialog::accept); dialog.exec();
    }
    void showClearHistory(QWidget *parent) {
        QDialog dialog(parent); dialog.setObjectName("clearHistoryDialog");
        dialog.setWindowTitle(QStringLiteral("清除歷史流量")); dialog.resize(600,440);
        auto *layout=new QVBoxLayout(&dialog);
        auto *explanation=new QLabel(QStringLiteral("清除每日歷史及其復原備份中的對應紀錄，不可復原。可先匯出 CSV。\n「清空本次統計」另行操作；此處不會清空本次統計或中斷即時監控。日期修復快照另存，不隨此操作移除。")); explanation->setWordWrap(true); layout->addWidget(explanation);
        auto *scope=new QComboBox; scope->setObjectName("clearHistoryScope"); scope->setAccessibleName(QStringLiteral("要清除的監控範圍"));
        QMap<QString,QString> scopes;
        for (const auto &d:historyStore.days) scopes[d.scope]=d.name;
        scopes[adapterSelector->currentData().toString()]=adapterSelector->currentText();
        for(auto it=scopes.begin();it!=scopes.end();++it) scope->addItem(it.value()+" ["+it.key()+"]",it.key());
        scope->setCurrentIndex(scope->findData(adapterSelector->currentData()));
        scope->addItem(QStringLiteral("所有監控範圍／網卡"),QString());
        layout->addWidget(new QLabel(QStringLiteral("監控範圍（各範圍紀錄互相獨立）"))); layout->addWidget(scope);
        auto *range=new QComboBox; range->setObjectName("clearHistoryRange"); range->setAccessibleName(QStringLiteral("要清除的日期範圍"));
        range->addItems({QStringLiteral("最近 7 天（含今天）"),QStringLiteral("最近 30 天（含今天）"),QStringLiteral("自訂起訖日期（含首尾）"),QStringLiteral("全部保留日期")}); layout->addWidget(range);
        const QDate today=QDate::currentDate();
        auto *dates=new QHBoxLayout;
        auto *first=new QDateEdit(today.addDays(-6)); first->setObjectName("clearHistoryFirst"); first->setAccessibleName(QStringLiteral("起始日期"));
        auto *last=new QDateEdit(today); last->setObjectName("clearHistoryLast"); last->setAccessibleName(QStringLiteral("結束日期"));
        for(auto *date:{first,last}) { date->setCalendarPopup(true); date->setDisplayFormat("yyyy-MM-dd"); }
        dates->addWidget(new QLabel(QStringLiteral("從"))); dates->addWidget(first); dates->addWidget(new QLabel(QStringLiteral("至"))); dates->addWidget(last); layout->addLayout(dates);
        auto filter=[=] { return history::Filter{scope->currentData().toString(),range->currentIndex()==3?QDate():first->date(),range->currentIndex()==3?QDate():last->date()}; };
        auto description=[=](int matches) { const auto f=filter(); return QStringLiteral("監控範圍：%1\n日期：%2\n符合 %3 筆每日紀錄（監控期間筆數可能增加）")
            .arg(scope->currentText(),f.first.isValid()?f.first.toString(Qt::ISODate)+QStringLiteral(" 至 ")+f.last.toString(Qt::ISODate)+QStringLiteral("（含首尾）"):QStringLiteral("全部保留日期")).arg(matches); };
        auto *preview=new QLabel; preview->setObjectName("clearHistoryPreview"); preview->setWordWrap(true); layout->addWidget(preview); layout->addStretch();
        auto *buttons=new QHBoxLayout;
        auto *exportSelection=new QPushButton(QStringLiteral("先匯出 CSV…")); exportSelection->setObjectName("clearHistoryExport");
        auto *erase=new QPushButton(QStringLiteral("清除所選歷史…")); erase->setObjectName("clearHistoryApply"); erase->setAutoDefault(false);
        auto *cancel=new QPushButton(QStringLiteral("取消")); cancel->setObjectName("clearHistoryCancel"); cancel->setDefault(true);
        buttons->addWidget(exportSelection); buttons->addStretch(); buttons->addWidget(erase); buttons->addWidget(cancel); layout->addLayout(buttons);
        quint64 seenRevision=std::numeric_limits<quint64>::max(); history::Filter seenFilter; bool havePreview=false;
        auto update=[=,&seenRevision,&seenFilter,&havePreview] {
            const auto f=filter();
            if(havePreview && seenRevision==historyStore.structureRevision && f.scope==seenFilter.scope && f.first==seenFilter.first && f.last==seenFilter.last) return;
            seenRevision=historyStore.structureRevision; seenFilter=f; havePreview=true;
            const bool valid=f.valid(); const int matches=valid?historyStore.matching(f):0;
            preview->setText(valid?description(matches):QStringLiteral("起始日期不得晚於結束日期。"));
            erase->setEnabled(matches>0); exportSelection->setEnabled(matches>0);
        };
        auto setRange=[=] { const int index=range->currentIndex(); if(index<2) { first->setDate(today.addDays(index==0?-6:-29)); last->setDate(today); } first->setEnabled(index==2); last->setEnabled(index==2); update(); };
        connect(scope,&QComboBox::currentIndexChanged,&dialog,update);
        connect(range,&QComboBox::currentIndexChanged,&dialog,setRange);
        connect(first,&QDateEdit::dateChanged,&dialog,update); connect(last,&QDateEdit::dateChanged,&dialog,update);
        connect(&timer,&QTimer::timeout,&dialog,update); setRange(); cancel->setFocus();
        connect(cancel,&QPushButton::clicked,&dialog,&QDialog::reject);
        connect(exportSelection,&QPushButton::clicked,&dialog,[&,filter] {
            const QString path=QFileDialog::getSaveFileName(&dialog,QStringLiteral("匯出即將清除範圍的歷史"),QStringLiteral("歷史流量.csv"),QStringLiteral("CSV (*.csv)")); if(path.isEmpty()) return;
            QString error; if(!history::Store::atomic(path,historyStore.csvData(filter()),error)) QMessageBox::warning(&dialog,QStringLiteral("匯出失敗"),error);
            else QMessageBox::information(&dialog,QStringLiteral("匯出完成"),path);
        });
        connect(erase,&QPushButton::clicked,&dialog,[&,filter,description,update] {
            const auto selected=filter();
            QMessageBox confirm(QMessageBox::Warning,QStringLiteral("確認清除歷史流量"),description(historyStore.matching(selected))+QStringLiteral("\n\n清除後不可復原。建議先取消並匯出 CSV。\n是否確定清除上述日期與監控範圍的歷史？"),QMessageBox::Yes|QMessageBox::Cancel,&dialog);
            confirm.setObjectName("clearHistoryConfirm"); confirm.setButtonText(QMessageBox::Yes,QStringLiteral("確定清除")); confirm.setButtonText(QMessageBox::Cancel,QStringLiteral("取消"));
            confirm.setDefaultButton(QMessageBox::Cancel); confirm.setEscapeButton(QMessageBox::Cancel);
            if(confirm.exec()!=QMessageBox::Yes) return;
            int removed=0; QString error,notice;
            if(!historyStore.erase(selected,removed,error,notice)) { QMessageBox::warning(&dialog,QStringLiteral("清除失敗"),QStringLiteral("未清除歷史；主檔與記憶體資料保持原狀。\n%1").arg(error)); update(); return; }
            historySaveError.clear(); refreshDisplay();
            for(auto *widget:findChildren<QWidget *>()) if(auto *plot=dynamic_cast<HistoryChart *>(widget)) plot->update();
            update();
            QMessageBox::information(&dialog,QStringLiteral("清除完成"),QStringLiteral("已清除 %1 筆每日紀錄。新的有效取樣會繼續累積歷史。").arg(removed)+(notice.isEmpty()?QString():"\n"+notice));
        });
        dialog.exec();
    }
    void showHistory() {
        QDialog dialog(this); dialog.setWindowTitle(QStringLiteral("歷史流量")); dialog.resize(760,480);
        auto *layout=new QVBoxLayout(&dialog); auto *explanation=new QLabel(QStringLiteral("僅統計有效監控期間；最多保留 365 天。範圍各自獨立，全部介面可能重複計算。")); explanation->setWordWrap(true); layout->addWidget(explanation);
        auto *scope=new QComboBox; scope->setAccessibleName(QStringLiteral("歷史監控範圍")); QMap<QString,QString> scopes;
        for(const auto &d:historyStore.days) scopes[d.scope]=d.name;
        scopes[adapterSelector->currentData().toString()]=adapterSelector->currentText();
        for(auto it=scopes.begin();it!=scopes.end();++it) scope->addItem(it.value()+" ["+it.key()+"]",it.key());
        scope->setCurrentIndex(scope->findData(adapterSelector->currentData())); layout->addWidget(scope);
        auto *range=new QComboBox; range->setAccessibleName(QStringLiteral("歷史日期範圍")); range->addItem(QStringLiteral("最近 7 天"),7); range->addItem(QStringLiteral("最近 30 天"),30); range->addItem(QStringLiteral("全部保留歷史（圖表顯示最近 30 天）"),0); layout->addWidget(range);
        auto *legend=new QLabel(QStringLiteral("藍色：下載　綠色：上傳；無紀錄日期顯示空白。")); layout->addWidget(legend);
        auto *plot=new HistoryChart(&historyStore); plot->setObjectName("historyPlot"); layout->addWidget(plot,1);
        auto update=[plot,scope,range] { plot->scope=scope->currentData().toString(); plot->count=range->currentData().toInt()==7?7:30; plot->update(); };
        connect(scope,&QComboBox::currentIndexChanged,&dialog,update); connect(range,&QComboBox::currentIndexChanged,&dialog,update); update();
        connect(&retentionTimer,&QTimer::timeout,&dialog,update);
        auto *buttons=new QHBoxLayout; auto *exportHistory=new QPushButton(QStringLiteral("匯出所選範圍 CSV…")); buttons->addWidget(exportHistory); auto *clearHistory=new QPushButton(QStringLiteral("清除歷史流量…")); clearHistory->setObjectName("historyClearButton"); buttons->addWidget(clearHistory);
        connect(clearHistory,&QPushButton::clicked,&dialog,[this,&dialog,update]{ showClearHistory(&dialog); update(); }); buttons->addStretch(); auto *close=new QPushButton(QStringLiteral("關閉")); buttons->addWidget(close); layout->addLayout(buttons);
        connect(close,&QPushButton::clicked,&dialog,&QDialog::accept);
        connect(exportHistory,&QPushButton::clicked,&dialog,[this,&dialog,scope,range] {
            const QString key=scope->currentData().toString(); const int count=range->currentData().toInt();
            const QString path=QFileDialog::getSaveFileName(&dialog,QStringLiteral("匯出歷史統計"),QStringLiteral("每日流量.csv"),QStringLiteral("CSV (*.csv)")); if(path.isEmpty())return;
            QString error; if(!history::Store::atomic(path,historyStore.csvData(key,count),error)) QMessageBox::warning(&dialog,QStringLiteral("匯出失敗"),error);
            else QMessageBox::information(&dialog,QStringLiteral("匯出完成"),path);
        }); dialog.exec();
    }
    bool paused = false;
    std::deque<CsvRecord> records;
    quint64 discardedRecords = 0;
    bool unexportedRecords = false;
    QTimer retentionTimer;
    QLabel *retentionLabel = nullptr;
    QLabel *statsLabel = nullptr;
    QPushButton *pauseButton = nullptr;
    QPushButton *exportButton = nullptr;
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
    bool sampleValid = false;
};

class SingleInstance {
public:
    explicit SingleInstance(const QString &settingsPath)
        : lock(settingsPath + QStringLiteral(".instance.lock")),
          name(QStringLiteral("RealtimeNetworkSpeed-") + QString::fromLatin1(
              QCryptographicHash::hash(settingsPath.toLower().toUtf8(), QCryptographicHash::Sha256).toHex())) {
        QDir().mkpath(QFileInfo(settingsPath).absolutePath());
        lock.setStaleLockTime(0); // A long-running process must never lose its lock by age.
    }
    // 1: owner, 0: original window notified, -1: fail closed.
    int start() {
        if (lock.tryLock(0)) {
            server.setSocketOptions(QLocalServer::UserAccessOption);
            if (!server.listen(name)) return -1;
            return 1;
        }
        QElapsedTimer deadline; deadline.start();
        while (deadline.elapsed() < 3000) {
            QLocalSocket client;
            client.connectToServer(name);
            if (client.waitForConnected(150) && (client.canReadLine() || client.waitForReadyRead(300))) {
                bool ok = false;
                const auto pid = client.readLine().trimmed().toULong(&ok);
                if (ok) {
                    AllowSetForegroundWindow(pid);
                    client.write("activate\n");
                    if ((client.bytesToWrite() == 0 || client.waitForBytesWritten(300))
                        && (client.canReadLine() || client.waitForReadyRead(500))
                        && client.readLine() == "ok\n") return 0;
                }
            }
            QThread::msleep(50);
        }
        return -1;
    }
    void onActivation(SpeedWindow &window) {
        QObject::connect(&server, &QLocalServer::newConnection, &window, [this, &window] {
            while (auto *socket = server.nextPendingConnection()) {
                QObject::connect(socket, &QLocalSocket::disconnected, socket, &QObject::deleteLater);
                QObject::connect(socket, &QLocalSocket::readyRead, &window, [socket, &window] {
                    if (!socket->canReadLine()) return;
                    if (socket->readLine(32) == "activate\n") { window.activate(); socket->write("ok\n"); socket->flush(); }
                    socket->disconnectFromServer();
                });
                socket->write(QByteArray::number(QCoreApplication::applicationPid()) + "\n");
                socket->flush();
                QTimer::singleShot(5000, socket, [socket] { socket->abort(); socket->deleteLater(); });
            }
        });
    }
private:
    QLockFile lock;
    QString name;
    QLocalServer server;
};

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("RealtimeNetworkSpeed"));
    QCoreApplication::setApplicationName(QStringLiteral("RealtimeNetworkSpeed"));
    SingleInstance instance(displaySettings().fileName());
    const int state = instance.start();
    if (state == 0) return 0;
    if (state < 0) {
        QMessageBox::warning(nullptr, QStringLiteral("即時網路速度"),
            QStringLiteral("無法取得單一執行個體鎖定，或原視窗暫時未回應。請稍後再試。"));
        return 1;
    }
    SpeedWindow window;
    instance.onActivation(window);
    if(app.arguments().contains(QStringLiteral("--startup")) && QSystemTrayIcon::isSystemTrayAvailable()) window.hide();
    else window.show();
    return app.exec();
}
