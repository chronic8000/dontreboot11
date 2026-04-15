# Don't Reboot 11

**Don't Reboot 11** is a high-fidelity, modular Win32 interdiction appliance designed to structurally dismantle the forced reboot mechanisms of **Windows 11 25H2 (April 2026 "Moment" Build)**.

Unlike common shutdown blockers, Don't Reboot 11 operates at the kernel and orchestration level, seizing control of the Windows Update servicing stack to guarantee absolute system uptime.

## 🛡️ The Triple-Lock Interdiction System

Don't Reboot 11 uses a multi-layered defensive strategy to ensure no update triggers an uncommanded reboot:

1.  **Registered System Administration (WinRT)**:
    Don't Reboot 11 registers as a `WindowsUpdateAdministrator` via C++/WinRT. By placing a kernel-level `BlockAutomaticRebootAsync` lock, Don't Reboot 11 demotes the native OS orchestrator, programmatically forbidding the system from rebooting without explicit permission.

2.  **ETW Preemptive Radar**:
    An active **Event Tracing for Windows (ETW)** consumer monitors the `Microsoft-Windows-UpdateOrchestrator` provider in real-time. Upon detecting Event ID 47 (Reboot notification scheduled), Don't Reboot 11 executes a preemptive strike, terminating `MusNotification.exe`, `MusNotificationUx.exe`, and `UpdateNotificationMgr.exe` before a prompt can ever appear.

3.  **Task Scheduler Hijacking (NTFS DACL Subversion)**:
    Don't Reboot 11 modifies the Security Descriptors (DACLs) of the critical `UpdateOrchestrator\Reboot` scheduled task. By injecting a **DENY** entry for the `SYSTEM` account, Don't Reboot 11 structurally prevents the OS from initializing the reboot flow via the Task Scheduler.

## 🧠 Intelligence & Detection

- **Moment Build Calibration**: Purpose-built for 25H2, detecting "Calendar Flyout" pauses via ISO 8601 strings and 64-bit QWORD registry ticks.
- **Instant Recognition**: Utilizes kernel-level **Registry Change Notifications** (`RegNotifyChangeKeyValue`) to update its status instantly when you toggle a pause or a policy is updated.
- **Deep Status Sweep**: Monitors 6 critical hives—including CBS Staging, WUA State, and Enterprise Policy—to distinguish between "Ghost" notifications and legitimate reboot threats.

## 🛠️ Technical Specifications

- **Performance**: Ultra-lightweight C++20 core (< 1.5MB RAM footprint).
- **Stealth**: Runs as a tray-resident service with no taskbar presence.
- **Reliability**: Zero-latency response with zero idle CPU utilization.
- **Requirements**: Requires **Administrator Privileges** to deploy WinRT locks and Task Security subversion.
- **Persistence**: Optional "Start with Windows" toggle via system tray.

## Usage

Simply run `dontreboot11.exe` as Administrator. Use the system tray icon to monitor real-time update status. When updates are paused, the app will stay in "Safe" mode; when an update is active or pending, it will engage its "Protected" interdiction layers.

---
*Architected for the preservation of deep-work and mission-critical sessions.*
