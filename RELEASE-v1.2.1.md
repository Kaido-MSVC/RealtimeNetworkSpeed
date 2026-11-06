# v1.2.1 發布驗證紀錄

日期：2026-11-06（Asia/Taipei）

使用者已親自驗收 v1.2.1 全部本機修正，確認沒問題並明確授權完整發布 GitHub。本次在原專案進行正式發布。

## 版本與內容

- 起點為 main／v1.2.0：`3e2ca4bdae9d69cde82129f8e2f2868b5f14827a`；發布前遠端 main 與 v1.2.0 指向一致，尚無 v1.2.1 tag。
- 原有 7 個修改檔及 DEVELOPMENT-v1.2.1.md 均屬已驗收修正；README 改為正式 v1.2.1 下載資訊，開發記錄保留歷史並指向本發布記錄。
- 主程式關於視窗與 app.rc 共用 version.h 的 1.2.1；Windows 檔案／產品版本為 1.2.1，執行檔為 AMD64。
- 保留 Qt Network、RealtimeNetworkSpeed.pro 與 archives 歷史資料。原專案授權聲明及第三方條文維持原內容。

## 正式建置與封裝驗證

- C++17；Qt 6.12.0 官方 MSVC 2022 x64，Widgets／Network；Visual Studio 18 2026、MSVC 19.51、Windows SDK 10.0.28000.0、CMake 4.4.4。
- Release x64 配置與建置成功，CTest traffic-logic／units-and-ui **2/2 通過**（10.00 秒）。涵蓋保留期限／筆數、CSV 寫入失敗保護、取消與先匯出流程、單一執行個體與視窗位置等必要回歸。
- 正式 dist 已更新為 v1.2.1；原 v1.2.0 dist 已備份到忽略的 backup 目錄。
- 建置腳本完成部署及 Windows 系統 PATH 啟動檢查。windeployqt 找不到 MSVC Redist 時，既有後備流程已成功複製實際 CRT；D3D12 備援警告不影響目前 Widgets 軟體繪圖使用。
- 可攜包解壓後 **26 個檔案**逐檔 SHA-256 與 dist 相同；清除 Qt 外掛相關環境變數、PATH 只留 Windows 系統目錄，確認建立可回應的 Windows 主視窗。
- 實際載入的 Qt Core／Gui／Widgets／Network、Windows 平台／樣式及 ICO 外掛均來自解壓資料夾；第二次啟動完成本機喚醒並以 0 退出。
- 隱藏啟動時 .NET MainWindowHandle 為 0；以 Windows 原生視窗列舉核實主視窗及回應狀態，無需修改程式。
- 發布前 31 個既有 archives 檔案均以 SHA-256 核對未改動。

## 授權與附件

- 已先讀取 D:\licenses\README.md。LGPL-3.0、GPL-3.0、MPL-2.0 全文與本機授權庫一致；BSD-3-Clause 與本機範本一致（版權占位符已正確替換，忽略換行差異）。MIT 的兩份專案副本一致；未修改授權庫範本。
- Qt Network 的 Public Suffix List／libpsl 聲明與全文均已附上。Qt 以可替換 DLL 動態連結。
- 隨附既有、未修改 Qt 官方 qtbase 6.12.0 原始碼包，SHA-256：`a951bd163c7b80fc6b8c88d7668fb56abf91c152373e13c10666763238131307`。
- 沿用專案既有免安裝發布方式，無安裝程式。正式附件：`RealtimeNetworkSpeed-1.2.1-Windows-x64-Portable.zip`、`RealtimeNetworkSpeed-1.2.1-source.zip`、`qtbase-everywhere-src-6.12.0.tar.xz`、`SHA256SUMS.txt`。
- 正式原始碼 ZIP 以 git archive 由發布提交產生，包含所有受版本管理的原始碼與文件；build、dist、archives、backup、臨時工作區與私密資料不提交。

## 測試範圍限制

執行檔未數位簽章。未在 Windows 10 或乾淨電腦實測，未做實體混合 DPI 多螢幕或真實連續 24 小時運行測試；150% 為先前已通過的 Qt 縮放測試，24 小時以受控時間與筆數驗證。VPN／網卡轉換以模擬覆蓋，Excel 開啟 CSV 未實測。使用者已親自驗收，先前 Windows 原生 CSV 另存流程及讀回驗證通過，本次不無故重跑。

逐筆資料仍只在記憶體中，退出會清除，逾保留範圍不可補匯出；匯出／確認期間不取樣。累積統計代表本次有效取樣，並非帳單用量。

正式發布後的遠端 main／tag／Release、附件下載及雜湊對照結果另存本次交付報告。
