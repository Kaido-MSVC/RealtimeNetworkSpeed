#define HISTORY_VERIFICATION
#define main applicationMain
#include "../main.cpp"
#undef main
#include <QtTest/QTest>
#include <QTemporaryDir>
#include <QProcess>
#include <QDir>
#include <QPointer>
#include <QScrollBar>
#include <iostream>
#include <psapi.h>

static int failures = 0;
static void check(bool condition, const char *message) {
    if (!condition) { std::cerr << "FAIL: " << message << '\n'; ++failures; }
}

struct Verification {
    static int measureCsv(bool independent) {
        auto memory=[] { PROCESS_MEMORY_COUNTERS_EX m{}; m.cb=sizeof(m);
            K32GetProcessMemoryInfo(GetCurrentProcess(),reinterpret_cast<PROCESS_MEMORY_COUNTERS *>(&m),sizeof(m)); return m; };
        std::deque<CsvRecord> records;
        const QString adapter=QStringLiteral("Intel(R) Ethernet Controller I225-V / 實體網路介面");
        const QString key=QStringLiteral("{01234567-89ab-cdef-0123-456789abcdef}");
        const auto wall=QDateTime(QDate(2026,11,6),QTime(0,0),QTimeZone::UTC);
        const auto before=memory(); const auto begin=std::chrono::steady_clock::now();
        for(int i=0;i<172800;++i) {
            traffic::Rates rates; rates.seconds=.5; rates.inBytes=12345; rates.outBytes=6789;
            records.push_back({wall.addMSecs(i*500LL).toString(Qt::ISODateWithMs),
                independent?QString::fromUtf8(adapter.toUtf8()):adapter,
                independent?QString::fromUtf8(key.toUtf8()):key,rates,{},begin+std::chrono::milliseconds(i*500LL)});
        }
        const auto full=memory();
        QTemporaryDir dir; QString error;
        const auto exportStart=std::chrono::steady_clock::now();
        const bool ok=writeCsv(dir.filePath("full.csv"),records,error);
        const auto exported=memory();
        std::cout << "CSV_MEMORY mode=" << (independent?"independent-strings":"shared-adapter-and-key")
            << " count=" << records.size() << " sizeof_record=" << sizeof(CsvRecord)
            << " private_delta=" << (qint64(full.PrivateUsage)-qint64(before.PrivateUsage))
            << " working_set_delta=" << (qint64(full.WorkingSetSize)-qint64(before.WorkingSetSize))
            << " export_peak_working_set=" << exported.PeakWorkingSetSize
            << " export_ms=" << std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now()-exportStart).count()
            << " csv_bytes=" << QFileInfo(dir.filePath("full.csv")).size() << " ok=" << ok << '\n';
        return ok?0:1;
    }
    static void verifyAudit(SpeedWindow &w,const QString &captures) {
        QTemporaryDir dir; const QDate today(2026,11,6);
        const auto mono=history::Store::Clock::now();
        const QDateTime wall(today,QTime(12,0),QTimeZone::UTC);
        traffic::Rates r; r.inBytes=100; r.outBytes=20; r.seconds=.5;
        history::Store store(dir.filePath("guard.json")); store.load(today);
        store.addPart(today.addDays(-364),"p","p",1,2,.5);
        store.add("p","p",r,wall,wall.addMSecs(500),mono);
        QString error; check(store.save(error),"audit initial trusted checkpoint");
        const auto original=store.encode(), main=bytes(store.path), backup=bytes(store.path+".bak");
        const auto future=wall.addYears(2);
        for(int i=0;i<6;++i) {
            store.add("p","p",r,future.addMSecs(i*500),future.addMSecs((i+1)*500),mono+std::chrono::milliseconds((i+1)*500));
            store.prune(future.date(),mono+std::chrono::seconds(i));
            check(store.save(error),"audit quarantined periodic save is safe");
        }
        check(store.encode()==original && bytes(store.path)==main && bytes(store.path+".bak")==backup
            && !store.clockWarning.isEmpty(),"audit multiple future samples and retention ticks preserve all memory/main/backup");
        store.add("p","p",r,wall.addSecs(10),wall.addSecs(10).addMSecs(500),mono+std::chrono::seconds(10));
        check(store.clockWarning.isEmpty() && store.totals("p",today,today).down==200,"audit corrected clock resumes immediately");
        const auto corrected=store.encode();
        store.add("p","p",r,wall.addYears(-2),wall.addYears(-2).addMSecs(500),mono+std::chrono::seconds(11));
        check(store.encode()==corrected && !store.clockWarning.isEmpty(),"audit backward multi-year clock quarantined");
        // Natural long-running time progresses via the independent monotonic anchor.
        store.anchor(today,mono);
        const auto next=wall.addDays(365);
        store.add("p","p",r,next,next.addMSecs(500),mono+std::chrono::hours(365*24));
        check(store.newest==today.addDays(365) && store.days.size()==1,"audit natural elapsed year advances retention and prunes expired rows");
        // Startup forward gaps are legitimate powered-off intervals. Preserve before pruning.
        for (int gap : {3,7,30}) {
            history::Store seed(dir.filePath(QString("startup-%1.json").arg(gap))); seed.load(today);
            seed.addPart(today.addDays(-364),"p","p",1,2,.5);
            seed.addPart(today,"p","p",11,2,.5);
            check(seed.save(error),"startup fixture saved");
            const auto complete=seed.encode();
            history::Store restart(seed.path); restart.load(today.addDays(gap));
            const auto snapshots=QDir(dir.path()).entryList({QFileInfo(seed.path).fileName()+".date-recovery-*"},QDir::Files);
            check(restart.clockWarning.isEmpty() && restart.newest==today.addDays(gap) && restart.days.size()==1
                && snapshots.size()==1 && bytes(dir.filePath(snapshots.value(0)))==complete,
                "startup 3/7/30 day absence snapshots full history before pruning");
            const auto resumed=wall.addDays(gap);
            restart.add("p","p",r,resumed,resumed.addMSecs(500));
            check(restart.totals("p",resumed.date(),resumed.date()).down==100,"startup 3/7/30 day absence records normally");
            check(restart.save(error),"startup resumed history persists");
            history::Store reload(seed.path); reload.load(resumed.date());
            check(reload.totals("p",resumed.date(),resumed.date()).down==100,"startup resumed history survives restart");
        }
        history::Store mismatch; mismatch.load(today);
        mismatch.add("p","p",r,wall,wall.addSecs(3600),mono);
        check(mismatch.clockWarning.isEmpty() && !mismatch.intervalWarning.isEmpty() && mismatch.days.isEmpty(),"within-day wall clock jump reports skipped history sample");
        mismatch.add("p","p",r,wall.addSecs(3600),wall.addSecs(3600).addMSecs(500),mono+std::chrono::seconds(1));
        check(mismatch.clockWarning.isEmpty() && mismatch.intervalWarning.isEmpty() && mismatch.totals("p",today,today).down==100,"valid interval after within-day clock jump resumes history");
        history::Store recent(dir.filePath("recent.json")); recent.load(today);
        recent.addPart(today,"p","p",5,2,.5); check(recent.save(error),"recent fixture");
        recent.load(today.addDays(30));
        check(QDir(dir.path()).entryList({"recent.json.date-recovery-*"},QDir::Files).isEmpty()
            && recent.clockWarning.isEmpty(),"startup without expired rows needs no snapshot");
        check(recent.save(error),"persist forward date for backwards-startup fixture");
        history::Store backwards(recent.path); backwards.load(today.addDays(27));
        const auto backwardsBefore=backwards.encode();
        backwards.add("p","p",r,wall.addDays(27),wall.addDays(27).addMSecs(500));
        check(!backwards.clockWarning.isEmpty() && backwards.encode()==backwardsBefore,"startup backwards more than two days blocks explicitly without changing data");
        backwards.load(today.addDays(28));
        check(backwards.clockWarning.isEmpty(),"startup backwards two days remains accepted");
        history::Store restart(store.path);
        const auto snapshotFiles=[&] { return QDir(dir.path()).entryList({"guard.json.date-recovery-*"},QDir::Files); };
        history::Store::failWrite=[](const QString &file) { return file.contains(".date-recovery-"); };
        restart.load(today.addDays(30));
        check(restart.encode()==original && bytes(restart.path)==main && bytes(restart.path+".bak")==backup
            && !restart.clockWarning.isEmpty() && snapshotFiles().isEmpty(),"snapshot creation failure preserves memory main backup and expired rows");
        restart.add("p","p",r,wall.addDays(30),wall.addDays(30).addMSecs(500));
        check(restart.encode()==original && snapshotFiles().isEmpty(),"snapshot failure retry cannot accumulate or prune");
        history::Store::failWrite={};
        check(restart.prune(today.addDays(30)) && restart.clockWarning.isEmpty() && snapshotFiles().size()==1,
            "snapshot storage recovery retries automatically before pruning");
        check(bytes(dir.filePath(snapshotFiles().first()))==original,"automatic recovery snapshot is complete");
        // Force manual reanchor; a failed main commit must remove its provisional snapshot.
        restart.load(today); restart.prune(today.addYears(2));
        const auto beforeFiles=snapshotFiles();
        HANDLE locked=CreateFileW(reinterpret_cast<LPCWSTR>(restart.path.utf16()),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
        check(locked!=INVALID_HANDLE_VALUE,"audit lock date-recovery primary");
        if(locked!=INVALID_HANDLE_VALUE) {
            check(!restart.confirmDate(today.addYears(2),error) && restart.encode()==original
                && bytes(restart.path)==main && bytes(restart.path+".bak")==backup && snapshotFiles()==beforeFiles,
                "failed date confirmation preserves memory primary backup and removes provisional snapshot");
            CloseHandle(locked);
        }
        for(int i=0;i<5;++i) {
            restart.prune(today.addYears(3+i));
            check(restart.confirmDate(today.addDays(i+1),error),"manual date recovery succeeds");
            check(snapshotFiles().size()<=3,"manual snapshots rotate to at most three");
            const auto acceptedFiles=snapshotFiles(); const auto accepted=restart.encode();
            const auto warning=restart.warning, notice=restart.dateNotice;
            check(restart.confirmDate(today.addDays(i+1),error) && snapshotFiles()==acceptedFiles
                && restart.encode()==accepted && restart.warning==warning && restart.dateNotice==notice,
                "repeated healthy confirmation is no-op without snapshots or appended warnings");
        }
        const auto keptFiles=snapshotFiles();
        const auto keptState=restart.encode();
        restart.prune(today.addYears(20));
        bool sawUnrotated=false;
        history::Store::failWrite=[&](const QString &file) {
            if (!file.contains(".date-recovery-")) return false;
            sawUnrotated=snapshotFiles()==keptFiles; return true;
        };
        check(!restart.confirmDate(today,error) && sawUnrotated && restart.encode()==keptState && snapshotFiles()==keptFiles,
            "failed new snapshot never rotates existing three snapshots");
        history::Store::failWrite={};
        history::Store::failWrite=[&](const QString &file) { return file==restart.path; };
        const auto committedMain=bytes(restart.path), committedBackup=bytes(restart.path+".bak");
        check(!restart.confirmDate(today,error) && restart.encode()==keptState && snapshotFiles()==keptFiles
            && bytes(restart.path)==committedMain && bytes(restart.path+".bak")==committedBackup,
            "injected main commit failure restores backup and retains all prior snapshots");
        history::Store::failWrite={};
        // Both delete-backup failure and post-commit backup failure exercise real transaction boundaries.
        const auto backupPath=restart.path+".bak";
        locked=CreateFileW(reinterpret_cast<LPCWSTR>(backupPath.utf16()),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
        check(locked!=INVALID_HANDLE_VALUE,"lock confirmation backup");
        if(locked!=INVALID_HANDLE_VALUE) {
            check(!restart.confirmDate(today,error) && restart.encode()==keptState && snapshotFiles()==keptFiles,
                "backup removal failure discards provisional snapshot without rotating existing files");
            CloseHandle(locked);
        }
        history::Store::failWrite=[&](const QString &file) { return file==backupPath; };
        check(restart.confirmDate(today,error) && bytes(restart.path)==restart.encode() && !QFile::exists(backupPath)
            && restart.dateNotice.contains(QStringLiteral("復原備份暫時無法建立")),"post-commit backup failure retains committed main and reports success with notice");
        history::Store::failWrite={};
        // Automatic rotation also retains the newest complete pre-prune state.
        for(int i=0;i<5;++i) {
            restart.addPart(restart.newest.addDays(-364),"expired","expired",quint64(i+1),0,.5);
            restart.dirty=true; check(restart.save(error),"automatic rotation seed saved");
            const auto full=restart.encode();
            restart.load(restart.newest.addDays(3));
            bool found=false; for(const auto &file:snapshotFiles()) if(bytes(dir.filePath(file))==full) found=true;
            check(found && snapshotFiles().size()==3 && restart.clockWarning.isEmpty(),"automatic rotation preserves newest full snapshot and retains three");
        }
        const auto beforeRotation=snapshotFiles();
        QList<HANDLE> snapshotLocks;
        for(const auto &file:beforeRotation) {
            const auto path=dir.filePath(file);
            HANDLE handle=CreateFileW(reinterpret_cast<LPCWSTR>(path.utf16()),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
            check(handle!=INVALID_HANDLE_VALUE,"lock existing recovery snapshot for rotation failure");
            if(handle!=INVALID_HANDLE_VALUE) snapshotLocks.append(handle);
        }
        const auto beforeLockedRecovery=restart.encode();
        restart.prune(restart.newest.addYears(2));
        check(restart.confirmDate(today,error) && snapshotFiles().size()==4
            && restart.dateNotice.contains(QStringLiteral("無法清理舊日期快照")),
            "locked old snapshot reports cleanup failure while preserving committed recovery");
        bool completeNew=false;
        for(const auto &file:snapshotFiles()) if(!beforeRotation.contains(file) && bytes(dir.filePath(file))==beforeLockedRecovery) completeNew=true;
        check(completeNew,"rotation cleanup failure retains complete newest snapshot");
        for(HANDLE handle:snapshotLocks) CloseHandle(handle);
        restart.prune(today.addYears(2));
        check(restart.confirmDate(today,error) && snapshotFiles().size()==3
            && !restart.dateNotice.contains(QStringLiteral("無法清理舊日期快照")),
            "next successful recovery retries cleanup and restores three-snapshot bound");
        history::Store poisoned(dir.filePath("poison.json")); poisoned.newest=today.addYears(2);
        poisoned.days[history::Store::key(today,"p")]=history::Day{today,"p","p",7,3,.5};
        poisoned.dirty=true; check(poisoned.save(error),"audit legacy future high fixture");
        poisoned.load(today);
        check(poisoned.newest==today.addYears(2) && poisoned.days.size()==1 && !poisoned.clockWarning.isEmpty(),"audit future high on disk is quarantined without pruning");
        check(poisoned.confirmDate(today,error),"audit explicit legacy future high repair");
        poisoned.add("p","p",r,wall,wall.addMSecs(500));
        check(poisoned.newest==today && poisoned.totals("p",today,today).down==107,"audit no year-long rejection after explicit legacy repair");
        QMap<QString,history::Day> result=poisoned.days; QDate high=today;
        const auto resultBefore=result;
        const QByteArray invalid="{\"version\":1,\"newest\":\"2099-01-01\",\"days\":[{}]}";
        check(!history::Store::decode(invalid,result,high) && high==today && result.keys()==resultBefore.keys()
            && result.first().down==resultBefore.first().down,"audit invalid decode commits neither output");
        check(poisoned.save(error),"audit persist repaired legacy checkpoint");
        const auto validBackup=bytes(poisoned.path+".bak");
        QMap<QString,history::Day> backupRows; QDate backupHigh;
        check(history::Store::decode(validBackup,backupRows,backupHigh),"audit valid backup fixture decoded");
        check(history::Store::atomic(poisoned.path,invalid,error),"audit invalid future main fixture");
        poisoned.load(today);
        check(poisoned.newest==backupHigh && poisoned.days.size()==backupRows.size()
            && poisoned.clockWarning.isEmpty(),"audit invalid future high cannot contaminate valid backup restoration");
        check(history::Store::atomic(poisoned.path,invalid,error) && history::Store::atomic(poisoned.path+".bak",invalid,error),"audit both invalid future-high files fixture");
        poisoned.load(today);
        check(poisoned.newest==today && poisoned.days.isEmpty(),"audit both invalid files cannot poison fresh baseline");
        check(HistoryChart::axisLabel(2e9)==QStringLiteral("2.00 GB") && !HistoryChart::axisLabel(2e12).contains("e+"),"audit fixed decimal GB axis without scientific notation");
        history::Store chartStore; chartStore.load(); chartStore.addPart(QDate::currentDate(),"p","p",2000000000ULL,1000000000ULL,.5);
        HistoryChart chart(&chartStore); chart.scope="p"; chart.resize(500,230);
        if(!captures.isEmpty()) chart.grab().save(captures+"/audit-history-2GB.png");
        chart.resize(60,230); check(!chart.grab().isNull(),"audit narrow history chart renders safely");
        chart.count=0; check(!chart.grab().isNull(),"audit empty chart range safe");
        // Real processSample path: session/CSV resume in the future, history stays quarantined.
        w.timer.stop(); w.retentionTimer.stop(); w.clearStatistics(); w.paused=false;
        w.historyStore=history::Store(dir.filePath("integration.json")); w.historyStore.load();
        traffic::InterfaceInfo nic; nic.luid=456; nic.guid="{audit}"; nic.hardware=true; nic.up=true;
        w.savedAdapter="physical"; w.updateAdapterList({nic});
        const auto current=QDateTime(QDate::currentDate(),QTime(12,0),QTimeZone::systemTimeZone());
        w.processSample({nic},true,mono,{},current); nic.inOctets=100;
        w.processSample({nic},true,mono+std::chrono::milliseconds(500),{},current.addMSecs(500));
        const auto checkpoint=w.historyStore.encode();
        w.historyBlockedNotified=false;
        nic.inOctets+=100; w.processSample({nic},true,mono+std::chrono::seconds(1),{},current.addYears(2));
        nic.inOctets+=100; w.processSample({nic},true,mono+std::chrono::milliseconds(1500),{},current.addYears(2).addMSecs(500));
        check(w.historyStore.encode()==checkpoint && w.session.down==200 && w.records.size()==2
            && !w.confirmHistoryDate->isHidden(),"audit processSample retains real-time and CSV with visible date recovery action");
        if(QSystemTrayIcon::isSystemTrayAvailable() && QSystemTrayIcon::supportsMessages()) {
            check(w.historyBlockedNotified && !w.notifyHistoryBlocked(),"history block sends first tray notification and suppresses repeated samples");
        }
        nic.inOctets+=100; w.processSample({nic},true,mono+std::chrono::seconds(2),{},current.addSecs(2));
        nic.inOctets+=100; w.processSample({nic},true,mono+std::chrono::milliseconds(2500),{},current.addMSecs(2500));
        check(w.historyStore.clockWarning.isEmpty() && w.historyStore.totals("physical",current.date(),current.date()).down==200,"audit processSample returns to trusted clock");
        check(!w.historyBlockedNotified,"history recovery resets tray notification latch");
        w.historyStore.prune(current.date().addYears(-2));
        check(w.notifyHistoryBlocked()==(QSystemTrayIcon::isSystemTrayAvailable() && QSystemTrayIcon::supportsMessages()),
            "new blocking episode can send a fresh notification");
        w.historyStore.prune(current.date()); w.refreshDisplay();
        // An interval mismatch is visible text, never a date action or tray blockage.
        w.historyStore.add("physical","Physical",r,current,current.addSecs(3600),mono+std::chrono::seconds(3));
        w.refreshDisplay();
        check(w.historyStore.clockWarning.isEmpty() && !w.historyStore.intervalWarning.isEmpty()
            && w.confirmHistoryDate->isHidden() && !w.historyBlockedNotified && !w.notifyHistoryBlocked()
            && w.note->text().contains(w.historyStore.intervalWarning),"interval mismatch shows text without date action or tray blockage");
        w.historyStore.add("physical","Physical",r,current,current.addMSecs(500),mono+std::chrono::seconds(4));
        w.refreshDisplay();
        check(w.historyStore.intervalWarning.isEmpty(),"next valid sample clears interval notice");
        // Drive date confirmation itself in the isolated settings/data fixture.
        w.historyStore.prune(current.date().addYears(2)); w.refreshDisplay();
        const auto beforeConfirm=w.historyStore.encode(); bool sawCancel=false;
        QTimer::singleShot(50,&w,[&] {
            if(auto *box=qobject_cast<QMessageBox *>(QApplication::activeModalWidget())) {
                sawCancel=true; check(box->defaultButton()==box->button(QMessageBox::Cancel),"audit date confirmation defaults to cancel");
                if(!captures.isEmpty()) box->grab().save(captures+"/audit-date-confirm.png");
                box->button(QMessageBox::Cancel)->click();
            }
        });
        w.confirmHistoryDate->click();
        check(sawCancel && w.historyStore.encode()==beforeConfirm && !w.historyStore.clockWarning.isEmpty(),"audit date confirmation cancellation keeps quarantine and all data");
        QTimer::singleShot(50,&w,[&] { if(auto *box=qobject_cast<QMessageBox *>(QApplication::activeModalWidget())) box->button(QMessageBox::Yes)->click(); });
        w.confirmHistoryDate->click();
        check(w.historyStore.clockWarning.isEmpty() && w.confirmHistoryDate->isHidden(),"audit confirmed date recovery hides action and resumes");
        w.historyStore.dirty=false; w.clearStatistics(); w.setPaused(true);
        std::cout << "AUDIT: date quarantine, restart, explicit recovery snapshot, transactional decode and chart regression completed\n";
    }
    static int miniRestored() {
        SpeedWindow window; window.timer.stop();
        check(window.mini->windowOpacity()>=.49 && window.mini->windowOpacity()<=.51
            && window.miniValue->font().pointSize()==15 && window.miniValue->text().contains(QStringLiteral("本次")),
            "mini settings survive process restart");
        return failures?1:0;
    }
    static int f10NormalExit(QApplication &app,const QString &directory) {
        SpeedWindow w; w.timer.stop(); w.retentionTimer.stop();
        w.historyStore=history::Store(QDir(directory).filePath("normal-exit.json"));
        const auto today=QDate::currentDate(); w.historyStore.load(today);
        w.historyStore.addPart(today,"physical","實體",900,400,1);
        QString error,notice; int removed;
        if(!w.historyStore.save(error) || !w.historyStore.erase({"",{},{}},removed,error,notice)) return 2;
        w.historyStore.addPart(today,"physical","實體",19,7,.5);
        QTimer::singleShot(0,&app,&QCoreApplication::quit);
        return app.exec();
    }
    static QByteArray bytes(const QString &path) {
        QFile f(path); return f.open(QIODevice::ReadOnly)?f.readAll():QByteArray();
    }
    static void verifyF10(const QString &captures) {
        QTemporaryDir dir;
        const QDate today=QDate::currentDate();
        const QString path=dir.filePath("delete.json");
        history::Store store(path); store.load(today);
        for(int i=0;i<40;++i) for(const QString &scope:{QString("physical"),QString("all"),QString("{nic-a}")})
            store.addPart(today.addDays(-i),scope,scope,100+i,200+i,.5);
        QString error,notice; int removed=-1;
        check(store.save(error),"F10 seed primary and backup");
        const auto initial=store.encode();
        history::Filter seven{"physical",today.addDays(-6),today};
        check(store.matching(seven)==7 && store.csvData(seven).count('\n')==8,"F10 inclusive seven-day export matches deletion preview");
        check(store.erase(seven,removed,error,notice) && removed==7 && notice.isEmpty(),"F10 delete inclusive seven days in one scope");
        QMap<QString,history::Day> expected; QDate high;
        check(history::Store::decode(initial,expected,high),"F10 decode expected source");
        for(auto it=expected.begin();it!=expected.end();) if(seven.matches(it.value())) it=expected.erase(it); else ++it;
        history::Store expectedStore; expectedStore.days=expected; expectedStore.newest=high;
        check(store.encode()==expectedStore.encode(),"F10 every unselected row and high-water date preserved exactly");
        check(bytes(path)==store.encode() && bytes(path+".bak")==store.encode() && !store.dirty,"F10 commit primary memory backup agree");
        check(store.save(error) && bytes(path)==store.encode(),"F10 later periodic save cannot resurrect old data");
        history::Store restarted(path); restarted.load(today);
        check(restarted.encode()==store.encode(),"F10 reload preserves deletion");
        check(history::Store::atomic(path,"{corrupt",error),"F10 corrupt main after deletion fixture");
        restarted.load(today);
        check(restarted.matching(seven)==0 && restarted.encode()==store.encode(),"F10 backup recovery cannot resurrect deleted history");
        check(restarted.save(error),"F10 recovery save remains deleted");
        store=restarted;
        // Unsaved data is part of the transaction and must survive outside its filter.
        store.addPart(today,"all","all",77,33,.5);
        const auto beforeFailure=store.encode(); const auto diskBefore=bytes(path),backupBefore=bytes(path+".bak");
        history::Filter thirty{"{nic-a}",today.addDays(-29),today};
        HANDLE locked=CreateFileW(reinterpret_cast<LPCWSTR>(path.utf16()),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
        check(locked!=INVALID_HANDLE_VALUE,"F10 lock primary against atomic replacement");
        if(locked!=INVALID_HANDLE_VALUE) {
            check(!store.erase(thirty,removed,error,notice) && removed==0 && store.dirty
                  && store.encode()==beforeFailure && bytes(path)==diskBefore && bytes(path+".bak")==backupBefore,
                  "F10 main commit failure preserves unsaved memory primary and restores backup");
            CloseHandle(locked);
        }
        locked=CreateFileW(reinterpret_cast<LPCWSTR>((path+".bak").utf16()),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
        check(locked!=INVALID_HANDLE_VALUE,"F10 lock backup against invalidation");
        if(locked!=INVALID_HANDLE_VALUE) {
            check(!store.erase(thirty,removed,error,notice) && store.encode()==beforeFailure
                  && bytes(path)==diskBefore && bytes(path+".bak")==backupBefore,"F10 locked backup aborts before modifying primary or memory");
            CloseHandle(locked);
        }
        check(!store.erase({"all",today,today.addDays(-1)},removed,error,notice) && store.encode()==beforeFailure,"F10 inverted range rejected");
        check(!store.erase({"all",QDate(),today},removed,error,notice) && store.encode()==beforeFailure,"F10 partial date filter rejected");
        check(store.erase({"missing",{},{}},removed,error,notice) && removed==0 && bytes(path)==diskBefore && store.dirty,"F10 empty match is a no-op preserving dirty data");
        store.writable=false;
        check(!store.erase(thirty,removed,error,notice) && store.encode()==beforeFailure,"F10 disabled history writes reject deletion"); store.writable=true;
        check(store.erase(thirty,removed,error,notice) && removed==30 && store.matching({"all",today,today})==1,
              "F10 thirty days inclusive keeps other scope and unsaved data");
        check(store.totals("all",today,today).down==177,"F10 unrelated unsaved totals committed exactly");
        check(store.erase({"",today.addDays(-35),today.addDays(-33)},removed,error,notice) && removed==9,"F10 custom cross-scope range deletes only three dates");
        check(store.erase({"all",{},{}},removed,error,notice) && removed==37,"F10 all dates for selected scope");
        check(store.erase({"",{},{}},removed,error,notice) && store.days.isEmpty() && store.newest==today,"F10 clear all histories keeps retention high-water");
        traffic::Rates r; r.inBytes=91; r.outBytes=17; r.seconds=.5;
        QDateTime wall(today,QTime(12,0),QTimeZone::systemTimeZone());
        store.add("physical","實體",r,wall,wall.addMSecs(500));
        check(store.totals("physical",today,today).down==91 && store.save(error),"F10 valid sampling resumes after deleting today");
        restarted.load(today); check(restarted.totals("physical",today,today).down==91,"F10 resumed samples persist without old totals");

        // Real modal controls, independent of the earlier verification window.
        const QString uiPath=dir.filePath("ui.json");
        {
            SpeedWindow w; w.timer.stop(); w.retentionTimer.stop(); w.setPaused(false);
            w.historyStore=history::Store(uiPath); w.historyStore.load(today);
            w.historyStore.addPart(today,"physical","所有實體網卡",9000000,3000000,1);
            w.historyStore.addPart(today.addDays(-10),"physical","所有實體網卡",20,10,1);
            w.historyStore.addPart(today,"{other}","其他網卡",30,10,1);
            check(w.historyStore.save(error),"F10 UI seed history");
            w.refreshDisplay(); w.resize(850,760); SpeedWindow::ensureVisible(&w); w.show();
            auto settings=displaySettings(); settings.sync(); const auto settingsBefore=bytes(settings.fileName());
            const auto session=w.session; const auto chart=w.chart->downloads; const auto records=w.records.size();
            const auto previous=w.previousTime; const bool baseline=w.hasPrevious;
            const auto oldJson=w.historyStore.encode(), oldDisk=bytes(uiPath),oldBackup=bytes(uiPath+".bak");
            auto *button=w.findChild<QPushButton *>("clearHistoryButton");
            check(button && button->text()!=w.findChild<QPushButton *>("resetButton")->text(),"F10 separate history and session actions");
            auto exercise=[&](bool accept,bool fail,bool all) {
                int stage=0; bool sawConfirm=false,sawResult=false;
                QTimer driver; driver.setInterval(20);
                QTimer watchdog; watchdog.setSingleShot(true);
                QObject::connect(&watchdog,&QTimer::timeout,&w,[&]{ check(false,"F10 modal test timeout"); driver.stop(); for(auto *widget:QApplication::topLevelWidgets()) if(auto *d=qobject_cast<QDialog *>(widget)) d->reject(); });
                QObject::connect(&driver,&QTimer::timeout,&w,[&] {
                    auto *modal=QApplication::activeModalWidget(); if(!modal) return;
                    if(modal->objectName()=="clearHistoryDialog") {
                        auto *d=qobject_cast<QDialog *>(modal);
                        if(stage==0) {
                            auto *scope=d->findChild<QComboBox *>("clearHistoryScope"); auto *range=d->findChild<QComboBox *>("clearHistoryRange");
                            check(d->findChild<QPushButton *>("clearHistoryCancel")->isDefault(),"F10 editor defaults to cancel");
                            scope->setCurrentIndex(scope->findData(all?QString():QString("physical")));
                            range->setCurrentIndex(2);
                            d->findChild<QDateEdit *>("clearHistoryFirst")->setDate(today);
                            d->findChild<QDateEdit *>("clearHistoryLast")->setDate(today.addDays(-1));
                            check(!d->findChild<QPushButton *>("clearHistoryApply")->isEnabled(),"F10 invalid date range disables delete");
                            d->findChild<QDateEdit *>("clearHistoryLast")->setDate(today);
                            if(all) range->setCurrentIndex(3);
                            check(d->findChild<QPushButton *>("clearHistoryExport")->isEnabled(),"F10 CSV available before deletion");
                            if(!captures.isEmpty() && !accept) d->grab().save(captures+"/f10-clear-range.png");
                            stage=1; QTimer::singleShot(0,d->findChild<QPushButton *>("clearHistoryApply"),&QPushButton::click);
                        } else if(stage>=2) { stage=4; driver.stop(); d->reject(); }
                    } else if(auto *box=qobject_cast<QMessageBox *>(modal)) {
                        if(stage==1 && box->objectName()=="clearHistoryConfirm") {
                            sawConfirm=true;
                            check(box->defaultButton()==box->button(QMessageBox::Cancel)
                                && box->escapeButton()==box->button(QMessageBox::Cancel)
                                && box->text().contains(QStringLiteral("不可復原")) && box->text().contains("CSV")
                                && box->text().contains(all?QStringLiteral("全部保留日期"):today.toString(Qt::ISODate)),"F10 concrete irreversible confirmation defaults and escapes to cancel");
                            if(!captures.isEmpty() && !accept) box->grab().save(captures+"/f10-confirm.png");
                            stage=2; box->button(accept?QMessageBox::Yes:QMessageBox::Cancel)->click();
                        } else if(stage==2) {
                            sawResult=true; check(box->windowTitle()==(fail?QStringLiteral("清除失敗"):QStringLiteral("清除完成")),"F10 UI reports actual persistence outcome");
                            stage=3; box->accept();
                        }
                    }
                });
                watchdog.start(10000); driver.start(); button->click(); driver.stop(); watchdog.stop();
                check(sawConfirm && (accept==sawResult) && stage==4,"F10 modal flow completed");
            };
            exercise(false,false,false);
            check(w.historyStore.encode()==oldJson && bytes(uiPath)==oldDisk && bytes(uiPath+".bak")==oldBackup,"F10 cancel preserves JSON memory and backup");
            HANDLE uiLock=CreateFileW(reinterpret_cast<LPCWSTR>(uiPath.utf16()),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
            check(uiLock!=INVALID_HANDLE_VALUE,"F10 UI persistence failure fixture");
            if(uiLock!=INVALID_HANDLE_VALUE) { exercise(true,true,false); CloseHandle(uiLock); }
            check(w.historyStore.encode()==oldJson && bytes(uiPath)==oldDisk,"F10 UI persistence failure leaves original data");
            exercise(true,false,false);
            check(w.historyStore.matching({"physical",today,today})==0 && w.historyStore.days.size()==2,"F10 UI selected date and scope deleted only matching row");
            check(w.historySummary->text().contains(QStringLiteral("今日 ↓ 0.000 MB")) && w.historySummary->text().contains(QStringLiteral("本月 ↓ 0.000 MB")),"F10 today and month summary refresh immediately");
            check(w.session.down==session.down && w.session.up==session.up && w.session.seconds==session.seconds
                  && w.chart->downloads==chart && w.records.size()==records && w.previousTime==previous && w.hasPrevious==baseline
                  && !w.paused && !w.timer.isActive(),"F10 clear leaves session realtime chart CSV sampling baseline and pause state intact");
            settings.sync(); check(bytes(settings.fileName())==settingsBefore,"F10 clearing writes no QSettings");
            // Drive an actual subsequent valid sample after deleting today's row.
            w.clearStatistics(); // Establish deterministic baseline before sampling.
            traffic::InterfaceInfo nic; nic.luid=987; nic.guid="{f10}"; nic.description="F10"; nic.up=true; nic.hardware=true;
            nic.inOctets=100; nic.outOctets=10;
            const auto t=std::chrono::steady_clock::now();
            w.processSample({nic},true,t,{},wall);
            nic.inOctets+=123; nic.outOctets+=45;
            w.processSample({nic},true,t+std::chrono::milliseconds(500),{},wall.addMSecs(500));
            check(w.historyStore.totals("physical",today,today).down==123 && w.session.down==123,"F10 real sampling resumes from current counters after today cleared");
            check(QMetaObject::invokeMethod(&w.retentionTimer,"timeout",Qt::DirectConnection),"F10 trigger periodic autosave");
            history::Store periodic(uiPath); periodic.load(today);
            check(periodic.totals("physical",today,today).down==123 && periodic.days.size()==3,"F10 autosave persists only new samples plus untouched records");
            exercise(true,false,true);
            check(w.historyStore.days.isEmpty(),"F10 UI clear all scopes all dates");
            w.historyStore.addPart(today,"physical","實體",8,4,.5);
            w.timer.start(500);
            int route=0; QTimer driver; driver.setInterval(20);
            QTimer watchdog; watchdog.setSingleShot(true);
            QObject::connect(&watchdog,&QTimer::timeout,&w,[&] { check(false,"F10 history route timeout"); driver.stop(); for(auto *widget:QApplication::topLevelWidgets()) if(auto *d=qobject_cast<QDialog *>(widget)) d->reject(); });
            QObject::connect(&driver,&QTimer::timeout,&w,[&] {
                auto *modal=QApplication::activeModalWidget(); if(!modal) return;
                check(w.timer.isActive() && !w.paused,"F10 dialogs keep real monitoring timer running");
                if(route==0) {
                    auto *clear=modal->findChild<QPushButton *>("historyClearButton"); if(!clear) return;
                    route=1; QTimer::singleShot(0,clear,&QPushButton::click);
                } else if(route==1 && modal->objectName()=="clearHistoryDialog") {
                    route=2; QTest::keyClick(modal,Qt::Key_Escape);
                } else if(route==2) {
                    check(modal->findChild<QWidget *>("historyPlot")!=nullptr,"F10 history chart remains open after cancelling clear");
                    route=3; driver.stop(); qobject_cast<QDialog *>(modal)->accept();
                }
            });
            watchdog.start(10000); driver.start(); w.showHistory(); driver.stop(); watchdog.stop(); w.timer.stop();
            check(route==3,"F10 history-window clear entry and Escape cancellation");
        }
        history::Store afterExit(uiPath); afterExit.load(today);
        check(afterExit.days.size()==1 && afterExit.totals("physical",today,today).down==8,"F10 normal window destruction saves only post-clear new data");
        QProcess normalExit;
        normalExit.start(QCoreApplication::applicationFilePath(),{"--f10-normal-exit",dir.path()});
        check(normalExit.waitForFinished(10000) && normalExit.exitCode()==0,"F10 application normal quit child exits successfully");
        history::Store childExit(dir.filePath("normal-exit.json")); childExit.load(today);
        check(childExit.days.size()==1 && childExit.totals("physical",today,today).down==19,"F10 aboutToQuit persists only post-deletion samples");
        std::cout << "F10: date/scope deletion, cancel, atomic failure, backup recovery, autosave, exit and resumed-sample checks completed\n";
    }
    static void verifyV13(SpeedWindow &w, const QString &captures) {
        w.timer.stop(); w.retentionTimer.stop(); w.clearStatistics();
        QTemporaryDir dir;
        const QString path=dir.filePath("history.json");
        history::Store store(path);
        const QDate date(2025,12,31);
        const QDateTime start(date,QTime(23,59,59,750),QTimeZone::UTC);
        traffic::Rates r; r.inBytes=1001; r.outBytes=201; r.seconds=.5;
        store.add("physical","實體",r,start,start.addMSecs(500));
        check(store.days.size()==2 && store.totals("physical",date,date).down==500
              && store.totals("physical",date.addDays(1),date.addDays(1)).down==501,"year midnight split conserves exact bytes");
        auto jan=store.totals("physical",QDate(2026,1,1),QDate(2026,1,31));
        check(jan.down==501 && jan.up==101 && jan.seconds==.25,"monthly and year rollover totals");
        const QDateTime month(QDate(2026,1,31),QTime(23,59,59,500),QTimeZone::UTC);
        r.seconds=1; r.inBytes=2000; r.outBytes=1000;
        store.anchor(month.date()); // synthetic month-long elapsed time in this fixture
        store.add("vpn","VPN",r,month,month.addSecs(1));
        check(store.totals("vpn",QDate(2026,2,1),QDate(2026,2,1)).down==1000
              && store.totals("physical",date,QDate(2026,2,1)).down==1001,"month rollover and scope isolation");
        const auto before=store.encode();
        store.add("vpn","VPN",r,month,month.addSecs(-10));
        store.add("vpn","VPN",r,month,month.addSecs(3600));
        check(store.encode()==before,"backward and forward clock jumps rejected without duplicate bytes");
        QString error; check(store.save(error),"atomic initial save and backup");
        history::Store loaded(path); loaded.load(QDate(2026,2,1));
        check(loaded.encode()==store.encode() && !loaded.dirty,"restart loads exact daily aggregates without recounting");
        QProcess crash;
        const QString crashPath=dir.filePath("crash.json");
        crash.start(QCoreApplication::applicationFilePath(),{"--history-crash",crashPath});
        check(crash.waitForFinished(10000) && crash.exitCode()==0,"abnormal exit fixture reaches saved checkpoint");
        history::Store checkpoint(crashPath); checkpoint.load();
        const auto recoveredCheckpoint=checkpoint.totals("physical",QDate::currentDate(),QDate::currentDate());
        check(recoveredCheckpoint.down==50,"abnormal exit restores only committed totals without duplicate unsaved bytes");
        r.inBytes=9007199254740993ULL; r.outBytes=9007199254740995ULL;
        store.add("=danger,\"scope\"","大數",r,month,month.addSecs(1));
        check(store.save(error),"second save rotates last valid backup");
        history::Store exact(path); exact.load(QDate(2026,2,1));
        check(exact.totals("=danger,\"scope\"",QDate(2026,1,1),QDate(2026,2,1)).down==9007199254740993ULL,"JSON preserves integers beyond 2^53");
        auto csv=exact.csvData("",0,QDate(2026,2,1));
        check(csv.startsWith(QByteArray::fromHex("efbbbf")) && csv.contains("\"'=danger,\"\"scope\"\"\"")
              && csv.contains("4503599627370496") && csv.contains("\r\n"),"history CSV BOM CRLF integer bytes and formula escaping");
        auto only7=exact.csvData("physical",7,QDate(2026,2,1));
        check(!only7.contains("2025-12-31") && !only7.contains("vpn"),"CSV selected dates and scope filter");
        QFile f(path); check(f.open(QIODevice::WriteOnly),"open corrupt fixture"); f.write("{broken"); f.close();
        history::Store recovered(path); recovered.load(QDate(2026,2,1));
        check(recovered.days.size()==4 && !recovered.warning.isEmpty()
              && QDir(dir.path()).entryList({"*.corrupt-*"},QDir::Files).size()==1,"corrupt main preserved and last valid backup recovered");
        check(recovered.save(error),"recovered state can be safely saved");
        QFile backup(path+".bak"); check(backup.open(QIODevice::WriteOnly),"open backup fixture"); backup.write("invalid"); backup.close();
        check(f.open(QIODevice::WriteOnly),"reopen main fixture"); f.write("invalid"); f.close();
        history::Store empty(path); empty.load(QDate(2026,2,1));
        check(empty.days.empty() && !empty.warning.isEmpty()
              && QDir(dir.path()).entryList({"*.corrupt-*"},QDir::Files).size()==3,"both damaged files preserved and startup recovers empty");
        check(empty.save(error),"blank recovery state saves atomically");
        history::Store retention(dir.filePath("retention.json"));
        for(int i=0;i<400;++i) { auto a=month.addDays(i); retention.anchor(a.date()); retention.add("p","p",r,a,a.addSecs(1)); }
        check(retention.days.size()==365,"history retention limited to 365 calendar days");
        const auto original=retention.encode(); retention.path=dir.filePath("block/child.json");
        QFile blocker(dir.filePath("block")); check(blocker.open(QIODevice::WriteOnly),"create write failure fixture"); blocker.close();
        check(!retention.save(error) && retention.dirty && retention.encode()==original,"save failure retains in-memory totals for retry");
        check(!history::Store::atomic(path+"/invalid",csv,error),"history CSV write failure reported");
        check(history::csv("\t@formula").startsWith("\"'"),"history CSV control characters neutralized");
        history::Notifications n;
        check(n.observe(1)==-1 && n.observe(0)==-1 && n.observe(1)==-1,"notification initial baseline and transient disconnect silent");
        check(n.observe(0)==-1 && n.observe(0)==-1 && n.observe(0)==0 && n.observe(0)==-1,"disconnect debounce and duplicate suppression");
        check(n.observe(2)==-1 && n.observe(2)==-1 && n.observe(2)==2,"read failure notification once after debounce");
        check(n.observe(1)==-1 && n.observe(1)==-1 && n.observe(1)==1,"recovery notification debounced");
        n.reset(); check(n.observe(0)==-1,"pause and scope reset remain silent");
        QSettings registry(QStringLiteral("HKEY_CURRENT_USER\\Software\\RealtimeNetworkSpeedTest-")+QUuid::createUuid().toString(QUuid::Id128),QSettings::NativeFormat);
        const QString executable=QCoreApplication::applicationFilePath();
        check(setStartup(registry,true,executable,error) && registry.value("RealtimeNetworkSpeed").toString()==startupCommand(executable),"HKCU startup quoted absolute executable and startup flag roundtrip");
        check(setStartup(registry,false,executable,error) && !registry.contains("RealtimeNetworkSpeed"),"HKCU startup removable"); registry.clear(); registry.sync();
        check(startupCommand("relative.exe").isEmpty() && startupCommand("D:/a\"b.exe").isEmpty()
              && startupCommand("D:/含 空白/app.exe")==QStringLiteral("\"D:\\含 空白\\app.exe\" --startup"),"startup path rejects relative and quoted injection; handles spaces unicode");
        check(!setStartup(registry,true,dir.filePath("missing.exe"),error),"missing startup executable rejected");
        w.chart->clear(); const auto t=std::chrono::steady_clock::now();
        for(int i=0;i<4000;++i) w.chart->append(i==3990?NAN:8000,1000,t+std::chrono::milliseconds(i*500));
        check(w.chart->longSamples.size()==3600 && w.chart->downloads.size()==120,"all chart buffers bounded independently");
        w.chart->setWindowSeconds(300); const auto five=w.chart->plotted();
        check(five.size()<=152 && std::any_of(five.begin(),five.end(),[](auto p){return !std::isfinite(p.down);}),"five minute aggregation preserves gap");
        w.chart->setWindowSeconds(1800); const auto thirty=w.chart->plotted();
        check(thirty.size()<=182 && std::any_of(thirty.begin(),thirty.end(),[](auto p){return !std::isfinite(p.down);}),"thirty minute aggregation bounded and gap preserved");
        const auto size=w.chart->longSamples.size(); w.setPaused(true); w.sample();
        check(w.chart->longSamples.size()==size,"paused chart frozen");
        auto settings=displaySettings(); settings.setValue("mini/opacity",30); settings.setValue("mini/font",2);
        settings.setValue("mini/showAdapter",true); settings.setValue("mini/showTotals",true); settings.sync();
        w.applyMiniSettings(); w.refreshDisplay();
        check(w.mini->windowOpacity()>=.49 && w.mini->windowOpacity()<=.51 && w.miniValue->font().pointSize()==15
              && w.miniValue->text().contains(QStringLiteral("本次")),"mini opacity clamped and large font optional totals applied");
        QDir settingsRoot(QFileInfo(displaySettings().fileName()).absolutePath()); settingsRoot.cdUp();
        QProcess miniChild; miniChild.start(QCoreApplication::applicationFilePath(),{"--check-mini",settingsRoot.path()});
        check(miniChild.waitForFinished(10000) && miniChild.exitCode()==0,"mini preferences persist across process restart");
        w.historyStore.dirty=true;
        check(w.retentionTimer.interval()==60000,"periodic history save configured for sixty seconds");
        QMetaObject::invokeMethod(&w.retentionTimer,"timeout",Qt::DirectConnection);
        check(!w.historyStore.dirty && w.historySaveError.isEmpty(),"periodic timeout commits history even while paused");
        settings.setValue("mini/opacity",100); settings.setValue("mini/font",1); settings.sync(); w.applyMiniSettings();
        // Integration: deterministic clocks, no real adapter changes or notifications needed.
        w.clearStatistics(); w.paused=false; w.notificationState.reset();
        traffic::InterfaceInfo nic; nic.luid=888; nic.guid="{history-test}"; nic.description="history-test"; nic.up=true; nic.hardware=true;
        w.savedAdapter="physical"; w.updateAdapterList({nic});
        const QDateTime wall(QDate::currentDate(),QTime(12,0),QTimeZone::systemTimeZone());
        w.processSample({nic},true,t,{},wall); nic.inOctets=100;
        w.processSample({nic},true,t+std::chrono::milliseconds(500),{},wall.addMSecs(500));
        const auto hist=w.historyStore.totals("physical",wall.date(),wall.date());
        check(w.session.down==100 && hist.down>=100,"valid sample feeds session and historical bytes");
        nic.inOctets=1000000;
        w.processSample({nic},true,t+std::chrono::seconds(1),{},wall.addSecs(3600));
        check(w.session.down==100,"wall clock jump rebaselines counters without spike");
        nic.inOctets+=50;
        w.processSample({nic},true,t+std::chrono::milliseconds(1500),{},wall.addSecs(3600).addMSecs(500));
        check(w.session.down==150,"sampling resumes after clock rebaseline");
        const auto persisted=w.historyStore.encode(); w.clearStatistics();
        check(w.historyStore.encode()==persisted,"session reset preserves history");
        w.setPaused(true);
        if(!captures.isEmpty()) {
            w.findChild<QComboBox *>("chartInterval")->setCurrentIndex(2);
            w.resize(850,760); SpeedWindow::ensureVisible(&w); w.show(); w.mini->show(); QTest::qWait(80);
            auto *scroll=w.findChild<QScrollArea *>("contentScroll"); scroll->verticalScrollBar()->setValue(0); QTest::qWait(30);
            w.grab().save(captures+"/v130-main.png"); w.mini->grab().save(captures+"/v130-mini.png");
            scroll->verticalScrollBar()->setValue(scroll->verticalScrollBar()->maximum()); QTest::qWait(30); w.grab().save(captures+"/v130-main-bottom.png");
            QTimer::singleShot(100,&w,[&]{ if(auto *dialog=qobject_cast<QDialog*>(QApplication::activeModalWidget())) { dialog->grab().save(captures+"/v130-history.png"); dialog->accept(); } }); w.showHistory();
            QTimer::singleShot(100,&w,[&]{ if(auto *dialog=qobject_cast<QDialog*>(QApplication::activeModalWidget())) { dialog->grab().save(captures+"/v130-mini-options.png"); dialog->accept(); } }); w.showMiniOptions();
        }
        w.mini->hide(); w.chart->setWindowSeconds(60);
    }

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
        const auto wall=QDateTime::currentDateTime();
        auto sample = [&](int ms, std::uint64_t down, std::uint64_t up) {
            nic.inOctets = down; nic.outOctets = up;
            w.processSample({nic}, true, t + std::chrono::milliseconds(ms), {}, wall.addMSecs(ms));
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
        w.processSample({}, true, t + std::chrono::milliseconds(3500), {}, wall.addMSecs(3500));
        check(w.adapterSelector->currentData().toString() == "{vpn-test}" && !w.sampleValid,
              "adapter unplug keeps selection and marks unavailable");
        sample(4000, 19000000000, 19000000000);
        check(w.records.size() == count && w.session.down == 2600, "reconnect baseline prevents spike");
        sample(4500, 19000000200, 19000000100);
        check(w.session.down == 2800, "reconnect subsequent delta counted");
        w.processSample({}, false, t + std::chrono::milliseconds(5000), "test read failure", wall.addMSecs(5000));
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
        check(history::csv("=1+1") == QStringLiteral("\"'=1+1\"") && history::csv("  @SUM(1)").contains(QChar(0x27)),
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
        w.unexportedRecords = false;
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

    static void verifyV121(SpeedWindow &w, const QString &captureDir) {
        w.timer.stop(); w.clearStatistics(); w.showNormal();
        auto *scroll = w.findChild<QScrollArea *>("contentScroll");
        for (const QSize size : {QSize(850, 560), QSize(640, 420), QSize(420, 300)}) {
            w.resize(size); QTest::qWait(80);
            check(scroll && scroll->verticalScrollBar()->maximum() > 0, "small window content scrolls");
            for (auto *button : {w.pauseButton, w.exportButton, w.findChild<QPushButton *>("resetButton")}) {
                const QRect rect(button->mapTo(&w, QPoint()), button->size());
                check(w.rect().contains(rect) && button->isVisible(), "all footer actions remain in viewport");
            }
            scroll->verticalScrollBar()->setValue(scroll->verticalScrollBar()->maximum());
            check(scroll->widget()->findChild<QLabel *>() != nullptr, "scroll content retained");
            if (!captureDir.isEmpty()) w.grab().save(captureDir + QString("/v121-%1x%2.png").arg(size.width()).arg(size.height()));
        }
        check(history::csv("\tname").startsWith("\"'") && history::csv("\r=1").startsWith("\"'")
              && history::csv("\n+1").startsWith("\"'") && history::csv(" \t@x").startsWith("\"'")
              && history::csv("ordinary") == "\"ordinary\"", "CSV controls and formulas escaped without changing ordinary text");
        const auto now = std::chrono::steady_clock::now();
        w.session.down = 987654321; w.session.seconds = 90000;
        w.records.push_back({"old", "adapter", "id", {}, w.session, now - std::chrono::hours(24)});
        w.records.push_back({"recent", "adapter", "id", {}, w.session, now});
        w.unexportedRecords = true;
        w.pruneRecords(now);
        check(w.records.size() == 1 && w.records.front().timestamp == "recent" && w.discardedRecords == 1,
              "24h retention expires oldest record at exact boundary");
        check(w.session.down == 987654321 && w.session.seconds == 90000 && w.unexportedRecords,
              "retention preserves session totals and unsaved state");
        while (w.records.size() <= w.recordLimit) w.records.push_back({"recent", "adapter", "id", {}, w.session, now});
        w.pruneRecords(now);
        check(w.records.size() == w.recordLimit && w.discardedRecords == 2, "record cap enforced independently of elapsed time");
        w.records.erase(w.records.begin() + 1, w.records.end());
        QString error;
        QTemporaryDir csvDir;
        const QString path = csvDir.filePath("retained.csv");
        check(writeCsv(path, w.records, error, w.discardedRecords), "retained range export succeeds");
        QFile file(path); check(file.open(QIODevice::ReadOnly), "retention export readable");
        const auto bytes = file.readAll();
        check(bytes.contains("Retained_From,Retained_To,Retention_Hours,Retention_Max_Records,Discarded_Records")
              && bytes.contains("987654321") && bytes.contains("24,172800,2"), "CSV distinguishes cumulative totals and retained range");
        const auto oldKey = w.savedAdapter;
        const auto oldTotal = w.session.down;
        const int target = w.adapterSelector->currentIndex() == 0 ? w.adapterSelector->count()-1 : 0;
        auto answer = [](bool discard) {
            QTimer::singleShot(50, qApp, [discard] {
                auto *box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget());
                check(box && box->defaultButton() && box->defaultButton()->text() == QStringLiteral("取消"), "discard prompt defaults to cancel");
                if (box) {
                    if (discard) { for (auto *b : box->buttons()) if (box->buttonRole(b) == QMessageBox::DestructiveRole) { b->click(); return; } }
                    box->defaultButton()->click();
                }
            });
        };
        answer(false); w.adapterSelector->setCurrentIndex(target);
        check(w.savedAdapter == oldKey && w.adapterSelector->currentData().toString() == oldKey
              && w.session.down == oldTotal && w.records.size() == 1, "cancel adapter change preserves old selection totals records");
        answer(false); w.findChild<QPushButton *>("resetButton")->click();
        check(w.session.down == oldTotal && w.unexportedRecords, "cancel reset preserves data");
        // Exercise save-before-discard with controlled Qt dialogs and real atomic writes.
        const bool wasNativeDisabled = QCoreApplication::testAttribute(Qt::AA_DontUseNativeDialogs);
        QCoreApplication::setAttribute(Qt::AA_DontUseNativeDialogs, true);
        auto saveBefore = [&](bool cancelSave) {
            QTimer dialogDriver;
            QObject::connect(&dialogDriver, &QTimer::timeout, &w, [&] {
                QWidget *active = QApplication::activeModalWidget();
                if (auto *fileDialog = qobject_cast<QFileDialog *>(active)) {
                    check(!w.timer.isActive() && w.savedAdapter == oldKey, "save dialog holds old scope and sampling");
                    if (cancelSave) fileDialog->reject();
                    else { fileDialog->selectFile(csvDir.filePath("saved-before-reset.csv"));
                           QMetaObject::invokeMethod(fileDialog, "accept", Qt::DirectConnection); }
                } else if (auto *box = qobject_cast<QMessageBox *>(active)) {
                    if (box->windowTitle() == QStringLiteral("匯出完成")) box->accept();
                    else for (auto *b : box->buttons())
                        if (box->buttonRole(b) == QMessageBox::AcceptRole) { b->click(); break; }
                }
            });
            dialogDriver.start(40);
            w.adapterSelector->setCurrentIndex(target);
        };
        saveBefore(true);
        check(w.savedAdapter == oldKey && w.session.down == oldTotal && w.unexportedRecords,
              "cancel save-before-switch cancels switch and retains unsaved data");
        saveBefore(false);
        const auto exportedFiles = QDir(csvDir.path()).entryList({"*.csv"}, QDir::Files);
        check(w.records.empty() && w.session.down == 0 && w.savedAdapter != oldKey
              && exportedFiles.size() == 2, "successful save-before-switch exports then switches");
        for (const auto &name : exportedFiles) if (name != "retained.csv") {
            QFile saved(csvDir.filePath(name));
            check(saved.open(QIODevice::ReadOnly) && saved.readAll().contains("987654321"), "save-before-switch preserves original cumulative data in CSV");
        }
        QCoreApplication::setAttribute(Qt::AA_DontUseNativeDialogs, wasNativeDisabled);
        // Recreate unsaved data for explicit discard branch.
        w.adapterSelector->setCurrentIndex(w.adapterSelector->findData(oldKey));
        w.records.push_back({"recent", "adapter", "id", {}, {}, now}); w.unexportedRecords = true;
        answer(true); w.adapterSelector->setCurrentIndex(target);
        check(w.session.down == 0 && w.records.empty() && !w.unexportedRecords && w.savedAdapter != oldKey,
              "explicit discard switches adapter and resets statistics");
        w.records.push_back({"test", "adapter", "id", {}, {}, now}); w.unexportedRecords = true;
        answer(true); w.findChild<QPushButton *>("resetButton")->click();
        check(w.records.empty() && !w.unexportedRecords, "explicit reset discards after confirmation");
        w.hide(); emit w.tray->activated(QSystemTrayIcon::Trigger); QTest::qWait(80);
        check(nativeVisible(&w), "single tray click restores native window");
        w.resize(740, 510); w.move(90, 90); w.mini->resize(310, 170); w.mini->move(130, 130);
        SpeedWindow::ensureVisible(&w); SpeedWindow::ensureVisible(w.mini);
        w.saveWindows();
        const auto mainGeometry = w.geometry(), miniGeometry = w.mini->geometry();
        const QString expectedPath = csvDir.filePath("geometry.ini");
        { QSettings expected(expectedPath, QSettings::IniFormat);
          expected.setValue("main", mainGeometry); expected.setValue("mini", miniGeometry); expected.sync(); }
        QProcess geometryChild;
        geometryChild.start(QCoreApplication::applicationFilePath(), {"--check-geometry", QFileInfo(displaySettings().fileName()).absolutePath() + "/..", expectedPath});
        check(geometryChild.waitForFinished(10000) && geometryChild.exitCode() == 0, "geometry restored in fresh process");
        if (geometryChild.exitCode() != 0) std::cerr << geometryChild.readAllStandardError().constData();
        w.resize(850, 700); w.move(200, 200); w.mini->move(250, 250); w.restoreWindows();
        check(w.geometry() == mainGeometry && w.mini->geometry() == miniGeometry, "main and mini geometry restored");
        w.move(-30000, -30000); w.mini->move(30000, 30000);
        SpeedWindow::ensureVisible(&w); SpeedWindow::ensureVisible(w.mini);
        check(QGuiApplication::primaryScreen()->availableGeometry().contains(w.frameGeometry())
              && QGuiApplication::primaryScreen()->availableGeometry().contains(w.mini->frameGeometry()),
              "offscreen windows clamped to available desktop");
        QTemporaryDir instanceDir;
        const QString identity = instanceDir.filePath("instance.ini");
        SingleInstance owner(identity);
        check(owner.start() == 1, "single instance lock acquired");
        owner.onActivation(w); w.hide();
        QProcess child;
        child.start(QCoreApplication::applicationFilePath(), {"--check-instance", identity});
        QElapsedTimer deadline; deadline.start();
        while (child.state() != QProcess::NotRunning && deadline.elapsed() < 6000) QTest::qWait(20);
        check(child.state() == QProcess::NotRunning && child.exitCode() == 0 && nativeVisible(&w),
              "second process exits and wakes existing window");
        if (child.state() != QProcess::NotRunning) { child.kill(); child.waitForFinished(); }
        w.clearStatistics(); w.adapterSelector->setCurrentIndex(0);
    }

    static int interactive(QApplication &app) {
        SpeedWindow w;
        w.show();
        QTimer::singleShot(180000, &app, &QCoreApplication::quit);
        return app.exec();
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
    static int geometryRestored(const QString &expectedPath) {
        QSettings expected(expectedPath, QSettings::IniFormat);
        SpeedWindow w;
        check(w.geometry() == expected.value("main").toRect() && w.mini->geometry() == expected.value("mini").toRect(),
              "main and mini geometry persist across process restart");
        return failures ? 1 : 0;
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
        check(!window.sampleValid && window.downloadValue->text() == QStringLiteral("—"), "startup shows unavailable until first valid rate");
        check(window.mini->width() < 500 && window.mini->height() < 300, "new mini window starts compact");
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
        window.unexportedRecords = false;
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
        verifyV121(window, captureDir);
        verifyV13(window, captureDir);
        verifyF10(captureDir);
        verifyAudit(window,captureDir);
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
    if(app.arguments().contains("--measure-csv")) return Verification::measureCsv(app.arguments().contains("--independent-strings"));
    if(app.arguments().size()==3 && app.arguments()[1]=="--f10-normal-exit") {
        QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,app.arguments()[2]);
        return Verification::f10NormalExit(app,app.arguments()[2]);
    }
    if(app.arguments().size()==3 && app.arguments()[1]=="--history-crash") {
        history::Store store(app.arguments()[2]);
        auto wall=QDateTime(QDate::currentDate(),QTime(12,0),QTimeZone::systemTimeZone());
        traffic::Rates r; r.inBytes=50; r.seconds=.5;
        store.add("physical","Physical",r,wall,wall.addMSecs(500));
        QString error; if(!store.save(error)) return 2;
        store.add("physical","Physical",r,wall.addMSecs(500),wall.addMSecs(1000));
        std::_Exit(0);
    }
    if(app.arguments().size()==3 && app.arguments()[1]=="--check-mini") {
        QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,app.arguments()[2]);
        return Verification::miniRestored();
    }
    if (app.arguments().size() == 3 && app.arguments()[1] == "--check-instance") {
        SingleInstance instance(app.arguments()[2]); return instance.start() == 0 ? 0 : 1;
    }
    if (app.arguments().size() == 4 && app.arguments()[1] == "--check-geometry") {
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, app.arguments()[2]);
        return Verification::geometryRestored(app.arguments()[3]);
    }
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
    if (app.arguments().size()==3 && app.arguments()[1]=="--packaged-smoke") {
        SpeedWindow w; w.show();
        check(displaySettings().fileName().startsWith(settings.path()),"packaged smoke uses isolated settings");
        QTimer::singleShot(600,&app,[&] {
            check(w.isVisible(),"packaged UI launches with deployed dependencies");
            check(w.grab().save(app.arguments()[2]),"packaged UI screenshot");
            app.exit(failures?1:0);
        });
        return app.exec();
    }
    if (app.arguments().contains("--interactive-smoke")) return Verification::interactive(app);
    const QString captures = app.arguments().size() > 1 ? app.arguments()[1] : qEnvironmentVariable("RNS_TEST_CAPTURES");
    if (!captures.isEmpty()) QDir().mkpath(captures);
    Verification::run(captures, settings.path());
    std::cout << (failures ? "FAILED" : "PASS") << ": conversion, auto thresholds, UI synchronization, history, live timer, process restart preferences, adapter selection\n";
    return failures ? 1 : 0;
}
