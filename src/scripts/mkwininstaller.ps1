param (
    [Parameter(Mandatory=$true)][string]$version
)

$target="safewallet-v$version"

Remove-Item -Path release/wininstaller -Recurse -ErrorAction Ignore  | Out-Null
New-Item release/wininstaller -itemtype directory                    | Out-Null

Copy-Item release/$target/safewallet.exe     release/wininstaller/
Copy-Item release/$target/LICENSE           release/wininstaller/
Copy-Item release/$target/README.md         release/wininstaller/
Copy-Item release/$target/safecoind.exe        release/wininstaller/
Copy-Item release/$target/safecoin-cli.exe     release/wininstaller/

Get-Content src/scripts/safe-qt-wallet.wxs | ForEach-Object { $_ -replace "RELEASE_VERSION", "$version" } | Out-File -Encoding utf8 release/wininstaller/safe-qt-wallet.wxs

candle.exe release/wininstaller/safe-qt-wallet.wxs -o release/wininstaller/safe-qt-wallet.wixobj 
if (!$?) {
    exit 1;
}

light.exe -ext WixUIExtension -cultures:en-us release/wininstaller/safe-qt-wallet.wixobj -out release/wininstaller/safewallet.msi 
if (!$?) {
    exit 1;
}

New-Item artifacts -itemtype directory -Force | Out-Null
Copy-Item release/wininstaller/safewallet.msi ./artifacts/Windows-installer-$target.msi