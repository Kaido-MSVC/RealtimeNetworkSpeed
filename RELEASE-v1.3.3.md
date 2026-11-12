# v1.3.3 發布驗證紀錄

日期：2026-11-12。基於 v1.3.2，只修改建置腳本與 CMake 連結／編譯旗標；C++ 程式碼、功能、資料格式、Qt 6.12.0 與隨附 DLL 均不變。

## 變更

- `build-and-test.ps1` 在 Visual Studio 產生器下以 `-T version=14.52.37025` 固定工具集（可用 `-Toolset` 覆寫），CRT 備援依同一版本選擇；Ninja 仍依目前開發者環境。
- 每次 configure 使用 `cmake --fresh`。本機既有 build 快取記錄已移除的 14.51.36231，原本會被 19.52 版本防護擋下。
- 正式 EXE 啟用 `/guard:cf`（編譯與連結）及 `/CETCOMPAT`。
- README 修正 zhtw-guard 回報的 4 處大陸用語。

## 驗證結果

以預設 `build-and-test.ps1` 完整流程（Visual Studio 18 2026 產生器、原有 build 目錄）及 `build-installer.ps1` 執行；Visual Studio 2026 18.12、MSVC 14.52.37025（cl.exe 19.52.37029.0）、Qt 6.12.0、CMake 4.4.4、Windows SDK 10.0.28000.0、Release x64。

- 舊 build 快取在 `--fresh` 下重新偵測成功，實際使用 14.52.37025；CRT 備援選用 14.52.37025 的 Microsoft.VC145.CRT。
- CTest traffic-logic、units-and-ui 2/2 通過；乾淨 PATH 隔離 UI 啟動測試通過。另以 `/W4 /permissive-` 試編三個 target，僅有既有的 `QMessageBox::setButtonText` 棄用警告。
- 正式 EXE FileVersion／ProductVersion 1.3.3，321,024 bytes；dumpbin 確認 Dynamic base、NX、High Entropy VA、Control Flow Guard（185 個函式）、CET compatible。
- payload 26 檔，與 v1.3.2 可攜版比對：僅 `RealtimeNetworkSpeed.exe` 不同，Qt DLL／外掛、CRT、qt.conf 與授權逐位元組相同。
- NSIS 安裝包 FileVersion 1.3.3。隔離目錄安裝測試 91 項通過：靜默安裝、26 檔雜湊、1.3.3 登錄版本、帶引號移除命令、捷徑、重新安裝、靜默移除、額外檔案保留、登錄與捷徑清理、使用者設定／歷史／備份 3 檔雜湊不變、既有 archives 50 檔不變。

## 限制

與 v1.3.2 相同：EXE 與安裝包未簽章；未在乾淨 VM、Windows 10 測試；實體系統匣點選與 Windows 通知呈現未人工驗證；GUI 驗證使用同源隔離 Verification，不以真實使用者資料啟動正式 EXE。
