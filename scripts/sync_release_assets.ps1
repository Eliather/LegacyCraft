# ============================================================
#  sync_release_assets.ps1
#  Copia todos los assets, librerias y estructura completa de
#  LegacyCraft al directorio destino especificado (ej. build o dist)
# ============================================================

param(
    [Parameter(Mandatory=$true)]
    [string]$TargetDir
)

$BackupRelease = "C:\Users\danie\Documents\Trabajos\LegacyCraft_backup\x64\Release"
$RepoRoot      = Split-Path $PSScriptRoot -Parent

if (-not (Test-Path $TargetDir)) {
    New-Item -ItemType Directory -Path $TargetDir -Force | Out-Null
}

Write-Host "Sincronizando estructura completa a: $TargetDir" -ForegroundColor Cyan

# 1. DLLs y archivos raiz
$RootFiles = @("iggy_w64.dll", "mss64.dll", "sound_init.txt")
foreach ($rf in $RootFiles) {
    $srcPath = Join-Path $BackupRelease $rf
    if (Test-Path $srcPath) {
        Copy-Item -Path $srcPath -Destination $TargetDir -Force
        Write-Host "  Copiado raiz: $rf"
    }
}

# 2. redist64
$RedistSrc = Join-Path $BackupRelease "redist64"
$RedistDst = Join-Path $TargetDir "redist64"
if (Test-Path $RedistSrc) {
    Copy-Item -Path $RedistSrc -Destination $TargetDir -Recurse -Force
    Write-Host "  Copiado: redist64/"
}

# 3. Skins
$SkinsSrc = Join-Path $BackupRelease "Skins"
$SkinsDst = Join-Path $TargetDir "Skins"
if (Test-Path $SkinsSrc) {
    Copy-Item -Path $SkinsSrc -Destination $TargetDir -Recurse -Force
    Write-Host "  Copiado: Skins/"
}

# 4. Saves
$SavesSrc = Join-Path $BackupRelease "Saves"
$SavesDst = Join-Path $TargetDir "Saves"
if (Test-Path $SavesSrc) {
    Copy-Item -Path $SavesSrc -Destination $TargetDir -Recurse -Force
    Write-Host "  Copiado: Saves/"
}

# 5. Sound (musica y soundbank)
$SoundSrc = Join-Path $BackupRelease "Sound"
$SoundDst = Join-Path $TargetDir "Sound"
if (Test-Path $SoundSrc) {
    Copy-Item -Path $SoundSrc -Destination $TargetDir -Recurse -Force
    Write-Host "  Copiado: Sound/"
}
# Asegurar que Minecraft.msscmp este en Sound/
$MsscmpSrc = Join-Path $BackupRelease "Common\Sound\Minecraft.msscmp"
if (Test-Path $MsscmpSrc) {
    Copy-Item -Path $MsscmpSrc -Destination "$SoundDst\Minecraft.msscmp" -Force
}

# 6. Windows64 (GameConfig y GameHDD)
$Win64Dst = Join-Path $TargetDir "Windows64"
New-Item -ItemType Directory -Path $Win64Dst -Force | Out-Null
$GameConfigSrc = Join-Path $BackupRelease "Windows64\GameConfig"
if (Test-Path $GameConfigSrc) {
    Copy-Item -Path $GameConfigSrc -Destination $Win64Dst -Recurse -Force
    Write-Host "  Copiado: Windows64/GameConfig/"
}
$GameHDDSrc = Join-Path $BackupRelease "Windows64\GameHDD"
if (Test-Path $GameHDDSrc) {
    Copy-Item -Path $GameHDDSrc -Destination $Win64Dst -Recurse -Force
    Write-Host "  Copiado: Windows64/GameHDD/"
}

# 7. Windows64Media (DLC, loc, Media)
$WinMediaSrc = Join-Path $BackupRelease "Windows64Media"
if (Test-Path $WinMediaSrc) {
    Copy-Item -Path $WinMediaSrc -Destination $TargetDir -Recurse -Force
    Write-Host "  Copiado: Windows64Media/"
}

# 8. Common (Media, res, Sound)
$CommonDst = Join-Path $TargetDir "Common"
New-Item -ItemType Directory -Path $CommonDst -Force | Out-Null

$CommonMediaSrc = Join-Path $RepoRoot "Minecraft.Client\Common\Media"
if (-not (Test-Path $CommonMediaSrc)) {
    $CommonMediaSrc = Join-Path $BackupRelease "Common\Media"
}
$CommonMediaDst = Join-Path $CommonDst "Media"
if (Test-Path $CommonMediaSrc) {
    $NestedMedia = Join-Path $CommonMediaDst "Media"
    if (Test-Path $NestedMedia) {
        Remove-Item -Path $NestedMedia -Recurse -Force
    }
    New-Item -ItemType Directory -Path $CommonMediaDst -Force | Out-Null
    Copy-Item -Path "$CommonMediaSrc\*" -Destination $CommonMediaDst -Recurse -Force
    Write-Host "  Copiado: Common/Media/"
}

$CommonResSrc = Join-Path $RepoRoot "Minecraft.Client\Common\res"
if (-not (Test-Path $CommonResSrc)) {
    $CommonResSrc = Join-Path $BackupRelease "Common\res"
}
$CommonResDst = Join-Path $CommonDst "res"
if (Test-Path $CommonResSrc) {
    $NestedRes = Join-Path $CommonResDst "res"
    if (Test-Path $NestedRes) {
        Remove-Item -Path $NestedRes -Recurse -Force
    }
    New-Item -ItemType Directory -Path $CommonResDst -Force | Out-Null
    Copy-Item -Path "$CommonResSrc\*" -Destination $CommonResDst -Recurse -Force
    Write-Host "  Copiado: Common/res/"
}

# Sincronizar todos los graficos de Media a res/Graphics
$MediaGraphics = Join-Path "$CommonDst\Media" "Graphics"
$ResGraphics   = Join-Path "$CommonDst\res" "Graphics"
if (Test-Path $MediaGraphics) {
    New-Item -ItemType Directory -Path $ResGraphics -Force | Out-Null
    Copy-Item -Path "$MediaGraphics\*" -Destination $ResGraphics -Recurse -Force
    Write-Host "  Sincronizados graficos Media -> res/Graphics"
}

$CommonSoundDst = Join-Path $CommonDst "Sound"
New-Item -ItemType Directory -Path $CommonSoundDst -Force | Out-Null
if (Test-Path $MsscmpSrc) {
    Copy-Item -Path $MsscmpSrc -Destination "$CommonSoundDst\Minecraft.msscmp" -Force
    Write-Host "  Copiado: Common/Sound/Minecraft.msscmp"
}

Write-Host "Sincronizacion completada con exito." -ForegroundColor Green
