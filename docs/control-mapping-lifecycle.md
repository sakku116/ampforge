# Control Map Lifecycle

Each template owns its `ControlMap`. The `MainComponent` runtime map is only the map currently selected for input handling.

## Rules

- At startup, the active template map replaces the standalone persisted map. The standalone map is a fallback only when no template is active.
- Recalling a template replaces the runtime map immediately.
- Capturing a template creates an empty map. It does not inherit mappings from the previously active template.
- Updating a template persists the current runtime map into that template.
- Before Learn Control checks for a duplicate trigger, actions whose stable slot IDs are no longer in the active chain are removed. Template-navigation actions remain valid.

## Inspecting Persisted State

Use the read-only helper to print each stored template's slots and control bindings:

```powershell
.\tools\inspect-settings.ps1
```

Pass `-SettingsPath` to inspect a different `AmpForge.settings` file.
