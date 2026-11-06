#pragma once
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTimeZone>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMap>
#include <QSaveFile>
#include <QUuid>
#include <limits>
#include "traffic.h"

namespace history {
struct Day {
    QDate date;
    QString scope, name;
    quint64 down = 0, up = 0;
    double seconds = 0;
};
inline quint64 plus(quint64 a, quint64 b) { return a + std::min(b, std::numeric_limits<quint64>::max() - a); }
inline QString csv(QString s) {
    const auto t = s.trimmed();
    bool control = false;
    for (QChar c : s) if (c.unicode() < 32 || c.unicode() == 127) control = true;
    if (control || (!t.isEmpty() && QStringLiteral("=+-@").contains(t.front()))) s.prepend(QChar(39));
    s.replace('"', "\"\""); return '"' + s + '"';
}
// Empty scope means every independently recorded scope; invalid dates mean all dates.
struct Filter {
    QString scope;
    QDate first, last;
    bool valid() const { return (!first.isValid() && !last.isValid()) || (first.isValid() && last.isValid() && first <= last); }
    bool matches(const Day &d) const {
        return valid() && (scope.isEmpty() || scope == d.scope)
            && (!first.isValid() || (d.date >= first && d.date <= last));
    }
};
class Store {
public:
    explicit Store(QString file = {}) : path(std::move(file)) {}
    QString path, warning;
    QMap<QString, Day> days;
    QDate newest; // high-water date: a backwards clock cannot resurrect expired dates
    bool dirty = false, writable = true;
    static QString key(QDate date, const QString &scope) { return date.toString(Qt::ISODate) + '/' + scope; }
    void prune(QDate today) {
        newest = std::max(newest, today);
        for (auto it = days.begin(); it != days.end();) {
            if (it->date < newest.addDays(-364)) { it = days.erase(it); dirty = true; }
            else ++it;
        }
    }
    void addPart(QDate date, const QString &scope, const QString &name, quint64 down, quint64 up, double seconds) {
        if (date < newest.addDays(-364)) return;
        auto &d = days[key(date, scope)]; d.date = date; d.scope = scope; d.name = name;
        d.down = plus(d.down, down); d.up = plus(d.up, up); d.seconds += seconds; dirty = true;
    }
    // Only call with a valid monotonic sample; wall-clock jumps are rejected here too.
    void add(const QString &scope, const QString &name, const traffic::Rates &r,
             QDateTime start, QDateTime end) {
        if (!(r.seconds > 0) || !start.isValid() || !end.isValid()) return;
        const qint64 ms = start.msecsTo(end);
        if (ms <= 0 || std::abs(ms / 1000.0 - r.seconds) > 0.25) return;
        prune(end.date());
        if (start.date() == end.date()) { addPart(end.date(), scope, name, r.inBytes, r.outBytes, r.seconds); return; }
        const QDateTime midnight(end.date(), QTime(0,0), end.timeZone());
        const double fraction = std::clamp(double(start.msecsTo(midnight)) / double(ms), 0.0, 1.0);
        const quint64 down = quint64(r.inBytes * static_cast<long double>(fraction));
        const quint64 up = quint64(r.outBytes * static_cast<long double>(fraction));
        addPart(start.date(), scope, name, down, up, r.seconds * fraction);
        addPart(end.date(), scope, name, r.inBytes - down, r.outBytes - up, r.seconds * (1-fraction));
    }
    Day totals(const QString &scope, QDate first, QDate last) const {
        Day result;
        for (const auto &d : days) if (d.scope == scope && d.date >= first && d.date <= last) {
            result.down = plus(result.down, d.down); result.up = plus(result.up, d.up); result.seconds += d.seconds;
        }
        return result;
    }
    QByteArray encode() const {
        QJsonArray rows;
        for (const auto &d : days) rows.append(QJsonObject{{"date",d.date.toString(Qt::ISODate)},
            {"scope",d.scope},{"name",d.name},{"download",QString::number(d.down)},
            {"upload",QString::number(d.up)},{"seconds",d.seconds}});
        return QJsonDocument(QJsonObject{{"version",1},{"newest",newest.toString(Qt::ISODate)},{"days",rows}}).toJson();
    }
    static bool decode(const QByteArray &bytes, QMap<QString,Day> &result, QDate &high) {
        QJsonParseError err;
        const auto doc = QJsonDocument::fromJson(bytes, &err);
        if (err.error != QJsonParseError::NoError || !doc.isObject()) return false;
        const auto obj = doc.object();
        if (obj["version"].toInt(-1) != 1 || !obj["days"].isArray()) return false;
        high = QDate::fromString(obj["newest"].toString(),Qt::ISODate);
        if (!high.isValid()) return false;
        QMap<QString,Day> parsed;
        const auto rows = obj["days"].toArray();
        if (rows.size() > 36500) return false;
        for (const auto &row : rows) {
            const auto o = row.toObject();
            Day d; d.date = QDate::fromString(o["date"].toString(),Qt::ISODate);
            d.scope=o["scope"].toString(); d.name=o["name"].toString();
            bool a=false,b=false; const auto ds=o["download"].toString(), us=o["upload"].toString();
            d.down=ds.toULongLong(&a); d.up=us.toULongLong(&b); d.seconds=o["seconds"].toDouble(-1);
            if (!d.date.isValid() || d.date > high || d.scope.isEmpty() || d.scope.size()>256 || d.name.size()>2048
                || !a || !b || QString::number(d.down)!=ds || QString::number(d.up)!=us
                || !std::isfinite(d.seconds) || d.seconds<0 || parsed.contains(key(d.date,d.scope))) return false;
            parsed.insert(key(d.date,d.scope), d);
        }
        result=parsed; return true;
    }
    bool read(const QString &file, QMap<QString,Day> &result, QDate &high) const {
        QFile f(file); return f.open(QIODevice::ReadOnly) && f.size()<=16*1024*1024 && decode(f.readAll(),result,high);
    }
    bool preserve(const QString &file) {
        if (!QFile::exists(file)) return true;
        const QString target=file+".corrupt-"+QUuid::createUuid().toString(QUuid::Id128);
        if (QFile::copy(file,target)) return true;
        writable=false; warning=QStringLiteral("無法保留異常歷史檔，已停止寫入；請檢查資料夾權限。"); return false;
    }
    void load(QDate today = QDate::currentDate()) {
        days.clear(); newest=today; dirty=false; writable=true; warning.clear();
        if (QFile::exists(path)) {
            if (!read(path,days,newest)) {
                preserve(path);
                if (read(path+".bak",days,newest)) warning=QStringLiteral("歷史主檔異常，已保留並從最後有效備份復原。");
                else { if (QFile::exists(path+".bak")) preserve(path+".bak");
                    if (writable) warning=QStringLiteral("歷史檔異常，已保留原檔；由空白統計重新開始。"); }
                dirty=true;
            }
        } else if (QFile::exists(path+".bak")) {
            if (read(path+".bak",days,newest)) { warning=QStringLiteral("歷史主檔遺失，已從備份復原。"); dirty=true; }
            else { preserve(path+".bak"); dirty=true; }
        }
        prune(today);
    }
    static bool atomic(const QString &file, const QByteArray &bytes, QString &error) {
        QSaveFile f(file); f.setDirectWriteFallback(false);
        if (!f.open(QIODevice::WriteOnly) || f.write(bytes)!=bytes.size() || !f.commit()) { error=f.errorString(); return false; }
        return true;
    }
    bool save(QString &error) {
        if (!writable) { error=warning; return false; }
        if (!dirty) return true;
        if (!QDir().mkpath(QFileInfo(path).absolutePath())) { error=QStringLiteral("無法建立歷史資料夾"); return false; }
        QMap<QString,Day> old; QDate date;
        if (QFile::exists(path) && read(path,old,date)) {
            QFile f(path); if (!f.open(QIODevice::ReadOnly) || !atomic(path+".bak",f.readAll(),error)) return false;
        }
        if (!atomic(path,encode(),error)) return false;
        // First successful save also provides a recovery point.
        if (!QFile::exists(path+".bak") && !atomic(path+".bak",encode(),error)) return false;
        dirty=false; return true;
    }
    int matching(const Filter &filter) const {
        int count=0; for (const auto &day : days) if (filter.matches(day)) ++count; return count;
    }
    // Commit point is the atomic replacement of the main JSON. Never mutate live
    // memory before it succeeds. Invalidate old recovery data first so neither a
    // crash nor a failed backup refresh can resurrect intentionally deleted rows.
    bool erase(const Filter &filter, int &removed, QString &error, QString &notice) {
        removed=0; error.clear(); notice.clear();
        if (!filter.valid()) { error=QStringLiteral("起始日期不得晚於結束日期。"); return false; }
        if (!writable) { error=warning; return false; }
        Store candidate=*this;
        for (auto it=candidate.days.begin(); it!=candidate.days.end();) {
            if (filter.matches(it.value())) { it=candidate.days.erase(it); ++removed; } else ++it;
        }
        if (!removed) return true;
        removed=0;
        if (!QDir().mkpath(QFileInfo(path).absolutePath())) { error=QStringLiteral("無法建立歷史資料夾"); return false; }
        const QString backup=path+".bak";
        QByteArray previousBackup;
        const bool hadBackup=QFile::exists(backup);
        if (hadBackup) {
            QFile f(backup);
            if (!f.open(QIODevice::ReadOnly)) { error=f.errorString(); return false; }
            previousBackup=f.readAll();
            if (f.error()!=QFileDevice::NoError) { error=f.errorString(); return false; }
            f.close();
            if (!QFile::remove(backup)) { error=QStringLiteral("無法移除舊復原備份，未清除任何歷史。"); return false; }
        }
        const QByteArray bytes=candidate.encode();
        if (!atomic(path,bytes,error)) {
            QString backupError;
            if (hadBackup && !atomic(backup,previousBackup,backupError))
                error+=QStringLiteral("；主檔與記憶體未變更，但復原備份無法還原：%1").arg(backupError);
            return false;
        }
        removed=days.size()-candidate.days.size();
        days=std::move(candidate.days); dirty=false;
        // No failure may be reported after the authoritative main-file commit.
        // A missing backup is safe; an old backup would not be.
        QString backupError;
        if (!atomic(backup,bytes,backupError)) notice=QStringLiteral("歷史已清除並保存；復原備份暫時無法建立：%1").arg(backupError);
        return true;
    }
    QByteArray csvData(const QString &scope, int count, QDate today=QDate::currentDate()) const {
        return csvData(Filter{scope,count ? today.addDays(1-count) : QDate(),count ? today : QDate()});
    }
    QByteArray csvData(const Filter &filter) const {
        QByteArray out=QByteArray::fromHex("efbbbf")+"Date,Adapter_ID,Download_Bytes,Upload_Bytes,Total_Bytes,Active_Seconds\r\n";
        for (const auto &d : days) if (filter.matches(d)) {
            // Decimal addition keeps Total_Bytes exact even when two uint64 values exceed uint64.
            QString a=QString::number(d.down), b=QString::number(d.up), total; int carry=0;
            while (!a.isEmpty() || !b.isEmpty() || carry) { int n=carry;
                if(!a.isEmpty()) { n+=a.back().digitValue(); a.chop(1); }
                if(!b.isEmpty()) { n+=b.back().digitValue(); b.chop(1); }
                total.prepend(QChar('0'+n%10)); carry=n/10;
            }
            if(total.isEmpty()) total="0";
            out+=(QStringList{d.date.toString(Qt::ISODate),csv(d.scope),QString::number(d.down),QString::number(d.up),total,QString::number(d.seconds,'g',17)}.join(',')+"\r\n").toUtf8();
        }
        return out;
    }
};

// State transitions require three consecutive observations; first observation and reset are silent.
struct Notifications {
    int stable=-1, pending=-1, count=0;
    void reset() { stable=pending=-1; count=0; }
    int observe(int state) {
        if (stable<0) { stable=state; return -1; }
        if (state==stable) { pending=-1; count=0; return -1; }
        if (pending!=state) { pending=state; count=1; } else ++count;
        if (count<3) return -1;
        stable=state; pending=-1; count=0; return state;
    }
};
} // namespace history
