# 版本紀錄

## v1.3.3 — 2026-11-12

- 正式腳本的 Visual Studio 產生器固定 MSVC 工具集 14.52.37025（可用 `-Toolset` 指定），CRT 備援依同一版本選擇。
- configure 一律 `--fresh`，避免舊 build 快取沿用已移除的 14.51 工具集而失敗。
- 正式 EXE 啟用 Control Flow Guard 與 CET 相容旗標；不變更 C++ 程式碼與功能。
- 發布驗證紀錄見 RELEASE-v1.3.3.md。
- English: The build script pins MSVC toolset 14.52.37025 (override with `-Toolset`), always reconfigures with `--fresh`, and the shipped EXE now enables Control Flow Guard and CET compatibility. No C++ source or feature changes.

## v1.3.2 — 2026-11-11

- 正式 CMake 建置改用 C++23；MSVC 最低版本為 19.52（工具集 14.52），舊版明確報錯。
- 僅真正的 MSVC 修正舊 CMake 的 C++23 旗標映射，保留 clang-cl 的自身設定及未來 CMake 的原生映射。
- Qt 保持 6.12.0；備用 qmake 路徑維持 C++17。沒有新增產品功能或宣稱效能提升。
- 正式腳本支援 `-BuildOnly -Ninja -BuildDirectory` 隔離建置與測試，保留既有產物。
- 更新建置文件與 Qt／VC++ Runtime 隔離部署驗證；相容性及發布紀錄見 DEVELOPMENT-CXX23.md、RELEASE-v1.3.2.md。既有主要功能及所有歷史 Tag／Release 保留。
- 封裝腳本同步排除 Widgets 軟體繪圖未使用的 DirectX shader compiler 備援 DLL，延續僅附必要執行依賴的發布方式。
- CRT 部署備援優先選擇建置工具集對應的完整 x64 執行階段，避免自動選到其他已安裝工具集。
- English: Official CMake builds use C++23 and MSVC 14.52 or later. CMake standard-flag mapping, older-MSVC rejection and clang-cl detection are improved. Build documentation and deployment validation are updated; Qt 6.12.0 and existing features are unchanged. No performance gain is claimed.

## v1.3.1 — 2026-11-06

日期與歷史安全修補，保留 v1.3.0 tag／Release。

- 執行中大幅時鐘跳動暫停歷史累積及修剪；校正後恢復，提供明確日期確認與系統匣受阻通知。
- 正常關機跨多日啟動繼續累積；會修剪過期資料時先建立完整 date-recovery 快照，設定資料夾內正常最多保留 3 份。快照失敗保留資料並自動重試；清理舊快照受阻時提示並於下次重試。
- 日期確認採先快照、再原子提交；提交前失敗保持原資料，備份還原失敗或提交後備份失敗有明確提示。
- 取樣間隔異常改為獨立文字提示，不再誤觸「確認歷史日期」或日期受阻通知。
- 新增目前使用者安裝版，保留可攜版；安裝／移除保留設定、歷史與其他檔案。

詳細行為、快照含舊資料的管理及限制見 DEVELOPMENT-v1.3.1.md；發布驗證見 RELEASE-v1.3.1.md。

## v1.3.0 — 2026-11-06

使用者已親自驗收全部功能版，保留 v1.2.1 既有功能與設定，未增加第三方框架。

| 編號 | 內容 |
| --- | --- |
| F01 | 按日期及監控範圍分開保存每日、今日、本月流量 |
| F02 | JSON 約 60 秒／退出保存、原子替換、備份及損壞檔保留、365 日保留 |
| F03 | 可選 Windows 登入自動啟動，預設關閉 |
| F04 | 60 秒、5 分鐘、30 分鐘即時曲線及固定記憶體上限 |
| F05 | 最近 7／30 天歷史流量圖 |
| F06 | 可選斷線、恢復及監控異常通知，防抖與去重 |
| F07 | 迷你視窗透明度、字體、網卡與本次流量顯示自訂 |
| F08 | 按日期與監控範圍匯出歷史 CSV；UTF-8 BOM、精確整數、公式防護 |
| F09 | 保留單一個體、系統匣、網卡資訊、逐筆 CSV 與既有設定並完成必要回歸 |
| F10 | 按網卡／範圍及日期或全部清除歷史；預設取消、可先匯出、不影響本次統計；防止舊備份復原已刪資料 |

歷史僅代表有效監控期間，非帳單用量；非正常退出可能失去尚未保存的約 60 秒。F10 清除不可復原，執行前可先匯出。技術與測試詳見 DEVELOPMENT-v1.3.0.md、RELEASE-v1.3.0.md。

## v1.2.1

穩定性、保留期限、CSV 保護、單一個體、視窗行為修正；詳見 RELEASE-v1.2.1.md。

## v1.2.0 與更早版本

詳見 README.md 與各版 DEVELOPMENT／RELEASE 紀錄；所有舊 tag 與 Release 保留。
