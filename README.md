# 即時網路速度（RealtimeNetworkSpeed）


**v1.3.0 新增 F10**：「清除歷史流量…」可按網卡／監控範圍及近 7／30 天、自訂日期或全部日期刪除。可先匯出 CSV，確認預設取消；與「清空本次統計」分開，清除後新的有效取樣繼續累積。已通過使用者本機驗收。
Windows x64 即時顯示網路卡下載／上傳速度的小工具。正式 CMake 建置使用 C++23／Qt 6 Widgets，透過 Windows `GetIfTable2`（iphlpapi）讀取網卡計數器，圖表以 QPainter 繪製，不依賴 Qt Charts。

作者：**Kaido-MSVC**。本程式完全在本機執行，不連網、不上傳任何資料。

最新版本：**v1.3.3**（2026-11-12）。[正式下載與全部附件](https://github.com/Kaido-MSVC/RealtimeNetworkSpeed/releases/tag/v1.3.3)。

### v1.3.3 建置可重現性與防護強化
- 正式建置腳本固定 MSVC 工具集 14.52.37025，每次 configure 都重新偵測編譯器，舊 build 快取不再造成失敗。
- 正式 EXE 啟用 Control Flow Guard 與 CET 相容；功能、資料格式、Qt 6.12.0 與隨附 DLL 均不變。詳見 [v1.3.3 發布驗證](RELEASE-v1.3.3.md)。
- The build script pins MSVC 14.52.37025 and always reconfigures fresh; the shipped EXE enables Control Flow Guard and CET compatibility. Features, data formats and bundled Qt DLLs are unchanged.

### v1.3.2 C++23 正式工具鏈升級
- 正式 CMake 建置升級至 C++23，MSVC 最低為 19.52（工具集 14.52）；修正舊 CMake 的標準旗標映射，避免誤用 c++latest，且不誤套到 clang-cl。
- Qt 保持 6.12.0，qmake 保留 C++17 備用路徑；既有監控、CSV、歷史及清除功能維持不變，不宣稱效能提升。
- 更新建置文件與部署驗證；詳見 [工具鏈紀錄](DEVELOPMENT-CXX23.md) 與 [v1.3.2 發布驗證](RELEASE-v1.3.2.md)。
- CMake builds now use C++23 with MSVC 14.52 or later. Qt remains at 6.12.0; the fallback qmake path remains C++17. Existing features are preserved; no performance improvement is claimed.

### v1.3.1 日期與歷史安全修補
- 執行中日期大幅跳動暫停歷史累積／修剪，校正後自動恢復；也可確認目前日期。正常關機跨多日重啟可繼續累積。
- 啟動日期前進超過 2 日且要修剪過期資料時，先在設定資料夾 `%APPDATA%\RealtimeNetworkSpeed` 建立 `traffic-history.json.date-recovery-*` 完整快照，成功後才修剪；手動確認日期也先建立快照。正常最多保留 3 份，清理失敗可能暫時超過並提示重試。
- 快照失敗時保留原歷史、主檔與備份，下次取樣自動重試；日期確認的提交或備份異常有明確提示。快照可能保留已清除資料，請自行管理。不要在程式執行時直接覆寫歷史檔。
- 真正日期／保存受阻發系統匣通知，同一段受阻通知一次，恢復後重設；Windows 必須支援並允許通知。取樣間隔不符僅略過該筆並顯示文字，不顯示日期確認按鈕或傳送受阻通知。
- 新增目前使用者安裝版，安裝／移除保留設定與歷史；升級前請退出舊版。詳見 [開發紀錄](DEVELOPMENT-v1.3.1.md) 與 [發布驗證](RELEASE-v1.3.1.md)。

### v1.3.0 全部功能版
- 依監控範圍分開統計今日、本月與每日有效流量；「歷史流量」提供 7／30 天圖及 7／30 天／全部保留歷史 CSV。
- 每日彙總以 JSON 自動保存於既有 INI 同目錄的 `traffic-history.json`，約每 60 秒及正常退出保存；QSaveFile 原子替換、有效備份與損壞檔保留。資料只代表程式有效監控期間，並非帳單用量；異常退出可能失去尚未保存的約 60 秒資料。
- 保留最多 365 個日曆日；跨日取樣依時間分配，時鐘跳動重新建立取樣基準。bytes 以字串保存精度。切換範圍或清空本次統計不刪除歷史。
- 即時曲線可選 60 秒、5 分鐘、30 分鐘，固定記憶體上限，空檔保持斷線，暫停時凍結。
- 可自行啟用 HKCU 登入啟動，預設關閉；登入時縮至系統匣。移動程式後須取消並重新勾選以更新路徑。
- 網卡斷線／恢復及讀取失敗通知預設關閉，三次連續確認後提示，穩定狀態不重複。
- 迷你視窗可調 50–100% 透明度、小／中／大字體及網卡／本次流量顯示，設定隨既有 INI 保存。
- v1.3.0 發布時保留 Qt 6.12.0／C++17／MSVC x64、Qt Network、.pro、既有授權及歷史封裝；不增加資料庫、服務、雲端或其他框架。現行原始碼的 C++23 正式建置需求見下方「建置」。

實作、資料格式、時間規則與測試限制見 [DEVELOPMENT-v1.3.0.md](DEVELOPMENT-v1.3.0.md)。使用者已親自驗收 F01～F10；正式建置與封裝紀錄見 [RELEASE-v1.3.0.md](RELEASE-v1.3.0.md)，版本摘要見 [CHANGELOG.md](CHANGELOG.md)。v1.3.x 以錯誤、安全及必要相容性修正為主。


### v1.2.1 穩定性與操作修正
- 主內容可捲動，小螢幕／高 DPI 下底部「暫停、清空、匯出」固定可見；較窄視窗可水平捲動內容。
- 逐筆紀錄採 deque，只保留最近 24 小時、最多 172800 筆；監控時逐次清理，暫停時每分鐘清理，匯出前再次清理。淘汰不影響本次累積、平均及峰值。
- 網卡切換、清空若有未匯出紀錄，提供「先匯出再繼續／捨棄並繼續／取消」，預設取消。取消存檔或存檔失敗會取消原操作並保留資料。
- 匯出與清除確認期間暫停取樣，關閉對話方塊後重新建立計數基準；該段時間不補算。不複製整份逐筆紀錄，仍使用 QSaveFile 原子寫入。
- 同一使用者／設定位置只執行一份，從其他資料夾再開也會喚醒原視窗。用獨立 instance lock 與限同一使用者的本機 named pipe，不傳送網路資料；v1.2.0 不具備此協定，升級前請先退出舊版。
- 系統匣單擊與雙擊均還原；主／迷你視窗位置及大小以 QSettings 記住，恢復時校正到可用畫面。
- Windows 版本資源補齊描述、作者、版權及原始檔名；初始速度為「—」；CSV 對 Tab、CR、LF 及其他 ASCII 控制字元加前置單引號。
- PowerShell 預設不暫停，支援 `-NoPause`、`-Pause` 與 `-BuildOnly`；`.cmd` 才預設要求暫停。`-BuildOnly` 只建置和測試，不改動 dist／archives。

CSV 新增欄位：`Retained_From,Retained_To,Retention_Hours,Retention_Max_Records,Discarded_Records`，每列明示本檔實際首末時間、24 小時／172800 筆上限與本次已淘汰筆數。既有累積、平均、峰值欄位仍涵蓋本次全部有效取樣，不能把它們解讀成僅 CSV 首末時間之內的統計。逐筆仍僅在記憶體中，退出會清除，需自行匯出。


### v1.2.0 新增功能
- 網卡選單可指定實體、VPN、虛擬介面；指定網卡消失仍保留 GUID，等待恢復，不自動監看其他網卡。
- 「網卡資訊」顯示目前範圍的名稱、IPv4／IPv6、MAC、連線速度與啟用／連線狀態；可捲動、選取及複製。
- 顯示本次平均與峰值。平均＝累積 bytes × 8 ÷ 有效取樣秒數；峰值為單次有效取樣最大 bit/s。主視窗數值與迷你數值可懸停查看本次統計。
- 「暫停監控」凍結速度、圖表與累積統計；恢復時重建計數器基準，排除暫停流量。
- 「清空本次統計」同時重設累積流量、平均、峰值、圖表及待匯出紀錄，保留設定；暫停中清空後仍維持暫停。
- 「匯出 CSV」手動輸出最近 24 小時保留的有效取樣紀錄，UTF-8 BOM、CRLF，速度固定為 bytes/s，累積量為 bytes，不受畫面單位影響。中文／逗號／引號／換行正確跳脫，公式樣式名稱會加上前置單引號。
- CSV 前六欄為 `Timestamp,Adapter,Download_Bps,Upload_Bps,Total_Download,Total_Upload`，後續為 `Adapter_ID,Sample_Seconds,Active_Seconds,Average_Download_Bps,Average_Upload_Bps,Peak_Download_Bps,Peak_Upload_Bps`。時間含本地時區偏移，bytes 整數不經浮點轉換。
- 讀取失敗、首次取樣、斷線、計數器重設及超過 2 秒空檔顯示「—」，該空檔不納入平均或 CSV；仍連線的其他介面可繼續累積。
- 逐筆紀錄只保存在記憶體；清空、切換監看範圍或結束程式會清除，需先手動匯出。CSV 不受圖表 60 秒限制，自 v1.2.1 起限制為最近 24 小時、最多 172800 筆。
- 沿用 v1.1.2 的獨立置頂迷你視窗、主視窗最小化／系統匣行為與原設定格式。

### v1.1.2 修正
- 主視窗最小化時隱藏工作列項目，從系統匣或迷你視窗還原後恢復一般工作列行為。
- 迷你視窗改為獨立置頂工具視窗，不占工作列；切換其他程式、主視窗最小化、縮至系統匣及還原時持續顯示。迷你視窗置頂不受主視窗的「視窗置頂」設定影響。
- 保留既有設定與流量監看功能；本機 UI 修正已由使用者親自驗收。

### v1.1.1 修正
- 主視窗最小化或縮至系統匣時，已開啟的迷你視窗會獨立保留顯示；還原主視窗不改變迷你視窗的顯示狀態。
- 保留迷你視窗原有標題列拖動、共同置頂與單位設定；可獨立關閉再開啟。在系統匣監看時關閉迷你視窗不會退出，完整退出時兩個視窗一併關閉。

### v1.1.0 新增功能
- 系統匣右鍵「還原／結束」，單擊或雙擊還原主視窗。「關閉時縮至系統匣」預設關閉，可記住選擇；系統匣不可用時直接退出。
- 「迷你視窗」同步顯示即時下載／上傳速度與所選單位，可獨立關閉及還原主視窗。
- 「視窗置頂」控制主視窗，保存至既有 INI 設定；自 v1.1.2 起迷你視窗固定獨立置頂。
- 本次監看累積下載／上傳／合計量，以十進位 MB 顯示，從有效位元組差值累加；重啟程式、手動重設或監看範圍切換時歸零。
- 計數器倒退、新介面及讀取失敗空檔不補算；取樣間隔超過 2 秒（例如休眠或長時間停頓）重新建立基準，該空檔不計入累積。因此本功能是有效取樣累積，並非帳單流量統計。
- 新設定鍵：`display/topmost`、`display/closeToTray`，均預設 false。

## 下載
從 [Releases](https://github.com/Kaido-MSVC/RealtimeNetworkSpeed/releases) 下載：

| 附件 | 內容 |
| --- | --- |
| `RealtimeNetworkSpeed-1.3.3-Windows-x64-Setup.exe` | 安裝目前使用者；不要求管理員。可從 Windows 已安裝應用程式移除，設定／歷史保留 |
| `RealtimeNetworkSpeed-1.3.3-Windows-x64-Portable.zip` | 免安裝版。完整解壓後執行 `RealtimeNetworkSpeed\RealtimeNetworkSpeed.exe` |
| `RealtimeNetworkSpeed-1.3.3-source.zip` | 本程式原始碼，內容與 `v1.3.3` Tag 相同 |
| `qtbase-everywhere-src-6.12.0.tar.xz` | 隨附 Qt DLL 的完整對應原始碼（Qt 官方原始碼包，未修改） |
| `SHA256SUMS.txt` | 上述附件的 SHA-256 |

Qt DLL、外掛與 Visual C++ 執行階段 DLL 都已隨附，不必另外安裝 Qt 或 VC++ 可轉散發套件。請保留整個資料夾，不要只複製 EXE。執行檔**未數位簽章**，Windows SmartScreen 可能會提示。

### 系統需求
- **文件支援範圍**：Windows 10 版本 1809 以上，或 Windows 11（x64）。這是 Qt 6.12 官方文件所列的支援平台。
- **實際測試結果**：只在 Windows 11 Home x64（26200）測試過，包含建置、兩組自動測試，以及只含 Windows 系統資料夾的 PATH 下的啟動檢查。沒有在 Windows 10 實機或虛擬機器測試。

## 功能
- 每 0.5 秒更新下載、上傳速度，圖表依實際時間可選最近 60 秒／5 分鐘／30 分鐘，短緩衝最多 120 筆、長緩衝最多 3600 筆；超過 2 秒的取樣間隔以斷線呈現。
- **網卡選擇**：「所有實體網卡」（預設）、個別實體／VPN／虛擬網卡，或「全部介面（含 VPN／虛擬網卡）」。選擇會記住；手動切換範圍時經確認或先匯出後清空本次統計與紀錄；拔插保留指定網卡，重連後重新建立基準。
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
| `display/topmost` | `true`／`false`，預設 `false`，控制主視窗置頂；迷你視窗固定獨立置頂 |
| `display/closeToTray` | `true`／`false`，預設 `false`；系統匣不可用時直接退出 |
| `window/mainGeometry`、`window/miniGeometry` | Qt 視窗位置／大小資料 |
| `display/adapter` | `physical`、`all` 或網卡 GUID（例如 `{xxxxxxxx-...}`） |

刪除此檔即回到預設（自動單位、所有實體網卡）。

## 專案結構
| 路徑 | 內容 |
| --- | --- |
| `main.cpp` | 視窗、圖表、Windows 網卡讀取 |
| `history.h` | 每日流量、JSON 原子保存／復原、篩選清除及歷史 CSV |
| `traffic.h` | 流量計算、介面篩選、縱軸刻度的純邏輯（不依賴 Qt／Windows） |
| `tests/traffic_tests.cpp` | `traffic.h` 單元測試 |
| `tests/verification.cpp` | 單位、介面、偏好、圖表的 Qt 驗證 |
| `build-and-test.ps1`／`.cmd` | 一鍵建置、測試、打包 |
| `LICENSE` | 本程式的 MIT 授權 |
| `licenses\` | 授權說明與第三方授權全文（打包時複製到 `dist\licenses`） |
| `app.ico`、`arrow-down.png`、`app_resources.qrc`、`app.rc` | 圖示與資源（`app.ico`、`arrow-down.png` 由作者以 AI／工具產生，與程式碼同樣以 MIT 授權發佈） |
| `RealtimeNetworkSpeed.pro` | 備用 qmake 專案（維持 C++17，Qt Creator 可開啟） |

`build\`、`dist\`、`archives\` 由建置腳本產生，不在版本庫中。

## 建置
**正式建置方式是 CMake，使用 C++23。** MSVC 最低需求為 **19.52（工具集 14.52）**，舊版本會在 CMake configure 時明確停止。這項版本限制僅針對真正的 MSVC，不會套用到 clang-cl 或其他 C++23 編譯器；其他編譯器未列入本次完整驗證範圍。

已驗證環境：Visual Studio 2026 18.12.0、MSVC 工具集 14.52.37025（cl.exe 19.52.37029.0）、Windows SDK 10.0.28000.0、Qt **6.12.0** 官方版 MSVC 2022 x64、CMake 4.4.4、Ninja 1.13.2、Release x64。升級沒有變更 Qt 或新增功能，驗證與限制見 [DEVELOPMENT-CXX23.md](DEVELOPMENT-CXX23.md)。本次正式版本為 v1.3.3。

`RealtimeNetworkSpeed.pro` 暫時維持 **C++17**，僅作為備用 qmake 建置路徑；本次 C++23 結論適用於正式 CMake 路徑，兩者的語言標準不同。Qt Creator 正式開發請開啟 `CMakeLists.txt`。

只支援 Qt 官方安裝版：`licenses\` 的授權說明與原始碼提供方式是依官方未修補的 Qt 6.12.0 寫的。腳本會檢查 Qt 版本（`$expectedQt`），且拒絕含 GLib 的非官方建置。

**一鍵建置**：用 PowerShell 7 執行 `build-and-test.ps1`（或雙擊 `build-and-test.cmd`）。腳本會：
1. 尋找 Qt（`QT_DIR` 環境變數 → `D:\qt\6.*\msvc2022_64` → `C:\Qt\6.*\msvc2022_64`），並確認版本為 6.12.0
2. 在 `build\` 以 CMake 建置 Release（含測試）並執行 CTest
3. 打包到 `dist\`：Qt DLL（Core／Gui／Widgets／Network 與用得到的外掛）、MSVC 執行階段 DLL、`qt.conf`、`licenses\`
4. 以只含 Windows 系統資料夾的 PATH 啟動使用相同 UI 程式碼的驗證程式，隔離設定／歷史並保存截圖（不啟動正式 EXE、不等同乾淨 Windows 環境驗證）
5. 以上都通過後，把 `dist\` 打包成 `archives\RealtimeNetworkSpeed-v<版本>-win64-<建置日期>.zip`，原始碼打包成 `archives\RealtimeNetworkSpeed-v<版本>-source-<建置日期>.zip`

安裝版另外執行 `pwsh -File build-installer.ps1`（需 NSIS 3.13）；版本自 `version.h` 讀取，依 dist 檔案生成精確移除清單，不遞迴刪除安裝資料夾。正式原始碼附件使用發布提交的 `git archive`。

輸出同時寫入 `build-log.txt`。同一天重建會覆寫當天的壓縮檔；建置或測試失敗時不會產生壓縮檔。原始碼壓縮檔會納入根目錄的 `.cpp`／`.h` 與 `tests\`、`licenses\`；其他建置需要的檔案列在腳本的 `$sourceFiles`，新增時要一併加進清單。

**手動建置**：

在指定 MSVC 14.52 的 x64 開發者 PowerShell 中執行，先將 `QT_DIR` 設為 Qt 6.12.0 的 `msvc2022_64` 安裝目錄。Ninja 依目前開發者環境選擇編譯器，不會自行切換到其他已安裝工具集。使用新目錄可保留既有建置：

```powershell
cmake -S . -B build/cxx23-release -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH="$env:QT_DIR" -DBUILD_VERIFICATION=ON
cmake --build build/cxx23-release --config Release
$env:PATH = "$env:QT_DIR\bin;" + $env:PATH
ctest --test-dir build/cxx23-release -C Release --output-on-failure
```

也可在相同開發者環境使用正式腳本執行隔離的 Ninja 建置與全部測試：

```powershell
pwsh -File build-and-test.ps1 -BuildOnly -Ninja -BuildDirectory build/cxx23-release
```

`-BuildDirectory` 與 `-Ninja` 僅供 `-BuildOnly` 使用，不執行打包；指定目錄時日誌也寫到該目錄，保留既有 `build-log.txt`。未指定這些選項時，一鍵建置／打包仍沿用既有 Visual Studio 產生器流程。

Visual Studio 產生器流程固定使用 MSVC 工具集 `version=14.52.37025`，VS 更新預設版本時不會自動換用；需要其他版本時以 `-Toolset version=<版本>` 指定。每次 configure 都使用 `cmake --fresh` 重新偵測編譯器，舊 `build` 快取指向已移除的工具集也不會失敗。正式 EXE 啟用 Control Flow Guard（`/guard:cf`）與 CET 相容（`/CETCOMPAT`）。

不需要測試時可省略 `-DBUILD_VERIFICATION=ON`；開啟時另需 Qt6 Test 模組。

windeployqt 若找不到 Visual Studio 的 Redist 檔案，腳本會用 vswhere 找到 VS，再從 `VC\Redist\MSVC\<版本>\x64\Microsoft.VC*.CRT` 複製 DLL，優先選用建置工具集對應的完整版本。windeployqt 連帶帶入、程式用不到的 `Qt6Pdf.dll`、`Qt6Svg.dll`、`iconengines\`、`opengl32sw.dll`、`D3Dcompiler_47.dll`、`dxcompiler.dll`、`dxil.dll` 會被腳本移除。

## 授權
- 本程式自己的程式碼與圖示以 MIT 授權發佈，Copyright (c) 2026 Kaido-MSVC（`LICENSE`）。
- Qt 6.12.0（官方版、未修改）依 LGPL-3.0-only 使用，以獨立 DLL 動態連結，可替換為自行修改或重新編譯的相容版本。完整對應原始碼 `qtbase-everywhere-src-6.12.0.tar.xz` 與程式一起放在 Release 附件。
- Qt 內建的 zlib、PCRE2、libpng、FreeType、HarfBuzz 等第三方程式碼，其著作權聲明與授權全文見 `licenses\Qt\Qt第三方元件.txt`（依 Qt SBOM 從官方 qtbase 原始碼的 `qt_attribution.json` 產生）。
- Microsoft Visual C++ 執行階段 DLL 依 Visual Studio 授權條款隨程式散布。
- 總覽見 `licenses\授權說明.txt`；程式內「關於／授權」按鈕也會顯示 Qt 授權聲明，並可開啟 licenses 資料夾。

再次散布時，請保留整個 `licenses\` 資料夾。更新 Qt 版本時，要一併更新 `licenses\` 與腳本中的 `$expectedQt`。
