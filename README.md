# 即時網路速度（RealtimeNetworkSpeed）

Windows x64 即時顯示網路卡下載／上傳速度的小工具。使用 C++17／Qt 6 Widgets，透過 Windows `GetIfTable2`（iphlpapi）讀取網卡計數器，圖表以 QPainter 繪製，不依賴 Qt Charts。

作者：**Kaido-MSVC**。本程式完全在本機執行，不連網、不上傳任何資料。

目前版本：**v1.1.0**。正式套件與對應原始碼見下方 Release 附件。

### v1.1.0 新增功能
- 系統匣右鍵「還原／結束」，雙擊還原主視窗。「關閉時縮至系統匣」預設關閉，可記住選擇；系統匣不可用時直接退出。
- 「迷你視窗」同步顯示即時下載／上傳速度與所選單位，可獨立關閉及還原主視窗。
- 「視窗置頂」同時套用主視窗與迷你視窗，保存至既有 INI 設定。
- 本次監看累積下載／上傳／合計量，以十進位 MB 顯示，從有效位元組差值累加；重啟程式、手動重設或監看範圍切換時歸零。
- 計數器倒退、新介面及讀取失敗空檔不補算；取樣間隔超過 2 秒（例如休眠或長時間停頓）重新建立基準，該空檔不計入累積。因此本功能是有效取樣累積，並非帳單流量統計。
- 新設定鍵：`display/topmost`、`display/closeToTray`，均預設 false。

## 下載
從 [Releases](https://github.com/Kaido-MSVC/RealtimeNetworkSpeed/releases) 下載：

| 附件 | 內容 |
| --- | --- |
| `RealtimeNetworkSpeed-1.1.0-Windows-x64-Portable.zip` | 免安裝版。完整解壓後執行 `RealtimeNetworkSpeed\RealtimeNetworkSpeed.exe` |
| `RealtimeNetworkSpeed-1.1.0-source.zip` | 本程式原始碼，內容與 `v1.1.0` Tag 相同 |
| `qtbase-everywhere-src-6.12.0.tar.xz` | 隨附 Qt DLL 的完整對應原始碼（Qt 官方原始碼包，未修改） |
| `SHA256SUMS.txt` | 上述附件的 SHA-256 |

Qt DLL、外掛與 Visual C++ 執行階段 DLL 都已隨附，不必另外安裝 Qt 或 VC++ 可轉散發套件。請保留整個資料夾，不要只複製 EXE。執行檔**未數位簽章**，Windows SmartScreen 可能會提示。

### 系統需求
- **文件支援範圍**：Windows 10 版本 1809 以上，或 Windows 11（x64）。這是 Qt 6.12 官方文件所列的支援平台。
- **實際測試結果**：只在 Windows 11 Home x64（26200）測試過，包含建置、兩組自動測試，以及只含 Windows 系統資料夾的 PATH 下的啟動檢查。沒有在 Windows 10 實機或虛擬機測試。

## 功能
- 每 0.5 秒更新下載、上傳速度，圖表依實際時間顯示最近 60 秒，最多保留 120 筆；超過 2 秒的取樣間隔以斷線呈現。
- **網卡選擇**：「所有實體網卡」（預設）、個別實體網卡，或「全部介面（含 VPN／虛擬網卡）」。選擇會記住；手動切換或拔插造成自動切換網卡時，圖表會清空並重新建立取樣基準。
- **顯示單位**：自動、Kbps、Mbps、Gbps、KB/s、MB/s。自動模式依目前速度與 60 秒峰值，在 Kbps／Mbps／Gbps 之間選擇共同單位。
- 採十進位：1 Kbps = 1000 bit/s；1 KB/s = 8 Kbps；1 MB/s = 8 Mbps。

### 統計方式
- 排除 loopback 與 NDIS 篩選驅動介面（`FilterInterface`），避免同一張網卡被重複計算。
- 逐介面（以 `InterfaceLuid` 比對）計算前後兩次取樣的差值，只計入兩次都存在且已連線的介面；介面上線、斷線或計數器重設時不會產生假尖峰。
- 「全部介面」會把 VPN、Hyper-V 等虛擬介面一起加總，流量經過實體網卡時可能重複計算。

讀取失敗時，速度顯示「—」且圖表斷線；恢復後先重新建立基準，下一次成功取樣才計算速度。應用程式版本定義於 `version.h`，供關於視窗與 Windows 檔案版本資源使用。

## 偏好設定
存於 `%APPDATA%\RealtimeNetworkSpeed\RealtimeNetworkSpeed.ini`，只保存在本機：

| 鍵 | 值 |
| --- | --- |
| `display/unit` | `auto`、`Kbps`、`Mbps`、`Gbps`、`KB/s`、`MB/s`（舊版存的 0～5 整數仍可讀取） |
| `display/topmost` | `true`／`false`，預設 `false`，主視窗與迷你視窗共同置頂 |
| `display/closeToTray` | `true`／`false`，預設 `false`；系統匣不可用時直接退出 |
| `display/adapter` | `physical`、`all` 或網卡 GUID（例如 `{xxxxxxxx-...}`） |

刪除此檔即回到預設（自動單位、所有實體網卡）。

## 專案結構
| 路徑 | 內容 |
| --- | --- |
| `main.cpp` | 視窗、圖表、Windows 網卡讀取 |
| `traffic.h` | 流量計算、介面篩選、縱軸刻度的純邏輯（不依賴 Qt／Windows） |
| `tests/traffic_tests.cpp` | `traffic.h` 單元測試 |
| `tests/verification.cpp` | 單位、介面、偏好、圖表的 Qt 驗證 |
| `build-and-test.ps1`／`.cmd` | 一鍵建置、測試、打包 |
| `LICENSE` | 本程式的 MIT 授權 |
| `licenses\` | 授權說明與第三方授權全文（打包時複製到 `dist\licenses`） |
| `app.ico`、`arrow-down.png`、`app_resources.qrc`、`app.rc` | 圖示與資源（`app.ico`、`arrow-down.png` 由作者以 AI／工具產生，與程式碼同樣以 MIT 授權發佈） |
| `RealtimeNetworkSpeed.pro` | qmake 專案（Qt Creator 可開啟） |

`build\`、`dist\`、`archives\` 由建置腳本產生，不在版本庫中。

## 建置
環境：Visual Studio 18 2026（MSVC 19.51）、Windows SDK 10.0.28000.0、Qt 6.12.0 官方版 MSVC 2022 x64、CMake 4.4。

只支援 Qt 官方安裝版：`licenses\` 的授權說明與原始碼提供方式是依官方未修補的 Qt 6.12.0 寫的。腳本會檢查 Qt 版本（`$expectedQt`），且拒絕含 GLib 的非官方建置。

**一鍵建置**：用 PowerShell 7 執行 `build-and-test.ps1`（或雙擊 `build-and-test.cmd`）。腳本會：
1. 尋找 Qt（`QT_DIR` 環境變數 → `D:\qt\6.*\msvc2022_64` → `C:\Qt\6.*\msvc2022_64`），並確認版本為 6.12.0
2. 在 `build\` 以 CMake 建置 Release（含測試）並執行 CTest
3. 打包到 `dist\`：Qt DLL（只留 Core／Gui／Widgets 與用得到的外掛）、MSVC 執行階段 DLL、`qt.conf`、`licenses\`
4. 以只含 Windows 系統資料夾的 PATH 啟動一次，進行基本啟動檢查（不等同乾淨 Windows 環境驗證）
5. 以上都通過後，把 `dist\` 打包成 `archives\RealtimeNetworkSpeed-v<版本>-win64-<建置日期>.zip`，原始碼打包成 `archives\RealtimeNetworkSpeed-v<版本>-source-<建置日期>.zip`

輸出同時寫入 `build-log.txt`。同一天重建會覆寫當天的壓縮檔；建置或測試失敗時不會產生壓縮檔。原始碼壓縮檔會納入根目錄的 `.cpp`／`.h` 與 `tests\`、`licenses\`；其他建置需要的檔案列在腳本的 `$sourceFiles`，新增時要一併加進清單。

**手動建置**：

```powershell
cmake -S . -B build -A x64 -DCMAKE_PREFIX_PATH=D:/qt/6.12.0/msvc2022_64 -DBUILD_VERIFICATION=ON
cmake --build build --config Release
$env:PATH = "D:\qt\6.12.0\msvc2022_64\bin;" + $env:PATH
ctest --test-dir build -C Release --output-on-failure
```

不需要測試時可省略 `-DBUILD_VERIFICATION=ON`；開啟時另需 Qt6 Test 模組。

windeployqt 若找不到 Visual Studio 的 Redist 檔案，腳本會用 vswhere 找到 VS，再從 `VC\Redist\MSVC\<版本>\x64\Microsoft.VC*.CRT` 複製 DLL。windeployqt 連帶帶入、程式用不到的 `Qt6Pdf.dll`、`Qt6Svg.dll`、`iconengines\`、`opengl32sw.dll`、`D3Dcompiler_47.dll` 會被腳本移除。

## 授權
- 本程式自己的程式碼與圖示以 MIT 授權發佈，Copyright (c) 2026 Kaido-MSVC（`LICENSE`）。
- Qt 6.12.0（官方版、未修改）依 LGPL-3.0-only 使用，以獨立 DLL 動態連結，可替換為自行修改或重新編譯的相容版本。完整對應原始碼 `qtbase-everywhere-src-6.12.0.tar.xz` 與程式一起放在 Release 附件。
- Qt 內建的 zlib、PCRE2、libpng、FreeType、HarfBuzz 等第三方程式碼，其著作權聲明與授權全文見 `licenses\Qt\Qt第三方元件.txt`（依 Qt SBOM 從官方 qtbase 原始碼的 `qt_attribution.json` 產生）。
- Microsoft Visual C++ 執行階段 DLL 依 Visual Studio 授權條款隨程式散布。
- 總覽見 `licenses\授權說明.txt`；程式內「關於／授權」按鈕也會顯示 Qt 授權聲明，並可開啟 licenses 資料夾。

再次散布時，請保留整個 `licenses\` 資料夾。更新 Qt 版本時，要一併更新 `licenses\` 與腳本中的 `$expectedQt`。
