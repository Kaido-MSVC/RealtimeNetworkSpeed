# v1.2.0 發布驗證紀錄

日期：2026-11-05（Asia/Taipei）

使用者已親自驗收本機 v1.2.0，並授權完整發布 GitHub。

- 環境：C++17、Qt 6.12.0 官方 MSVC 2022 x64、Visual Studio 18 2026、MSVC 19.51.36260.0、Windows SDK 10.0.28000.0、CMake 4.4.4。
- Release 配置與建置成功，CTest traffic-logic、units-and-ui 2/2 通過（7.20 秒）。
- 既有腳本完成部署；僅使用 Windows 系統 PATH 的基本啟動檢查通過。
- 程式及 Windows 檔案／產品版本皆為 1.2.0，執行檔為 AMD64；關於視窗使用 version.h 的 APP_VERSION。
- README 更新為正式 v1.2.0 下載資訊；Qt Network 授權聲明包含 Public Suffix List／libpsl。LGPL／GPL 全文由 D:\licenses 複製，未修改範本。
- 可攜 ZIP 包含完整 dist；發布原始碼 ZIP 使用 git archive，與發布提交完全一致。
- 依既有慣例隨附 Qt 6.12.0 官方 qtbase 原始碼包，SHA-256 為 a951bd163c7b80fc6b8c88d7668fb56abf91c152373e13c10666763238131307。
- SHA256SUMS.txt 列出三個發布附件的雜湊。build、dist、archives、備份及二進位產物不提交版本庫。
- 舊 dist 已保留至 backup/dist-before-v1.2.0-20261105-141159，舊版壓縮檔不變。

限制：執行檔未數位簽章。VPN 切換及網卡拔插採模擬驗證；未於 Windows 10、乾淨電腦、不同 DPI／多螢幕實測，也未另外操作原生儲存對話框全流程或 Excel 開啟 CSV。未進行長時間壓力測試。前次獨立 CSV 解析驗證已通過，本次不無故重跑。

逐筆資料僅存在記憶體中，清空、切換監看範圍或結束前需手動匯出。全部介面模式可能重複計入 VPN 與實體介面流量，累積數據不是帳單用量。
