# 網速工具 v1.2.0 本機開發與驗證紀錄

日期：2026-11-05（Asia/Taipei）
專案：`D:\projects\網速工具`
基礎版本：v1.1.2；本機開發版本：v1.2.0。

已直接修改原專案並沿用原 `build` 目錄，未建立專案副本、未執行清理或重建 dist、未修改既有 archives，未提交、推送 GitHub 或建立 Release。

## 完成內容

| 功能 | 行為 |
| --- | --- |
| 指定網卡 | 實體、VPN、虛擬介面均可單獨選擇；排除 loopback 與 NDIS 篩選介面；GUID 偏好沿用原設定。網卡消失保留選擇及統計，不切換至其他網卡；恢復後先建立基準。 |
| 網卡資訊 | 顯示名稱、GUID、IPv4／IPv6、MAC、啟用／連線狀態、接收／傳送連線速度；可捲動與複製。查詢資訊不推進流量取樣基準。 |
| 平均與峰值 | 下載及上傳分別統計。平均為本次累積 bytes × 8 ÷ 有效取樣秒數；峰值為單次有效取樣最大 bit/s。正常零流量取樣納入平均時間。 |
| 滑鼠懸停 | 主視窗下載／上傳數值及迷你視窗數值可顯示狀態、累積、平均與峰值。 |
| 暫停／恢復 | 暫停凍結速度、圖表、累積、平均、峰值與待匯出紀錄；迷你視窗標題標示暫停。恢復重新讀取基準，不補算暫停流量。 |
| 清空本次統計 | 同時清除累積、平均、峰值、圖表、取樣基準及本次 CSV 紀錄，保留設定；暫停時清空仍維持暫停。 |
| 手動 CSV | UTF-8 BOM、CRLF；輸出按下匯出時的記憶體快照。速度固定 bytes/s，累積固定 bytes；整數累積不經浮點轉換。含網卡 GUID、有效秒數、平均與峰值，時間附 +08:00。 |

手動切換監看範圍會開始新的工作階段，清除原範圍紀錄；請先匯出需要保留的資料。斷線、首次取樣、讀取失敗、超過 2 秒的取樣空檔及計數器倒退不補算，無有效速度時顯示「—」並在圖表留下空檔。

保留原有系統匣還原／結束、關閉時縮至系統匣、主視窗最小化隱藏工作列項目、獨立置頂迷你視窗，以及單位與視窗設定。自動單位仍採目前速度及最近 60 秒圖表峰值。

## 主要變更檔案

- `main.cpp`：網卡資訊、控制項、統計顯示、懸停、CSV 序列化及 QSaveFile 原子儲存。
- `traffic.h`：VPN／虛擬介面選擇、有效取樣時間、加權平均與峰值。
- `tests/traffic_tests.cpp`、`tests/verification.cpp`：流量與 Windows UI 回歸驗證。
- `CMakeLists.txt`、`RealtimeNetworkSpeed.pro`：加入 Qt Network，僅用於讀取本機介面 IP 資訊。
- `build-and-test.ps1`：後續打包時保留新增依賴 Qt6Network.dll；本次未執行打包腳本。
- `version.h`：版本與 Windows 資源版本更新為 1.2.0。
- `README.md`、`licenses/`：功能規則與新增模組的第三方聲明；授權全文取自 D:\licenses，保留 Qt 本機原始碼中的原作者聲明，未修改授權範本。
- `run-local.cmd`：使用本機 Qt 環境啟動原 build/Release 執行檔。

## 實際驗證結果

環境：C++17、Qt 6.12.0 官方 MSVC x64 套件、Visual Studio 18 2026、Windows SDK 10.0.28000.0、Windows 11 x64。

1. 原專案 CMake 配置、MSVC x64 Release 編譯通過；應用程式及兩個驗證執行檔均已產生。Windows 檔案版本及產品版本均為 1.2.0，PE 機器碼為 AMD64。
2. `RealtimeNetworkSpeedTrafficTests.exe` 通過：介面篩選、VPN 加總、單一網卡、計數器倒退、新介面／消失、休眠空檔、累積及圖表刻度。
3. `RealtimeNetworkSpeedVerification.exe` 最終版本通過：真實 Windows 計數器取樣、單位同步、設定重啟還原、系統匣、獨立迷你視窗、原生視窗樣式與最小化回歸；另以可控制的取樣資料驗證重連、讀取失敗、暫停／恢復、清空、加權平均、峰值及 CSV。
4. 同一驗證程式開啟真實網卡資訊視窗並自動關閉；已檢查實體網卡 IPv4／IPv6、MAC 與連線速度可顯示，以及查詢不改動取樣基準。
5. CSV 由獨立 Python CSV 解析器讀回：BOM、13 欄、7 筆有效取樣、繁體中文、逗號、雙引號、多行名稱、bytes/s 換算與精確累積皆通過；範例最後累積下載 3300 bytes、上傳 950 bytes。時間偏移 +08:00 驗證通過。
6. CSV 公式樣式名稱中和、無效儲存路徑失敗回報與既有檔案保持不變的驗證通過。使用 QSaveFile 的暫存檔／commit 儲存流程，未啟用直接覆寫 fallback。
7. 已檢視預設及最小 760×720 尺寸的主視窗、迷你視窗及網卡資訊截圖；圖表與操作按鈕無裁切。輸出的主視窗預覽使用模擬測試數值，並非目前實際網速。
8. Git 差異檢查通過；保留原有換行及 BOM 格式，未變更既有視窗生命週期程式碼。

## 未驗證事項與實際限制

- 未實際中斷使用者網路、拔插網卡或登入／切換 VPN；這些狀態轉移採模擬取樣驗證，不能視為各家 VPN 驅動的實機驗收。
- 未在 Windows 10、乾淨電腦、不同 DPI／多螢幕環境驗證；未進行長時間壓力測試。
- 未另外操作 Windows 原生儲存對話框完整流程或使用 Excel 開啟 CSV；已驗證共用 CSV 寫入函式及獨立解析結果。
- 本次不產生新版可攜套件；既有 dist 與 archives 仍為原版本，請用本機啟動入口執行 build/Release 中的 v1.2.0。
- 逐筆紀錄僅存在記憶體中，不會持續寫入硬碟；清空、切換範圍或結束程式前需手動匯出。記憶體用量隨取樣筆數增加。
- 全部介面模式仍可能將 VPN 與實體介面重複計算；需要精確區分時指定單一介面。累積量為有效取樣統計，不是帳單用量。

## 本機啟動

雙擊同資料夾的「啟動網速工具-v1.2.0.cmd」，會呼叫原專案的 `run-local.cmd`，執行 `D:\projects\網速工具\build\Release\RealtimeNetworkSpeed.exe`。使用本機 Qt 安裝，不複製程式或覆寫既有部署。

## API 文件參考

Qt 用法先透過 Context7 查詢，並與安裝的 Qt 6.12.0 標頭及編譯結果核對。

- [Windows MIB_IF_ROW2](https://learn.microsoft.com/en-us/windows/win32/api/netioapi/ns-netioapi-mib_if_row2)：介面計數器、MAC、連線速度及狀態欄位。
- [Qt QNetworkInterface](https://doc.qt.io/qt-6/qnetworkinterface.html)：本機介面的位址資訊。
- [Qt QSaveFile](https://doc.qt.io/qt-6/qsavefile.html)：暫存寫入、commit 與失敗保護。
