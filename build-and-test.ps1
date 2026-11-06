param([switch]$Pause, [switch]$NoPause, [switch]$BuildOnly)
# 建置、測試並打包「即時網路速度」。輸出同時寫入 build-log.txt。
# 可用環境變數 QT_DIR 指定 Qt（例如 C:\Qt\6.12.0\msvc2022_64）。
$ErrorActionPreference = 'Stop'
$root = $PSScriptRoot
$log = Join-Path $root 'build-log.txt'
Set-Content -Path $log -Value "=== $(Get-Date -Format 'yyyy-MM-dd HH:mm:ss') 建置開始" -Encoding utf8

function Log([string]$text) { $text | Tee-Object -FilePath $log -Append }
function Run([string]$exe, [string[]]$argList) {
    Log "> $exe $($argList -join ' ')"
    & $exe @argList 2>&1 | ForEach-Object { "$_" } | Tee-Object -FilePath $log -Append
    if ($LASTEXITCODE -ne 0) { throw "$exe 結束代碼 $LASTEXITCODE" }
}

$failed = $false
try {
    $qt = $env:QT_DIR
    if (-not $qt) {
        # 只找 Qt 官方安裝版：授權說明與原始碼提供方式是依官方未修補的 Qt 寫的
        $qt = Get-ChildItem 'D:\qt', 'C:\Qt' -Directory -ErrorAction SilentlyContinue |
            Where-Object { $_.Name -match '^6\.12\.\d+$' } |
            Sort-Object { [version]$_.Name } -Descending |
            ForEach-Object { Join-Path $_.FullName 'msvc2022_64' } |
            Where-Object { Test-Path (Join-Path $_ 'lib\cmake\Qt6\Qt6Config.cmake') } |
            Select-Object -First 1
    }
    if (-not $qt) { throw '找不到 Qt 6 官方 MSVC x64（D:\qt 或 C:\Qt），請設定環境變數 QT_DIR 後再執行' }
    # 發行包內的 Qt 版本必須與 licenses\授權說明.txt 一致
    $expectedQt = '6.12.0'
    $qtVersion = (& (Join-Path $qt 'bin\qtpaths6.exe') --qt-query QT_VERSION 2>$null | Select-Object -First 1)
    if ($qtVersion -notmatch '^6\.12\.\d+$') { throw "僅支援 Qt 6.12.x，目前為 $qtVersion" }
    # 僅建置允許 6.12.x；打包仍須與既有授權材料所記錄的版本一致。
    if (-not $BuildOnly -and $qtVersion -ne $expectedQt) { throw "Qt 版本是 $qtVersion，授權說明寫的是 $expectedQt；更新版本時要一併更新授權說明與 `$expectedQt" }
    if (Test-Path (Join-Path $qt 'bin\glib-2.0-0.dll')) { throw "$qt 不是 Qt 官方版（含 GLib），授權說明不適用" }
    Log "Qt：$qt"
    Log "PowerShell：$($PSVersionTable.PSVersion)"
    Run 'cmake' @('--version')

    $build = Join-Path $root 'build'
    Run 'cmake' @('-S', $root, '-B', $build, '-A', 'x64', "-DCMAKE_PREFIX_PATH=$qt", '-DBUILD_VERIFICATION=ON')
    Run 'cmake' @('--build', $build, '--config', 'Release')

    $env:PATH = (Join-Path $qt 'bin') + ';' + $env:PATH
    Run 'ctest' @('--test-dir', $build, '-C', 'Release', '--output-on-failure')

    if ($BuildOnly) { Log '僅建置與測試完成；未打包'; exit 0 }

    # 打包到 dist（不覆寫 app\ 或已安裝的版本）
    $dist = Join-Path $root 'dist'
    if ([IO.Path]::GetFullPath($dist) -ne [IO.Path]::Combine([IO.Path]::GetFullPath($root), 'dist')) { throw 'dist 路徑檢查失敗' }
    if (Test-Path $dist) { Remove-Item -LiteralPath $dist -Recurse -Force }
    New-Item -ItemType Directory -Path $dist | Out-Null
    $exe = Join-Path $dist 'RealtimeNetworkSpeed.exe'
    Copy-Item (Join-Path $build 'Release\RealtimeNetworkSpeed.exe') $exe
    # windeployqt 需要 VCINSTALLDIR 才能部署 MSVC 執行階段 DLL
    if (-not $env:VCINSTALLDIR) {
        $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
        if (Test-Path $vswhere) {
            $vs = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
            if ($vs) { $env:VCINSTALLDIR = (Join-Path $vs 'VC') + '\' }
        }
    }
    Log "VCINSTALLDIR：$env:VCINSTALLDIR"
    Run (Join-Path $qt 'bin\windeployqt.exe') @('--release', '--no-translations', '--compiler-runtime', $exe)
    # windeployqt 有時找錯 Redist 版本資料夾：改從實際存在的最新版 x64 CRT 資料夾複製
    if (-not (Test-Path (Join-Path $dist 'vcruntime140.dll')) -and $env:VCINSTALLDIR) {
        $redistRoot = Join-Path $env:VCINSTALLDIR 'Redist\MSVC'
        $crt = Get-ChildItem $redistRoot -Directory -ErrorAction SilentlyContinue |
            Where-Object { $_.Name -match '^\d+\.\d+\.\d+$' } |
            Sort-Object { [version]$_.Name } -Descending |
            ForEach-Object { Get-ChildItem (Join-Path $_.FullName 'x64') -Directory -Filter 'Microsoft.VC*.CRT' -ErrorAction SilentlyContinue } |
            Select-Object -First 1
        if ($crt) {
            Log "複製 MSVC 執行階段：$($crt.FullName)"
            Copy-Item (Join-Path $crt.FullName '*.dll') $dist
        } else {
            Log "在 $redistRoot 找不到 x64 CRT 資料夾"
        }
    }
    if (-not (Test-Path (Join-Path $dist 'vcruntime140.dll'))) { throw '打包缺少 MSVC 執行階段 DLL（vcruntime140.dll）' }
    Log "dist 內的執行階段：$((Get-ChildItem $dist -Filter '*140*.dll' | ForEach-Object Name) -join ', ')"

    # 只保留程式用得到的 Qt 元件（程式使用 Core／Gui／Widgets／Network（本機介面資訊）；圖示只需要 ICO 外掛）
    # Qt6Pdf（含 PDFium）、Qt6Svg、iconengines 由 windeployqt 依外掛連帶帶入；
    # opengl32sw.dll（Mesa）、D3Dcompiler_47.dll 是 OpenGL／D3D 備援，Widgets 軟體繪圖用不到
    foreach ($unused in @('tls', 'networkinformation', 'generic', 'Qt6Pdf.dll', 'Qt6Svg.dll',
                          'iconengines', 'opengl32sw.dll', 'D3Dcompiler_47.dll')) {
        $path = Join-Path $dist $unused
        if (Test-Path $path) { Remove-Item $path -Recurse -Force; Log "移除未使用：$unused" }
    }
    Get-ChildItem (Join-Path $dist 'imageformats') -Filter '*.dll' -ErrorAction SilentlyContinue |
        Where-Object { $_.Name -ne 'qico.dll' } | ForEach-Object { Remove-Item $_.FullName -Force }

    # 官方 Qt 的 zlib、PCRE2、libpng、FreeType、HarfBuzz 等已內建在 Qt DLL 中，不需另附 DLL
    Log "dist 內容：$((Get-ChildItem $dist -Recurse -File | ForEach-Object { $_.FullName.Substring($dist.Length + 1) }) -join ', ')"

    # 讓 Qt 只從程式資料夾找外掛，不會用到建置環境的 Qt
    Set-Content -Path (Join-Path $dist 'qt.conf') -Value "[Paths]`r`nPrefix = .`r`nPlugins = ." -Encoding ascii

    # 授權說明
    Copy-Item (Join-Path $root 'licenses') (Join-Path $dist 'licenses') -Recurse
    Log '已複製 licenses 資料夾'

    foreach ($required in @('platforms\qwindows.dll', 'styles\qmodernwindowsstyle.dll', 'imageformats\qico.dll')) {
        if (-not (Test-Path (Join-Path $dist $required))) { throw "打包缺少 $required" }
    }

    # 以相同 UI 程式碼的驗證執行檔檢查部署；QSettings 全部隔離，不啟動使用者資料。
    $smoke = Join-Path $root ('build/packaged-smoke-' + [guid]::NewGuid().ToString('N'))
    if (Test-Path $smoke) { throw "請先保留或移除上次隔離驗證目錄：$smoke" }
    Copy-Item $dist $smoke -Recurse
    Copy-Item (Join-Path $build 'Release/RealtimeNetworkSpeedVerification.exe') $smoke
    # 驗證程式另需 Qt Test，正式套件不附此 DLL。
    Copy-Item (Join-Path $qt 'bin/Qt6Test.dll') $smoke
    $savedPath = $env:PATH
    $env:PATH = "$env:SystemRoot\System32;$env:SystemRoot"
    try {
        Push-Location $smoke
        try { Run (Join-Path $smoke 'RealtimeNetworkSpeedVerification.exe') @('--packaged-smoke', (Join-Path $build 'packaged-smoke.png')) }
        finally { Pop-Location }
    } finally { $env:PATH = $savedPath }
    Log '乾淨 PATH 隔離 UI 啟動測試：通過（未開啟使用者設定或歷史）'

    # 測試全部通過才打包程式與原始碼壓縮檔（同一天重建會覆寫當天的壓縮檔，不動舊版壓縮檔）
    $archives = Join-Path $root 'archives'
    New-Item -ItemType Directory -Force -Path $archives | Out-Null
    $stamp = Get-Date -Format 'yyyy-MM-dd'
    $versionHeader = Get-Content -LiteralPath (Join-Path $root 'version.h') -Raw
    if ($versionHeader -notmatch '#define APP_VERSION "([0-9.]+)"') { throw 'version.h 缺少版本號' }
    $appVersion = $Matches[1]
    Add-Type -AssemblyName System.IO.Compression, System.IO.Compression.FileSystem
    # $baseDir 底下的 $relPaths 依相對路徑放進壓縮檔的 $topFolder 資料夾；先寫 .tmp，完成才改名
    function New-Archive([string]$zipPath, [string]$topFolder, [string]$baseDir, [string[]]$relPaths) {
        $tmpZip = "$zipPath.tmp"
        if (Test-Path $tmpZip) { Remove-Item $tmpZip -Force }
        $zip = [IO.Compression.ZipFile]::Open($tmpZip, 'Create')
        try {
            foreach ($rel in $relPaths) {
                $entry = "$topFolder/" + $rel.Replace('\', '/')
                [void][IO.Compression.ZipFileExtensions]::CreateEntryFromFile($zip, (Join-Path $baseDir $rel), $entry, 'Optimal')
            }
        }
        finally { $zip.Dispose() }
        Move-Item $tmpZip $zipPath -Force
        Log "壓縮檔：$zipPath（$($relPaths.Count) 個檔案，$([math]::Round((Get-Item $zipPath).Length / 1MB, 2)) MB）"
    }
    function Get-RelativeFiles([string]$baseDir, [string[]]$folders) {
        foreach ($folder in $folders) {
            Get-ChildItem (Join-Path $baseDir $folder) -Recurse -File | ForEach-Object { $_.FullName.Substring($baseDir.Length + 1) }
        }
    }

    New-Archive (Join-Path $archives "RealtimeNetworkSpeed-v$appVersion-win64-$stamp.zip") 'RealtimeNetworkSpeed' $dist @(Get-RelativeFiles $dist @('.'))

    # 原始碼：專案根目錄的 .cpp／.h 自動納入（不往下找，避免包進 build、backup 等資料夾）；
    # 其他建置需要的檔案列在下面，新增資源或設定檔時要一併加進清單
    $sourceFiles = @('main.cpp', 'CMakeLists.txt', 'RealtimeNetworkSpeed.pro', 'app.rc', 'app.ico',
                     'app_resources.qrc', 'arrow-down.png', 'LICENSE', 'README.md', 'build-and-test.ps1', 'build-and-test.cmd', 'build-installer.ps1', 'CHANGELOG.md', 'DEVELOPMENT-v1.3.1.md', 'RELEASE-v1.3.1.md')
    foreach ($file in $sourceFiles) { if (-not (Test-Path (Join-Path $root $file))) { throw "原始碼缺少 $file" } }
    $sourceFiles += Get-ChildItem $root -File | Where-Object { $_.Extension -in '.cpp', '.h' } | ForEach-Object Name
    $sourceFiles += Get-RelativeFiles $root @('tests', 'licenses', 'installer')
    $sourceFiles = @($sourceFiles | Sort-Object -Unique)
    New-Archive (Join-Path $archives "RealtimeNetworkSpeed-v$appVersion-source-$stamp.zip") 'RealtimeNetworkSpeed-source' $root $sourceFiles
    Log "完成：$exe"
}
catch {
    $failed = $true
    Log "失敗：$_"
}
Log "=== $(Get-Date -Format 'yyyy-MM-dd HH:mm:ss') 結束（$(if ($failed) { '失敗' } else { '成功' })）"
if ($Pause -and -not $NoPause -and [Environment]::UserInteractive -and -not [Console]::IsInputRedirected) { Read-Host '按 Enter 關閉視窗' }
if ($failed) { exit 1 }
exit 0
