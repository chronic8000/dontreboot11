# Don't Reboot 11

**Don't Reboot 11** is a deterministic, user-authoritative reboot permission layer for **Windows 11**. It blocks automatic update reboots while letting you pause updates through official organization policy controls—and turn those controls off again from the system tray.

## What it does

1. **Reboot gatekeeper (always on while running)**  
   Uses `BlockAutomaticRebootAsync` (WinRT) with a 60-second watchdog so Windows cannot restart without your consent.

2. **Organization update control (tray toggle)**  
   Applies Microsoft-supported policies under `HKLM\SOFTWARE\Policies\Microsoft\Windows\WindowsUpdate`:
   - Pause feature and quality updates (up to 35 days)
   - Defer updates (35 / 30 days)
   - `NoAutoRebootWithLoggedOnUsers`
   - `AUOptions = 3` (download automatically, notify before install)

   It deliberately does **not** set compliance **deadline** policies (`ConfigureDeadlineFor*`), which cause *"completed at a time selected by your organisation"* and grey out **Download & install**.

3. **Restore control**  
   **Restore Windows Update control** removes Don't Reboot 11 policies, clears the Windows Update `GPCache`, and runs `gpupdate` so Settings returns to normal.

## Tray menu (right-click the shield icon)

| Item | Action |
|------|--------|
| **Pause Windows Update (Organization control)** | Toggle pause/defer policies on or off |
| **Restore Windows Update control** | Emergency cleanup if updates stay locked |
| **Start with Windows** | Autostart |
| **Exit** | Quit the app |

## If updates are stuck (your screenshot)

1. Run `dontreboot11.exe` **as Administrator**.
2. Right-click the tray icon → **Restore Windows Update control**.
3. Uncheck **Pause Windows Update** if it is still checked.
4. Open **Settings → Windows Update** — **Download & install** should be enabled again.

Stale policy can also live in `HKLM\SOFTWARE\Microsoft\WindowsUpdate\UpdatePolicy\GPCache`; the restore action deletes that tree.

## Build

From **x64 Native Tools Command Prompt for VS**:

```bat
build.bat
```

Or open `dontreboot11.sln` in Visual Studio (requires Administrator manifest).

## Requirements

- Windows 11 Pro or higher (policy registry paths)
- Run as **Administrator**

---

*Clean control for mission-critical sessions—pause updates when you need to, restore control when you don't.*
