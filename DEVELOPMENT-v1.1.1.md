# v1.1.1 開發紀錄（2026-11-04）

## 範圍與環境
- 以已發布 v1.1.0（f60030d9e71c2f3a5d260bcec0976eb0c293eeb5）為基礎，直接修改原專案，未建立工作副本。
- C++17、Qt Widgets 6.12.0 官方版、Visual Studio 18 2026 / MSVC x64 Release；未增加相依或設定鍵。

## 修正
- 根因是 `QWidget(this, Qt::Window)` 仍有主視窗 parent，在 Windows 建立 owner 關係，最小化時受 owner 牽動。
- 改為無 parent 的 `Qt::Window`，沒有主視窗的 HWND owner；不增加 show/hide 事件連動或改用 Tool 視窗。
- 明確複製原有樣式，保留標題列拖動、單位同步、置頂旗標與既有設定。
- 迷你視窗停用 `WA_QuitOnClose`，主視窗藏到系統匣後，關閉最後可見的迷你視窗不會誤退監看。
- `aboutToQuit` 隱藏迷你視窗，SpeedWindow 解構時釋放它，確保完整退出時不殘留獨立視窗。
- 版本與 Windows 檔案資源更新為 1.1.1，README 更新修正說明及正式附件名稱。
- 參考 Context7 與 Qt 6.12 官方 QWidget 文件：https://doc.qt.io/qt-6.12/qwidget.html 。

## 驗證
- MSVC x64 Release 主程式及兩組驗證程式建置成功；CTest 2/2 通過（traffic-logic、units-and-ui）。
- 在 Windows 原生平台以 `GW_OWNER`、`IsWindowVisible`、`IsIconic` 驗證沒有 owner，以及置頂開／關時主視窗最小化、還原不影響迷你視窗可見性與位置。
- 系統匣可用，實際 close 流程驗證主視窗隱藏而迷你視窗保留、隱藏時持續取樣、迷你視窗關閉與重新開啟，以及迷你按鈕／系統匣選單還原。
- 獨立子程序執行真實 Qt 事件循環，驗證最後可見迷你視窗關閉仍繼續監看、主視窗完整關閉、系統匣結束；確認 aboutToQuit 隱藏迷你視窗，另驗證主視窗解構會刪除迷你視窗。
- 保留既有流量、單位、設定重讀、圖表及網卡範圍回歸。

## 發行驗證與限制
- 正式 dist 使用既有 windeployqt 與裁剪流程重新產生，附 Qt 6.12.0 Core／Gui／Widgets、必要外掛、MSVC 執行階段、qt.conf 與完整 licenses。
- 發布前核對執行檔版本、Qt／授權檔雜湊、可攜 ZIP 與 dist 內容、原始碼 ZIP 與發布提交內容，以及附件 SHA256。
- Qt 完整對應原始碼沿用未修改官方 qtbase-everywhere-src-6.12.0.tar.xz；SHA256：a951bd163c7b80fc6b8c88d7668fb56abf91c152373e13c10666763238131307。
- 執行檔未數位簽章。未做乾淨虛擬機、Windows 10 實機或長時間壓力測試；僅含系統目錄 PATH 的啟動檢查不代表乾淨 Windows 驗證。
- 本次以 Windows 原生自動化視窗檢查驗證操作流程，未額外逐項人工滑鼠操作。原有 backup／archives 保留。
- 實際提交、Tag 及附件雜湊以 GitHub v1.1.1 Release 與 SHA256SUMS.txt 為準。
