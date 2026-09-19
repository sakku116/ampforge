[CmdletBinding()]
param(
    [string] $SettingsPath = (Join-Path $env:APPDATA "AmpForge\AmpForge.settings")
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

if (-not (Test-Path -LiteralPath $SettingsPath -PathType Leaf)) {
    throw "Amp Forge settings file was not found: $SettingsPath"
}

[xml] $settings = Get-Content -LiteralPath $SettingsPath -Raw
$scenesValue = @($settings.PROPERTIES.VALUE | Where-Object { $_.name -eq "scenes" })[0]

if ($null -eq $scenesValue -or $null -eq $scenesValue.SCENES) {
    Write-Output "No stored templates found."
    exit 0
}

function Format-Trigger($binding) {
    switch ([int] $binding.trigType) {
        1 { return "Note $($binding.trigNumber) (ch $($binding.trigChannel))" }
        2 { return "CC $($binding.trigNumber) (ch $($binding.trigChannel))" }
        3 { return "Program $($binding.trigNumber) (ch $($binding.trigChannel))" }
        4 {
            $key = [int] $binding.trigNumber
            if ($key -ge 32 -and $key -le 126) { return "Key $([char] $key)" }
            return "Key code $key"
        }
        default { return "Unknown trigger" }
    }
}

function Format-Action($binding) {
    switch ([int] $binding.actType) {
        1 { return "Next Template" }
        2 { return "Previous Template" }
        3 { return "Load Template index $($binding.actIndex)" }
        4 { return "Toggle Bypass slot ID $($binding.actIndex)" }
        5 { return "Activate Preset Slot ID $($binding.actIndex)" }
        default { return "Unknown action" }
    }
}

foreach ($scene in @($scenesValue.SCENES.TONEFORGE_PRESET)) {
    $slots = @($scene.SLOT | ForEach-Object { [int] $_.slotId })
    $bindings = @($scene.CONTROLMAP.BINDING)
    $name = $scene.name

    Write-Output "Template: $name"
    Write-Output "  Slot IDs: $($slots -join ', ')"
    Write-Output "  Bindings: $($bindings.Count)"

    foreach ($binding in $bindings) {
        Write-Output "    $(Format-Trigger $binding) -> $(Format-Action $binding)"
    }
}
