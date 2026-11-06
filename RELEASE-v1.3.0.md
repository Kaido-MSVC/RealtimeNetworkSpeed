# v1.3.0 發布驗證紀錄

日期：2026-11-06（Asia/Taipei）。使用者已親自驗收 F01～F10，明確授權同步、清理與正式發布。

## 同步與保留

- 原專案基準為 main／v1.2.1 `7865c1ead6f4d40cbc53e277caf4ad15f7401b8a`，原有 6 個修改檔及 history.h、DEVELOPMENT-v1.3.0.md 保留並納入本次版本。
- 已逐檔核對 F01～F09 候選原始碼 25 檔：20 檔與原專案相同；history.h、main.cpp、tests/verification.cpp、README.md、DEVELOPMENT-v1.3.0.md 五檔差異均對應後續 F10。F10 增量反向檢查完整通過，原專案已具備最新成果，未以舊候選副本覆蓋原始碼。
- 舊 archives 全部 37 檔預先計算 SHA-256；發布流程僅新增 v1.3.0 附件，不覆寫舊檔。
- 兩階段測試報告、日誌、截圖、F10 增量、開發前基準與同步清單保存於原專案忽略的 evidence/v1.3.0，含本機路徑與測試素材，不提交 GitHub。正式開發紀錄保留在 DEVELOPMENT-v1.3.0.md。
- 舊 dist 完整搬存於忽略的 backup/dist-v1.2.1-before-v1.3.0，逐檔驗證後才替換正式 dist。

## 建置與驗證

- C++17；Qt 6.12.0 EXACT 官方 MSVC 2022 x64（Core／Gui／Widgets／Network；測試 Test）；Visual Studio 18 2026／MSVC x64、Windows SDK 10.0.28000.0、CMake 4.4.4。
- Release 重新配置與建置通過。CTest traffic-logic、units-and-ui **2/2 通過，10.58 秒**。
- 覆蓋每日／跨日／月流量、365 日保留、保存／重載／損壞復原、F10 範圍與日期隔離、預設取消、主檔／備份鎖定失敗、刪除後自動保存與正常退出不復活、後續重新累積、CSV BOM／CRLF／整數／公式防護、單一個體等必要回歸。
- 本次 Release EXE 與使用者已驗收 F10 執行檔逐位元組一致，版本 1.3.0；SHA-256 `301178af6fd169f411f36f67b41b4dda74e7a74e499d9b6c356fffba8fcebb36`。程式碼與測試未在此次發布作額外修改。
- 可攜 ZIP CRC 通過；26 檔逐檔 SHA-256 與 staging／正式 dist 一致；授權副本一致。解壓後移除 Qt 相關環境變數、PATH 僅 Windows 系統目錄，啟動成功。
- 解壓版 `--startup` 啟動後，第二程序以 0 退出並喚醒可回應的可見視窗，原程序仍運行；實際載入的 Qt DLL 及平台／樣式／ICO 外掛均在可攜資料夾內。僅結束此次測試程序。
- 最初的測試啟動器使用 Windows 強制隱藏旗標，導致第一次顯示主視窗受旗標影響；改以程式自己的 --startup 流程控制隱藏後，獨立啟動檢查通過，無需修改產品。
- 100／150／200% 縮放及 F10 200% 證據沿用已驗收且相同程式碼的既有測試，不無故重跑。未進行低機率極端壓力測試。

## 授權與封裝

- 已讀取 D:/licenses/README.md；MIT、LGPL-3.0-only、GPL-3.0-only、MPL-2.0 及 Qt 第三方聲明沿用既有正確副本，LGPL／GPL／MPL 與本機授權庫核對一致；未修改範本、未重新下載條文。
- Qt 6.12.0 動態 DLL 可替換；隨附未修改官方 qtbase-everywhere-src-6.12.0.tar.xz，SHA-256 `a951bd163c7b80fc6b8c88d7668fb56abf91c152373e13c10666763238131307`。本次未增加第三方依賴。
- MSVC CRT 從 Visual Studio 官方 VC/Redist/MSVC/14.52.36815/x64/Microsoft.VC145.CRT 部署，未從編譯器 bin 或 Windows 系統目錄擷取 DLL。依 [Microsoft 散布文件](https://learn.microsoft.com/en-us/cpp/windows/redistributing-visual-cpp-files?view=msvc-170)核對 Redist 來源；Qt 部署參考 [Qt 6.12 文件](https://doc.qt.io/qt-6.12/windows-deployment.html)。
- 正式附件：RealtimeNetworkSpeed-1.3.0-Windows-x64-Portable.zip、RealtimeNetworkSpeed-1.3.0-source.zip、qtbase-everywhere-src-6.12.0.tar.xz、SHA256SUMS.txt。
- 原始碼 ZIP 使用 git archive 由本次發布提交產生；不納入 build、dist、archives、backup、evidence、工作區暫存、使用者歷史或設定。
- 公開頁面：https://github.com/Kaido-MSVC/RealtimeNetworkSpeed/releases/tag/v1.3.0 。遠端 ref、附件重新下載驗證與最終工作區清理結果另存本次交付報告及本機 evidence。

## 實測限制

執行檔未簽章。未在 Windows 10、乾淨電腦／VM、實體混合 DPI／多螢幕切換、連續 24 小時、真實登出登入、Excel 開啟 CSV 實測；實體網卡拔除／停用與 VPN 切換以注入資料模擬。乾淨 PATH 不等同乾淨 Windows。歷史只代表有效監控期間、非帳單用量；異常退出可能失去未保存的約 60 秒，備份復原可能回到再前一次保存點。F10 會清除相應主檔及復原備份，其他人工保存的舊檔由使用者自行管理。
