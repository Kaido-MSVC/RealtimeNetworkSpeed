# 即時網路速度 v1.2.1 本機修正與驗證

日期：2026-11-06（Asia/Taipei）。狀態：本機開發階段記錄；使用者已於 2026-11-06 驗收通過並授權正式發布。正式發布驗證見 RELEASE-v1.2.1.md。

## 起始狀態與版本決策

- 專案：`D:\projects\網速工具`，分支 `main`。
- 修改前工作目錄乾淨；HEAD 與既有 `v1.2.0` tag 指向 `3e2ca4bdae9d69cde82129f8e2f2868b5f14827a`。
- GitHub 最新公開 Release 已透過唯讀查詢確認為 v1.2.0（2026-11-05 發布）。本次版本設為 v1.2.1，但不建立 tag、commit、push 或 Release。
- C++17；Qt 6.12.0 官方 MSVC 2022 x64；Widgets／Network；Visual Studio 18 2026、MSVC 19.51.36260.0、SDK 10.0.28000.0、CMake 4.4.4；Release x64 建置。

## 完成修正

| 項目 | 最終行為 |
| --- | --- |
| 小螢幕／高 DPI | 主內容放入 QScrollArea；最低 420×300 邏輯像素；底部暫停、清空、匯出按鈕獨立固定。窄視窗可水平捲動內容；圖表最低高度 180。 |
| 逐筆保留 | deque 最近 24 小時、最多 172800 筆。監控時每次取樣清理、暫停時每分鐘清理、匯出前清理。以 monotonic clock 判斷經過時間，系統時間調整不會破壞淘汰順序。 |
| 累積統計 | 淘汰逐筆不修改本次累積流量、有效秒數、平均或峰值。 |
| CSV | 不再複製整份 records。儲存對話框期間暫停取樣，結束後重建基準，不補算該段流量；仍以 QSaveFile 原子寫入。 |
| CSV 範圍 | 保留原有 13 欄，追加 Retained_From、Retained_To、Retention_Hours、Retention_Max_Records、Discarded_Records；畫面及完成訊息明示範圍、筆數、淘汰數。累積欄仍代表本次全部有效取樣。 |
| 避免誤清 | 網卡切換及清空有未匯出紀錄時，提供先匯出／捨棄／取消，預設與 Escape 為取消。取消存檔或失敗會取消原操作。確認期間保持舊網卡範圍。 |
| 單一執行個體 | 使用設定路徑對應的專用 `.instance.lock`，與 QSettings 自己的鎖分開；同使用者本機 named pipe 喚醒原視窗，回覆確認後第二份退出。異常時不再開第二個監控視窗。 |
| 視窗操作 | 系統匣單擊／雙擊還原；主、迷你視窗位置及大小存入 QSettings，恢復時校正可用螢幕範圍；保留獨立置頂迷你視窗。 |
| 版本資源 | 1.2.1，補齊 FileDescription、CompanyName、LegalCopyright、OriginalFilename；UTF-8、繁體中文語系。 |
| 腳本 | PowerShell 預設不等待輸入；`-NoPause`／`-Pause`；`.cmd` 才傳 `-Pause`；`-BuildOnly` 只建置與測試，完全跳過打包。 |
| 其他 | sampleValid 初始 false；CSV 對 Tab、CR、LF 等 ASCII 控制字元及公式開頭加前置單引號。 |

## 驗證結果

- Qt 6.12.0／MSVC x64 Release 建置成功。
- CTest **2/2 通過**：`traffic-logic` 與 `units-and-ui`；最終記錄另存 `validation.txt`。
- 新增／必要回歸涵蓋：24 小時邊界、筆數上限、累積不受淘汰影響、CSV 保留欄位及原子寫入失敗保護、Tab／CR／LF 防公式、取消切換、取消清空、先匯出後切換、取消存檔保留資料、明確捨棄、跨程序喚醒、主／迷你位置跨程序還原、離屏校正、系統匣單擊還原，以及啟動速度顯示。
- 使用 Windows Qt 平台完成一般縮放與 `QT_SCALE_FACTOR=1.5` UI 回歸。測試視窗大小包括 850×560、640×420、420×300 邏輯像素；底部三按鈕均在視窗內。已檢視保存的實際畫面。
- Windows 原生另存對話框已實際操作成功；CSV 寫入輸出目錄，讀回確認 UTF-8 BOM、67 筆、18 欄及保留範圍。完成後取樣從 67 筆繼續至 76 筆，確認恢復更新。
- PowerShell `-NonInteractive -NoPause -BuildOnly` 正常結束，沒有 Read-Host 阻塞。
- 以 Windows FileVersionInfo 讀回全部新增版本欄位成功。
- Git 格式檢查通過（既有 PowerShell／CMD 使用 CRLF，檢查採 `cr-at-eol`）；C++／資源／README 保留原有 LF。
- 測試使用暫存設定路徑，未修改使用者真正的 INI。測試視窗已退出。

## 本機開發完成時的檔案與發布狀態（歷史記錄）

修改：`main.cpp`、`tests/verification.cpp`、`version.h`、`app.rc`、`build-and-test.ps1`、`build-and-test.cmd`、`README.md`。

新增：`DEVELOPMENT-v1.2.1.md`（本記錄）。變更全部保留於本機工作目錄，尚未暫存或提交。

`build\` 的 Release 產物與 `build-log.txt` 已更新，均為原本忽略的建置資料。`dist\` 保持 v1.2.0，`archives\` 舊檔案未動；Qt Network、`.pro` 與授權條文未改。HEAD 及 v1.2.0 tag 未改寫。沒有推送、建 tag 或建立 Release。

## 本機開發階段的驗收方式與限制（歷史記錄）

先退出舊 v1.2.0，再執行 `D:\projects\網速工具\run-local.cmd`，會啟動 `build\Release\RealtimeNetworkSpeed.exe`（使用本機 Qt 6.12.0）。不要用仍保留舊版的 dist EXE 驗收。

本次沒有重新製作可攜發布包或測試乾淨 PATH；沒有 Windows 10／乾淨電腦、實體混合 DPI 多螢幕及真實連續 24 小時測試。保留期限以可控制時間及筆數測試驗證，150% 為 Qt 縮放測試，未變更作業系統顯示設定。這些是本次測試範圍限制。

逐筆資料仍只存在記憶體，結束程式會清除；逾保留上限的資料不可補匯出。匯出或清除確認期間不取樣，暫停時間不計入累積；此行為已寫入 README。

## 文件參考

已查詢 Context7 的 Qt 6 文件，並以本機 Qt 6.12.0 官方文件及實際編譯驗證相關 API：QScrollArea、QWidget geometry、QLockFile 長期鎖與 QLocalServer／QLocalSocket。

- https://doc.qt.io/qt-6/qscrollarea.html
- https://doc.qt.io/qt-6/restoring-geometry.html
- https://doc.qt.io/qt-6/qlockfile.html
- https://doc.qt.io/qt-6/qlocalserver.html
- https://doc.qt.io/qt-6/qlocalsocket.html
