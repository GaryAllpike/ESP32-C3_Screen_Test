# ESP-IDF 6: esp_lcd_panel_dev_config_t uses rgb_ele_order (not rgb_endian).
# Run from project root if build still compiles managed_components/.../esp_lcd_st7735.c with errors.
$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
if (-not (Test-Path (Join-Path $root "CMakeLists.txt"))) {
    $root = (Get-Location).Path
}
$targets = @(
    (Join-Path $root "managed_components\waveshare__esp_lcd_st7735\esp_lcd_st7735.c"),
    (Join-Path $root "components\waveshare__esp_lcd_st7735\esp_lcd_st7735.c")
)
foreach ($f in $targets) {
    if (-not (Test-Path $f)) { Write-Host "Skip (missing): $f"; continue }
    $c = Get-Content -Raw $f
    if ($c -notmatch 'rgb_endian') { Write-Host "Already patched: $f"; continue }
    $c = $c.Replace('panel_dev_config->rgb_endian', 'panel_dev_config->rgb_ele_order')
    $c = $c.Replace('LCD_RGB_ENDIAN_RGB', 'LCD_RGB_ELEMENT_ORDER_RGB')
    $c = $c.Replace('LCD_RGB_ENDIAN_BGR', 'LCD_RGB_ELEMENT_ORDER_BGR')
    $c = $c.Replace('unsupported rgb endian', 'unsupported RGB element order')
    Set-Content -Path $f -Value $c -NoNewline
    Write-Host "Patched: $f"
}
Write-Host "Done. Then run: idf.py reconfigure"
