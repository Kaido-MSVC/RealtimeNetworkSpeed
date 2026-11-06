param([string]$Makensis = 'C:\Program Files (x86)\NSIS\makensis.exe')
$ErrorActionPreference='Stop'
$root=$PSScriptRoot
$dist=Join-Path $root 'dist'
$header=Get-Content -LiteralPath (Join-Path $root 'version.h') -Raw
if($header -notmatch '#define APP_VERSION "([0-9.]+)"') { throw '找不到版本號' }
$version=$Matches[1]
$files=@(Get-ChildItem -LiteralPath $dist -File -Recurse | Sort-Object FullName)
if(-not $files.Count -or -not (Test-Path (Join-Path $dist 'RealtimeNetworkSpeed.exe'))) { throw '請先完成 build-and-test.ps1' }
$generated=Join-Path $root 'build/installer-payload.nsh'
$install=@('!macro InstallPayload')
$remove=@('!macro RemovePayload')
$dirs=@()
foreach($file in $files) {
    $rel=$file.FullName.Substring($dist.Length+1)
    if($rel -match '[\r\n"$]') { throw "不支援的封裝檔名：$rel" }
    $parent=Split-Path $rel
    $install+='SetOutPath "$INSTDIR'+$(if($parent){'\'+$parent})+'"'
    $install+='File "'+$file.FullName+'"'
    $remove+='Delete "$INSTDIR\'+$rel+'"'
    while($parent) { $dirs+=$parent; $parent=Split-Path $parent }
}
$install+='!macroend'; $remove+='!macroend'
$empty=@('!macro RemoveEmptyPayloadDirs')
$empty+=@($dirs | Sort-Object -Unique | Sort-Object Length -Descending | ForEach-Object { 'RMDir "$INSTDIR\'+$_+'"' })
$empty+='!macroend'
Set-Content -LiteralPath $generated -Value ($install+$remove+$empty) -Encoding utf8BOM
$out=Join-Path $root "archives/RealtimeNetworkSpeed-$version-Windows-x64-Setup.exe"
& $Makensis "/DAPP_VERSION=$version" "/DOUTPUT_FILE=$out" "/X!include `"$generated`"" (Join-Path $root 'installer/setup.nsi')
if($LASTEXITCODE -ne 0) { throw "NSIS 失敗：$LASTEXITCODE" }
Write-Output $out
