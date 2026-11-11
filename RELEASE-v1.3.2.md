# v1.3.2 發布驗證紀錄

日期：2026-11-11。正式 CMake 建置採 C++23／MSVC 14.52，Qt 維持 6.12.0；qmake 保留 C++17 備用路徑。既有功能、資料格式與歷史 Tag／Release 不變，不宣稱效能提升。

版本來源仍為 version.h，關於視窗、Windows EXE 資源及 NSIS 安裝器均使用此版本。CMake project 沒有獨立版本常數，避免增加第二份版本來源。

## 建置與旗標

本次使用新的隔離原始碼副本及建置目錄，執行既有 build-and-test.ps1 完整建置／測試／部署流程與 build-installer.ps1；固定 Visual Studio 2026 18.12.0、MSVC 工具集 14.52.37025（cl.exe 19.52.37029.0）、Qt 6.12.0、CMake 4.4.4、Windows SDK 10.0.28000.0、Release x64。完整發布流程使用 Visual Studio 18 2026 產生器，明確選擇 v145 的 14.52.37025 x64 工具集，不混用原本 build 的 CMakeCache。

- 三個 target 建置成功，原有警告等級下編譯／連結警告 0；沒有啟用更高警告等級。
- MSBuild 的 Release CL.command.1.tlog 共 8 個 C++ 編譯單元，每個只有一個 `-std:c++23`，沒有 c++latest。既有 CMake 防護不變：真正 MSVC 低於 19.52 明確報錯、clang-cl 不套用 MSVC 映射、未來原生映射保持原樣。
- 正式 EXE FileVersion／ProductVersion 為 1.3.2，大小 315,904 bytes；與先前 Ninja 實驗的大小不作效能或最佳化推論。關於視窗實際顯示「關於即時網路速度 1.3.2」。
- CTest traffic-logic、units-and-ui **2/2 通過，13.46 秒**。包含真實取樣、指定介面及網卡資訊、平均／峰值／統計、暫停恢復、CSV 寫入讀回、歷史讀取及日期／範圍篩選、F10 清除、防備份復活、日期及保存失敗防護、設定保存／子行程重新載入、系統匣生命週期等既有回歸。

## GUI、部署及安裝

- 部署目錄以只含 Windows 系統目錄的 PATH 啟動同一 main.cpp 視窗實作的隔離 Verification，設定及歷史使用測試目錄。實際桌面確認 Intel I219-V 即時流量、網卡資訊、平均／峰值、暫停／恢復、關於版本與關閉；恢復後有效取樣由 83.5 秒增加至 103.0 秒。CSV、歷史與清除的實際檔案操作由完整 CTest 驗證。
- 介面原本以繁體中文為主，關於／授權含既有英文文字，沒有可切換語系系統；本次沒有新增或假稱驗證其他語言。GitHub Release Notes 提供繁中與英文。
- 系統匣自動回歸通過；實體系統匣展開被 agent-desktop headless 政策拒絕，**實際圖示點擊及 Windows 通知呈現未驗證**。
- windeployqt 找不到預設 14.53 CRT 時，初始備援曾選到較新的 14.53；本次修正備援排序，優先使用建置工具集對應的完整 CRT。最終附帶官方 14.52.37025 Redist（DLL 實際修訂 14.52.37029.0），沒有附帶 14.53 CRT。
- 封裝腳本補上排除未使用的 dxcompiler.dll／dxil.dll，與既有 PDF／SVG／TLS／OpenGL 備援移除規則一致。最終乾淨 PATH 隔離 GUI 重新驗證通過。Qt Core／Gui／Widgets／Network、qwindows 與 CRT 實際載入自隔離部署目錄。
- 正式 payload 共 26 檔，包括必要 Qt DLL／platforms／styles／imageformats、CRT、qt.conf 與授權；不含 Verification、Qt6Test、PDB、SDK、編譯器、CSV／歷史／設定或測試資料。
- NSIS 3.13 安裝版成功，40 項檢查通過：目前使用者靜默安裝、26 個 payload 雜湊、1.3.2 版本登錄、帶引號移除命令、重裝、靜默移除、額外根目錄／巢狀檔案保留、捷徑與登錄清理、使用者設定／歷史主檔／備份雜湊不變。不啟動實際使用者資料的正式 EXE。

封裝修正只改依賴選擇及移除清單，沒有改 C++ 程式或編譯設定；在最終 payload 重做必要的部署啟動及安裝測試，沒有無故重跑已通過的 CTest。

## 授權、附件與保留

已讀本機授權庫規則。既有 LGPL-3.0-only、GPL-3.0-only、MPL-2.0 條文與本機授權庫一致，本程式 MIT 副本與 LICENSE 相同，Qt 第三方聲明保留；沒有下載或修改授權範本。Qt 為官方未修改動態 DLL，完整對應 qtbase-everywhere-src-6.12.0.tar.xz 沿用既有檔案，SHA-256 為 `a951bd163c7b80fc6b8c88d7668fb56abf91c152373e13c10666763238131307`。

正式附件沿用五項：

- RealtimeNetworkSpeed-1.3.2-Windows-x64-Setup.exe
- RealtimeNetworkSpeed-1.3.2-Windows-x64-Portable.zip
- RealtimeNetworkSpeed-1.3.2-source.zip
- qtbase-everywhere-src-6.12.0.tar.xz
- SHA256SUMS.txt（前四項，以小寫 SHA-256、兩個空格及檔名記錄）

可攜 ZIP 以最終 26 檔 payload 產生並核對 CRC／逐檔雜湊；正式原始碼 ZIP 由發布提交以 git archive 產生，核對追蹤檔清單、CRC／內容及私密資料特徵。安裝包已實際安裝／移除驗證。正式附件公開前須核對完整資產，再公開穩定 Release；上傳後逐一重新下載並核對 SHA-256，結果保留本機證據。

原有 build、dist、archives、使用者 INI／歷史／備份及既有 Tag／Release 先記錄清單與雜湊，不覆寫舊版本。本次新產物保存在新版本目錄；建置、封裝、安裝、遠端核對及保留記錄位於被忽略的 evidence/v1.3.2，不放入正式原始碼。

## 限制

正式 EXE 與安裝包未簽章。乾淨 PATH 的同源隔離 Verification 不是乾淨 Windows 的正式 EXE 實測；未重做乾淨 VM、Windows 10、混合 DPI、登入自動啟動或長時間壓力測試。未人工操作安裝精靈各頁，也未人工驗證系統匣點擊／通知。歷史仍非帳單用量，異常退出可能失去尚未保存約 60 秒；日期快照受鎖時可能暫時超過三份。這些限制沒有被描述為已通過。
