# v1.1.2 開發與發布紀錄（2026-11-04）

- 基於已發布 v1.1.1（5f394b8fe196152b2f702089bda34ab047a922a7），僅發布使用者已親自驗收的兩項本機 UI 修正及其回歸測試。
- 主視窗最小化且系統匣可用時，待狀態切換完成後隱藏；立即還原不會被延後的隱藏覆蓋。
- 迷你視窗保持無 parent／owner，使用獨立置頂 Tool 視窗；主視窗置頂設定不再控制迷你視窗。
- C++17、Qt Widgets 6.12.0、Visual Studio 18 2026，MSVC x64 Release；未增加相依、功能或設定鍵。
- 發布檢查：MSVC x64 Release、CTest 兩組回歸、正式 dist 的系統 PATH 啟動、可攜 ZIP／dist 逐檔比對、來源 ZIP／提交逐檔比對、授權與 Qt DLL／官方安裝內容比對、遠端附件 SHA256。
- Qt 完整對應原始碼沿用未修改的 qtbase-everywhere-src-6.12.0.tar.xz，SHA256：a951bd163c7b80fc6b8c88d7668fb56abf91c152373e13c10666763238131307。
- 未數位簽章；未做乾淨虛擬機、Windows 10 實機或長時間壓力測試。使用者已完成本機 UI 人工驗收。
- 保留 D 槽原專案與既有封存，不建立工作副本、不改寫既有 tag。實際提交及附件雜湊以 v1.1.2 Release 與 SHA256SUMS.txt 為準。
