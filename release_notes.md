# Don't Reboot 11 v1.1.0 - Windows Update UI Crash Fix & Restore Logic

This release contains critical fixes for users attempting to restore manual Windows Update control and resolves the Settings app UI crash ("Something went wrong").

### Key Changes
* **Settings UI Crash Resolution**: Fixed a critical issue where the Windows Update Settings page crashed with *"Something went wrong"* after removing the organisation banner.
* **Local Group Policy Cache Wiping**: Added automated recursive cleaning of `C:\Windows\System32\GroupPolicy` and `C:\Windows\System32\GroupPolicyUsers` in the restore logic. This prevents the OS from re-applying cached Group Policies that lock update controls.
* **Orchestrator Locks Removal**: Completely removes legacy update administration configurations (specifically registered to `Sentinel-Sovereign-Interdictor`) that were causing updates to display *"This update will be completed at a time selected by your organisation"*.
* **Enhanced System Diagnostics**: Built-in diagnostics log generated when running the recovery tool to map active policies, registry settings, and security permissions for troubleshooting.
* **Clean Project Naming**: Ensured all project files, resource settings, and documentation consistently refer to **Don't Reboot 11** and output the final binary as `dontreboot11.exe`.

### Installation & Usage
1. Download `dontreboot11.exe`.
2. Run the application (requires Administrator elevation).
3. Right-click the shield icon in the system tray:
   * Select **Pause Windows Update** to block reboots using clean organization pause settings.
   * Select **Restore Windows Update control** if you wish to wipe all policy controls and restore standard Windows update behavior.
