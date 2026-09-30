param(
    [Parameter(Mandatory = $true)][string]$Python,
    [string]$SevenZip = 'C:/Program Files/7-Zip',
    [string]$Output = 'D:/Git/mixxx/dist/ohos-migration'
)
$ErrorActionPreference = 'Stop'
$toolDirectory = $PSScriptRoot
& $Python -m PyInstaller --noconfirm --clean --windowed --onedir --name PomeloMixxxMigration --distpath $Output --workpath "$Output/build" --specpath "$Output/build" --add-data "$SevenZip/7z.exe;7zip" --add-data "$SevenZip/7z.dll;7zip" --add-data "$SevenZip/License.txt;7zip" "$toolDirectory/migration_gui.py"
if ($LASTEXITCODE -ne 0) { throw 'PC tool build failed' }
Copy-Item -LiteralPath "$toolDirectory/README.md" -Destination "$Output/PomeloMixxxMigration/README.md"
Compress-Archive -LiteralPath "$Output/PomeloMixxxMigration" -DestinationPath "$Output/PomeloMixxxMigration-portable.zip" -Force
