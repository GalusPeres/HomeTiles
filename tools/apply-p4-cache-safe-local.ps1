# Injects the cache-safe P4 display/camera objects
# (tools/esp-idf-3.3.7-p4-cache-safe) into both P4 variants of the local
# Arduino ESP32 3.3.7 installation and verifies every archive member. The
# first run keeps a copy of each stock archive next to it.
$ErrorActionPreference = 'Stop'

$coreVersion = '3.3.7'
$arduinoEsp32 = Join-Path $env:LOCALAPPDATA 'Arduino15\packages\esp32'
$ar = Get-ChildItem (Join-Path $arduinoEsp32 'tools') -Recurse -Filter 'riscv32-esp-elf-ar.exe' |
    Select-Object -First 1 -ExpandProperty FullName
if (-not $ar) {
    throw 'riscv32-esp-elf-ar.exe was not found in the Arduino ESP32 installation.'
}

$repoRoot = Split-Path $PSScriptRoot -Parent
$fixDirectory = Join-Path $repoRoot 'tools\esp-idf-3.3.7-p4-cache-safe'
$archiveByObject = [ordered]@{
    'esp_lcd_panel_dpi.c.obj' = 'libesp_lcd.a'
    'dw_gdma.c.obj'           = 'libesp_hw_support.a'
    'esp_cam_ctlr_csi.c.obj'  = 'libesp_driver_cam.a'
}

foreach ($variant in @('esp32p4-libs', 'esp32p4_es-libs')) {
    $changed = $false
    foreach ($objectName in $archiveByObject.Keys) {
        $object = Join-Path (Join-Path $fixDirectory $variant) $objectName
        if (-not (Test-Path -LiteralPath $object)) {
            throw "Cache-safe object not found: $object"
        }
        $archive = Join-Path $arduinoEsp32 "tools\$variant\$coreVersion\lib\$($archiveByObject[$objectName])"
        if (-not (Test-Path -LiteralPath $archive)) {
            throw "Archive not found: $archive"
        }
        $backup = "$archive.hometiles-stock-backup"
        if (-not (Test-Path -LiteralPath $backup)) {
            Copy-Item -LiteralPath $archive -Destination $backup
        }

        $expected = (Get-FileHash -Algorithm SHA256 -LiteralPath $object).Hash
        $verifyDirectory = Join-Path $env:TEMP ("hometiles-p4-cache-safe-" + [Guid]::NewGuid())
        New-Item -ItemType Directory -Path $verifyDirectory | Out-Null
        Push-Location $verifyDirectory
        try {
            & $ar x $archive $objectName
            $actual = if (Test-Path -LiteralPath $objectName) {
                (Get-FileHash -Algorithm SHA256 -LiteralPath $objectName).Hash
            } else {
                ''
            }
            if ($actual -ne $expected) {
                & $ar rs $archive $object
                if ($LASTEXITCODE -ne 0) {
                    throw "Failed to inject $objectName into $archive"
                }
                Remove-Item -LiteralPath $objectName -Force -ErrorAction SilentlyContinue
                & $ar x $archive $objectName
                if ($LASTEXITCODE -ne 0 -or
                    (Get-FileHash -Algorithm SHA256 -LiteralPath $objectName).Hash -ne $expected) {
                    throw "Verification failed for $objectName in $archive"
                }
                $changed = $true
            }
        }
        finally {
            Pop-Location
            Remove-Item -LiteralPath $verifyDirectory -Recurse -Force
        }
    }
    $status = if ($changed) { 'patched and verified' } else { 'already patched' }
    Write-Host "P4 cache-safe display/camera objects $status ($variant)"
}
