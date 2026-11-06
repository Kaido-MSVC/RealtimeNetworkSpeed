# v1.3.1 發布驗證紀錄

日期：2026-11-06（Asia/Taipei）。v1.3.0 tag／Release 保留，另建立 v1.3.1 修補版。

## 保留與修正

- 開始時 main 基準為 ee6be22dfb532ce116e541ed6b1cfcb098e962dd（v1.3.0）。CMakeLists.txt、build-and-test.ps1、history.h、main.cpp、tests/verification.cpp 共 5 個既有修改檔已完整備份，前兩輪日期／歷史安全成果納入此版。history.h 與 main.cpp 與上一輪交付副本逐檔 SHA-256 相同。
- 取樣間隔異常獨立為 intervalWarning，只顯示文字；clockWarning 維持真正日期防護及日期快照受阻。新增 UI 按鈕／通知隔離與下一筆有效取樣恢復斷言。
- version.h 共用於程式關於視窗與 Windows 版本資源，正式 EXE FileVersion／ProductVersion 為 1.3.1；安裝程式版本由同一檔案讀取。
- 日期、跨多日啟動、快照位置／輪替與失敗恢復、通知行為詳見 DEVELOPMENT-v1.3.1.md。歷史格式未變更。

## 建置與測試

- C++17；官方 Qt 6.12.0 MSVC 2022 x64（Core／Gui／Widgets／Network；測試 Test）；Visual Studio 18 2026，MSVC 19.51.36260.0，Windows SDK 10.0.28000.0，CMake 4.4.4。
- Release 配置與編譯成功。CTest traffic-logic、units-and-ui **2/2 通過，10.98 秒**；含執行中日期跳動、3／7／30 日關機重啟、365 日保留、完整快照與最多三份輪替、快照／主檔／備份失敗、鎖定恢復、確認取消與成功、受阻通知去重、F10 刪除後不復活及必要既有回歸。
- 首次受限環境阻擋測試暫存寫入與隔離登錄鍵，造成測試失敗；以正常 Windows 權限重跑隔離測試後通過。首次編譯的封裝測試曾存取私有 timer 成員，已改用公開視窗可見性檢查；正式程式未因此改動。
- 封裝使用 Qt DLL／外掛與官方 Visual Studio Redist/MSVC/14.52.36815/x64/Microsoft.VC145.CRT。windeployqt 的 CRT 路徑警告由既有 Redist 複製備援處理；未使用的 D3D12、PDF、SVG、TLS 與 OpenGL 備援未隨附。
- 乾淨 PATH 下，使用同一 UI 程式碼的驗證執行檔加 Qt Test（僅隔離測試目錄）啟動、顯示及擷取畫面通過；正式套件不含測試執行檔／Qt Test。此方式不開啟使用者設定或歷史；未直接啟動正式 EXE。
- NSIS 3.13 安裝版編譯成功；目前使用者安裝、26 個 payload 檔案逐一 SHA-256、版本登錄、引號包圍的移除命令、重裝、靜默移除、根目錄／巢狀額外檔案保留、捷徑與移除登錄清理均通過（40 項檢查，包含使用者設定／主歷史／備份雜湊不變）。未執行安裝版的程式。

## 授權與附件

- 已讀 D:/licenses/README.md。既有 LGPL-3.0-only、GPL-3.0-only、MPL-2.0 與本機條文庫 SHA-256 相同，MIT 作者聲明與 Qt 第三方聲明保留；沒有下載或改動授權範本。
- Qt 6.12.0 動態連結，可替換相容 DLL；完整未修改 qtbase-everywhere-src-6.12.0.tar.xz 沿用既有檔案，SHA-256 a951bd163c7b80fc6b8c88d7668fb56abf91c152373e13c10666763238131307，另隨 Release 提供。
- 正式附件：RealtimeNetworkSpeed-1.3.1-Windows-x64-Setup.exe、RealtimeNetworkSpeed-1.3.1-Windows-x64-Portable.zip、RealtimeNetworkSpeed-1.3.1-source.zip、qtbase-everywhere-src-6.12.0.tar.xz、SHA256SUMS.txt。
- 正式原始碼 ZIP 由發布提交以 git archive 產生；不含 build、dist、archives、backup、evidence、私人設定／歷史、暫存測試資料或憑證。封裝 ZIP CRC、payload 逐檔比對與上傳後遠端 SHA-256 核對另記本機交付報告。

## 測試限制

執行檔與安裝程式未簽章。未在乾淨 Windows／VM、Windows 10、真實登入自動啟動、混合 DPI 實機或長時間壓力環境驗證；本次未人工操作安裝精靈各頁。乾淨 PATH 與隔離 UI 程式碼驗證不等於乾淨電腦正式 EXE 實測。歷史非帳單用量，異常退出可能失去未保存約 60 秒；系統通知仍受 Windows 設定限制。清理遭鎖定時日期快照可能暫時多於三份並顯示提示。
