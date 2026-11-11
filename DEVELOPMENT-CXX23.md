# C++23 正式工具鏈升級

日期：2026-11-11。基於 v1.3.1 提交 `0504e94f82f8cdf77fac1e10570e4fdba2648f58`，由 `experiment/cxx23-msvc1452` 整合。此變更不建立新版本、Tag 或 Release，不變更 Qt、產品功能、資料格式或備用 qmake 的 C++17 設定。

## 決策與審查修正

正式路徑使用 CMake、C++23、Release x64。主程式、Verification、TrafficTests 各自宣告 `target_compile_features(... PRIVATE cxx_std_23)`，保留 `CMAKE_CXX_STANDARD_REQUIRED`，不再全域指定 C++17。沒有修改 C++ 功能程式碼。

依使用者提供的 Claude Code 獨立審查結論「PASS WITH NOTES」與列出的缺失，完成以下修正；本機沒有另存完整審查報告，不將摘要當成額外測試證據。

- **P1**：README 更新正式 C++23／MSVC 最低版本及 Qt 6.12.0 要求，區分正式 CMake 與 C++17 備用 qmake；歷史版本的 C++17 記錄明確標為當時狀態。README 沒有對應的英文建置段落。CHANGELOG 新增未發布工具鏈紀錄，不宣稱新功能或加速。
- **P2-1**：版本檢查與映射覆寫僅在 `CMAKE_CXX_COMPILER_ID STREQUAL "MSVC"` 時執行，不以 MSVC 相容前端判斷真正編譯器。
- **P2-2**：真正 MSVC 低於 19.52（工具集 14.52）時 `FATAL_ERROR`，訊息列出需求與偵測版本。其他編譯器交由 CMake 的 C++23 feature 支援判斷，不套用 MSVC 版本限制。
- **P2-3**：`RealtimeNetworkSpeed.pro` 逐位元組不變，仍為 C++17 備用路徑。
- **P3**：保留現有 AUTOMOC／AUTORCC 及測試結構；沒有重構建置系統。只有當 CMake 的 C++23 standard／extension 映射仍精確為 `-std:c++latest` 或 `/std:c++latest` 時才替換成 `-std:c++23`。未來原生的 C++23 映射保持原樣，不另追加 target 旗標。

本機 CMake 4.4.4 的 MSVC 模組仍使用 c++latest；覆寫侷限專案作用域，不修改安裝的 CMake。這是建置工具映射修正，未發現 Qt 或程式碼需要相容性修補。CMake 的 meta feature 只要求編譯模式，不等於本專案驗證了 C++23 全部功能。

正式 `build-and-test.ps1` 增加僅限 `-BuildOnly` 的 `-Ninja`／`-BuildDirectory`，以便使用指定 x64 開發者環境並隔離建置與日誌。未指定時保留原有 Visual Studio 打包流程；打包的原始碼清單也納入本文件。沒有執行會覆寫既有 dist／archives 的預設打包流程。

## 驗證環境

| 項目 | 實測值 |
|---|---|
| Visual Studio | Community 2026 18.12.0（18.12.12405.243） |
| 工具集 | 14.52.37025，Hostx64／x64 |
| cl.exe | 19.52.37029.0 |
| Qt | 6.12.0 官方 msvc2022_64，動態連結 |
| CMake／Ninja | 4.4.4／1.13.2 |
| Windows SDK | 10.0.28000.0 |
| 主機 | Windows 11 Home x64，10.0.26200，Core i5-8600K |

直接依賴仍為 Qt Widgets／Network（連帶 Core／Gui）、Windows iphlpapi；驗證另用 Qt Test。沒有更新依賴。Qt 官方 x64 支援表列 MSVC 2022，本次搭配 MSVC 2026 的成功是本機相容性證據，不擴張上游支援承諾。

## 正式驗證結果

在透過 `vcvarsall.bat x64 -vcvars_ver=14.52` 初始化的子行程執行正式腳本，使用新建置目錄：

```powershell
# QT_DIR 預先指向官方 Qt 6.12.0 的 msvc2022_64 安裝目錄
pwsh -NoProfile -File build-and-test.ps1 -BuildOnly -Ninja -BuildDirectory evidence/cxx23-integration-20261111/build
```

| 檢查 | 結果 |
|---|---|
| 三個 target 完整 Release x64 建置 | 成功，exit 0 |
| 原有警告設定下的編譯／連結警告 | 0；未提高警告等級，Qt 外部標頭仍使用既有 external:W0 |
| 實際標準旗標 | 8 個 C++ 編譯單元均只有一個 `-std:c++23`，沒有 c++latest |
| CTest traffic-logic | 通過 |
| CTest units-and-ui | 通過，包含全部現有回歸 |
| CTest 合計 | 2/2，12.82 秒；不作效能結論 |
| 主程式大小 | 316,928 bytes |
| 舊 MSVC 防護 | 實際 19.44.35230.0 configure 被清楚拒絕 |
| clang-cl 排除 | 實際 Clang 22.1.3 configure 成功，使用 `-clang:-std=c++23`；未做其完整編譯／執行驗證 |
| 映射規則檢查 | 以實際專案條件區塊確認：原生 C++23 映射保留、兩種 c++latest 拼法修正、Clang 映射不變 |

configure 中可選 Vulkan headers 未找到、Windows pthread 探測失敗但找到 Threads，與既有環境一致，未影響 Widgets 建置。原始碼打包清單與 PowerShell 語法另行檢查，不因文件清單變更重跑已通過的 CTest。

### 功能與部署

- Windows Qt platform 的既有驗證通過：主／迷你視窗、單位與圖表同步、偏好及視窗位置、網卡篩選／消失恢復、暫停／恢復、逐筆與歷史 CSV 寫入讀回、錯誤路徑保留舊檔。
- 歷史及高影響清除回歸通過：跨日／保留期限、3／7／30 日重啟、日期隔離與恢復、快照輪替、鎖定及提交失敗、F10 範圍刪除／取消／防備份復活／退出與後續取樣。
- 以 agent-desktop 操作隔離驗證視窗：真實 Intel I219-V 取樣、暫停／恢復、歷史圖表、清除對話框及取消。有效取樣由 31.0 秒增加至 70.5 秒，紀錄由 62 增加至 141 筆。
- 透過實際 Windows 存檔對話框匯出 CSV，讀回標頭及真實取樣列，產生 19,059 bytes 的 CSV；不操作既有使用者設定或歷史。
- 系統匣 Qt 訊號、選單動作、隱藏後持續取樣、恢復視窗及退出生命週期自動回歸通過。實體系統匣展開操作被 agent-desktop 的 headless 政策拒絕，**實體圖示點擊與 Windows 通知呈現未驗證**。沒有為測通知中斷真實網路或改動系統通知設定。
- 將 Release 主程式與 Qt DLL／外掛部署到新目錄。windeployqt 初次選到不完整的 14.53 Redist，缺少 CRT；沒有略過失敗，改從已安裝的 14.52.37025 x64 CRT 補齊。既有打包腳本也有從實際存在的 Redist 複製 CRT 的備援流程。
- 檢查正式 EXE 的 PE 依賴；只含 Windows 系統目錄的 PATH 下，部署目錄中的 `--packaged-smoke` 成功。互動驗證行程載入的 Qt Core／Gui／Widgets／Network／Test、qwindows 及 MSVC CRT 均來自部署目錄，未靠 Qt 開發目錄補足 DLL。

為避免讀寫既有資料，GUI 執行使用包含相同 main.cpp 視窗與功能實作的 Verification，QSettings 與歷史隔離；正式 EXE 本身已完整編譯及部署／檢查依賴，但沒有直接啟動其會使用真實設定的入口。此驗證不等同乾淨 Windows 或安裝器測試。Qt6Test.dll 與 Verification 只屬驗證用途，正式發布仍不應附帶。

## 原始實驗摘要

同一工具鏈與 Qt 下，C++17／C++23 完整編譯均為 0 警告、CTest 均 2/2，主程式均為 316,928 bytes，沒有修改 C++ 功能碼。

既有 `--measure-csv` 使用固定 172,800 筆資料、相同 Release 最佳化與 runtime；每版本每種字串配置預熱一次、交錯量測五次。共用字串匯出中位數 C++17／C++23 為 915／876 ms（範圍 862–969／827–941）；獨立字串為 1,766／1,338 ms（範圍 900–2,327／898–2,397）。每次匯出均為 37,325,071 bytes，紀錄大小均為 160 bytes。

範圍重疊且部分測次波動較大，只支持「此工作負載未見明顯退步」，不能歸因為 C++23 加速，也不外推長時間 CPU、啟動速度或網卡吞吐。本次只修改建置防護及文件，沒有重新進行這組已完成的效能比較。

## 保留與範圍

既有 main 及所有 Tag 在整合前保留原提交；完成必要驗證後才提交並整合到本機 main。未變更 version.h、qmake、授權條文或 C++ 功能碼，未推送、建立 Tag 或發布。舊 build／dist／archives 與使用者資料在操作前後以 SHA-256 檢查。

原始實驗、完整審核前報告、編譯／CTest 日誌、CSV、截圖、工具鏈絕對路徑、雜湊清單及可執行驗證腳本保留在被忽略的 `evidence/`，不納入版本庫。本文件只保存方法、結果與限制。

結論：在本次 MSVC 14.52、Qt 6.12.0、CMake／Ninja、Release x64 範圍，適合正式採用 C++23。待補項目是實體系統匣點擊／通知呈現及其他編譯器完整驗證；它們沒有被標示為已通過，也不構成此次已授權本機整合的阻擋。

參考：已用 Context7 確認 CMake compiler ID／frontend variant 差異及 Ninja Release 設定；本機編譯器模組與 compile_commands 為實際旗標依據。[CMake compile features](https://cmake.org/cmake/help/latest/command/target_compile_features.html)、[Qt Windows 支援組合](https://doc.qt.io/qt-6/windows.html)、[MSVC 14.52 C++23 說明](https://devblogs.microsoft.com/cppblog/msvc-build-tools-version-14-52-release-candidate-now-available/)。
