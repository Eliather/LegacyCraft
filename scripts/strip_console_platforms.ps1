# ============================================================
#  strip_console_platforms.ps1
#  Elimina de Minecraft.Client.vcxproj y MinecraftConsoles.sln
#  todas las configuraciones que no sean Debug|x64 / Release|x64.
#
#  Plataformas eliminadas: Xbox 360, Durango, PS3, PSVita, ORBIS
#
#  Uso:
#    .\scripts\strip_console_platforms.ps1
# ============================================================

$ErrorActionPreference = "Stop"
$Root      = Split-Path $PSScriptRoot -Parent
$VcxprojPath = Join-Path $Root "Minecraft.Client\Minecraft.Client.vcxproj"
$SlnPath     = Join-Path $Root "MinecraftConsoles.sln"

# Plataformas a eliminar (patron de busqueda en atributos Condition)
$ConsolePlatforms = @("Xbox 360", "Durango", "PS3", "PSVita", "ORBIS")

# ── Helpers ──────────────────────────────────────────────────
function Has-ConsoleCondition([string]$condition) {
    foreach ($p in $ConsolePlatforms) {
        if ($condition -like "*$p*") { return $true }
    }
    return $false
}

# ── 1. Procesar el .vcxproj ───────────────────────────────────
Write-Host "Procesando $VcxprojPath ..." -ForegroundColor Cyan

[xml]$xml = Get-Content $VcxprojPath -Encoding UTF8

$ns = "http://schemas.microsoft.com/developer/msbuild/2003"
$nsMgr = New-Object System.Xml.XmlNamespaceManager($xml.NameTable)
$nsMgr.AddNamespace("ms", $ns)

$removedCount = 0

# Recorrer todos los elementos hijos del Project que tengan atributo Condition
$nodesToRemove = @()
foreach ($node in $xml.Project.ChildNodes) {
    $cond = $node.GetAttribute("Condition")
    if ($cond -and (Has-ConsoleCondition $cond)) {
        $nodesToRemove += $node
    }
    else {
        # Dentro de ItemGroups: revisar hijos con Condition
        foreach ($child in @($node.ChildNodes)) {
            $childCond = $child.GetAttribute("Condition")
            if ($childCond -and (Has-ConsoleCondition $childCond)) {
                $nodesToRemove += $child
            }
        }
    }
}

foreach ($node in $nodesToRemove) {
    $node.ParentNode.RemoveChild($node) | Out-Null
    $removedCount++
}

# Guardar con la misma codificacion
$settings = New-Object System.Xml.XmlWriterSettings
$settings.Indent = $true
$settings.IndentChars = "  "
$settings.Encoding = [System.Text.Encoding]::UTF8
$settings.OmitXmlDeclaration = $false

$writer = [System.Xml.XmlWriter]::Create($VcxprojPath, $settings)
$xml.Save($writer)
$writer.Close()

Write-Host "  Eliminados $removedCount nodos de consola del vcxproj." -ForegroundColor Green

# ── 2. Procesar el .sln ───────────────────────────────────────
Write-Host "Procesando $SlnPath ..." -ForegroundColor Cyan

$slnLines = Get-Content $SlnPath -Encoding UTF8
$filteredLines = @()
$skippedSln = 0

foreach ($line in $slnLines) {
    $skip = $false
    foreach ($p in $ConsolePlatforms) {
        if ($line -like "*$p*") {
            $skip = $true
            $skippedSln++
            break
        }
    }
    if (-not $skip) { $filteredLines += $line }
}

Set-Content -Path $SlnPath -Value $filteredLines -Encoding UTF8
Write-Host "  Eliminadas $skippedSln lineas de consola del sln." -ForegroundColor Green

# ── 3. Verificacion rapida ────────────────────────────────────
Write-Host ""
Write-Host "Verificacion:" -ForegroundColor Cyan

$remaining = Select-String -Path $VcxprojPath -Pattern ($ConsolePlatforms -join "|")
if ($remaining) {
    Write-Host "  ATENCION: Aun quedan $($remaining.Count) referencias en el vcxproj." -ForegroundColor Yellow
} else {
    Write-Host "  vcxproj limpio - sin referencias a consolas." -ForegroundColor Green
}

$configs = Select-String -Path $VcxprojPath -Pattern '<ProjectConfiguration Include='
Write-Host "  Configuraciones restantes: $($configs.Count) (esperado: 2)" -ForegroundColor $(if ($configs.Count -eq 2) { "Green" } else { "Yellow" })

Write-Host ""
Write-Host "Listo. Abre la solucion en Visual Studio para verificar." -ForegroundColor Green
