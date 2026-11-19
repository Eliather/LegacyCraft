# ============================================================
#  make_release.ps1
#  Empaqueta el build de Release en una carpeta lista para
#  comprimir y subir como GitHub Release.
#
#  Uso:
#    .\scripts\make_release.ps1
#    .\scripts\make_release.ps1 -Version "0.2.0"
# ============================================================

param(
    [string]$Version = "dev"
)

$Root          = Split-Path $PSScriptRoot -Parent
$BuildDir      = if (Test-Path "$Root\build\Minecraft.Client.exe") { Join-Path $Root "build" } else { Join-Path $Root "x64\Release" }
$BackupRelease = "C:\Users\danie\Documents\Trabajos\LegacyCraft_backup\x64\Release"
$OutputName    = "LegacyCraft-$Version"
$OutputDir     = Join-Path $Root "dist\$OutputName"

# ── Verificar que el build exista ──────────────────────────────
if (-not (Test-Path "$BuildDir\Minecraft.Client.exe")) {
    Write-Error "No se encontro Minecraft.Client.exe en $BuildDir ni en build\"
    Write-Host  "Compila primero ejecutando: cmake --build build"
    exit 1
}

# ── Limpiar y crear carpeta de salida ─────────────────────────
if (Test-Path $OutputDir) { Remove-Item -Recurse -Force $OutputDir }
New-Item -ItemType Directory -Path $OutputDir -Force | Out-Null

Write-Host "Empaquetando release en: $OutputDir" -ForegroundColor Cyan

function FastCopy-Dir($src, $dst) {
    if (-not (Test-Path $src)) { return }
    if (-not (Test-Path $dst)) {
        New-Item -ItemType Directory -Path $dst -Force | Out-Null
    }
    robocopy $src $dst /E /R:1 /W:1 /NP /NFL /NDL /NJH /NJS | Out-Null
}

# ── 1. Copiar ejecutable ───────────────────────────────────────
Copy-Item "$BuildDir\Minecraft.Client.exe" -Destination $OutputDir -Force
Write-Host "  Copiado ejecutable: Minecraft.Client.exe"

# ── 2. Copiar DLLs y archivos raiz ────────────────────────────
$RootFiles = @("iggy_w64.dll", "mss64.dll", "sound_init.txt")
foreach ($rf in $RootFiles) {
    $src = if (Test-Path "$BuildDir\$rf") { "$BuildDir\$rf" } else { "$BackupRelease\$rf" }
    if (Test-Path $src) {
        Copy-Item -Path $src -Destination $OutputDir -Force
        Write-Host "  Copiado raiz: $rf"
    }
}

# ── 3. Copiar redist64 (plugins de sonido Miles) ───────────────
$RedistSrc = if (Test-Path "$BuildDir\redist64") { "$BuildDir\redist64" } else { "$BackupRelease\redist64" }
FastCopy-Dir $RedistSrc "$OutputDir\redist64"
Write-Host "  Copiado: redist64/"

# ── 4. Copiar Skins ───────────────────────────────────────────
$SkinsSrc = if (Test-Path "$BuildDir\Skins") { "$BuildDir\Skins" } else { "$BackupRelease\Skins" }
FastCopy-Dir $SkinsSrc "$OutputDir\Skins"
Write-Host "  Copiado: Skins/"

# ── 5. Copiar Saves ───────────────────────────────────────────
$SavesSrc = if (Test-Path "$BuildDir\Saves") { "$BuildDir\Saves" } else { "$BackupRelease\Saves" }
FastCopy-Dir $SavesSrc "$OutputDir\Saves"
Write-Host "  Copiado: Saves/"

# ── 6. Copiar Sound (musica binka y soundbank) ─────────────────
$SoundSrc = if (Test-Path "$BuildDir\Sound") { "$BuildDir\Sound" } else { "$BackupRelease\Sound" }
FastCopy-Dir $SoundSrc "$OutputDir\Sound"
Write-Host "  Copiado: Sound/"

# ── 7. Copiar Windows64 (GameConfig y GameHDD) ─────────────────
$Win64Src = if (Test-Path "$BuildDir\Windows64") { "$BuildDir\Windows64" } else { "$BackupRelease\Windows64" }
FastCopy-Dir $Win64Src "$OutputDir\Windows64"
Write-Host "  Copiado: Windows64/"

# ── 8. Copiar Windows64Media (DLC, loc, Media) ─────────────────
$WinMediaSrc = if (Test-Path "$BuildDir\Windows64Media") { "$BuildDir\Windows64Media" } else { "$BackupRelease\Windows64Media" }
FastCopy-Dir $WinMediaSrc "$OutputDir\Windows64Media"
Write-Host "  Copiado: Windows64Media/"

# ── 9. Copiar Common (Media, res, Sound) ───────────────────────
$CommonDst = Join-Path $OutputDir "Common"
New-Item -ItemType Directory -Path $CommonDst -Force | Out-Null

$CommonMediaSrc = "$BackupRelease\Common\Media"
if (-not (Test-Path $CommonMediaSrc)) {
    $CommonMediaSrc = Join-Path $Root "Minecraft.Client\Common\Media"
}
FastCopy-Dir $CommonMediaSrc "$CommonDst\Media"
Write-Host "  Copiado: Common/Media/"

$RepoCommonRes = Join-Path $Root "Minecraft.Client\Common\res"
if (Test-Path $RepoCommonRes) {
    FastCopy-Dir $RepoCommonRes "$CommonDst\res"
}
if (Test-Path "$BackupRelease\Common\res") {
    FastCopy-Dir "$BackupRelease\Common\res" "$CommonDst\res"
}
Write-Host "  Copiado: Common/res/"

$CommonSoundDst = Join-Path $CommonDst "Sound"
New-Item -ItemType Directory -Path $CommonSoundDst -Force | Out-Null
$MsscmpSrc = if (Test-Path "$BuildDir\Sound\Minecraft.msscmp") { "$BuildDir\Sound\Minecraft.msscmp" } else { "$BackupRelease\Common\Sound\Minecraft.msscmp" }
if (Test-Path $MsscmpSrc) {
    Copy-Item -Path $MsscmpSrc -Destination "$CommonSoundDst\Minecraft.msscmp" -Force
    Write-Host "  Copiado: Common/Sound/Minecraft.msscmp"
}

# ── 10. Crear ZIP ──────────────────────────────────────────────
$ZipPath = Join-Path $Root "dist\$OutputName.zip"
if (Test-Path $ZipPath) { Remove-Item -Force $ZipPath }

Write-Host "Comprimiendo ZIP: $ZipPath ..." -ForegroundColor Yellow
Add-Type -AssemblyName System.IO.Compression.FileSystem
[System.IO.Compression.ZipFile]::CreateFromDirectory($OutputDir, $ZipPath)

Write-Host ""
Write-Host "Release listo:" -ForegroundColor Green
Write-Host "  Carpeta : $OutputDir"
Write-Host "  ZIP     : $ZipPath"
Write-Host ""
Write-Host "Proximos pasos:" -ForegroundColor Cyan
Write-Host "  1. git tag v$Version"
Write-Host "  2. git push origin v$Version"
Write-Host "  3. Sube $OutputName.zip como asset en GitHub Releases"
