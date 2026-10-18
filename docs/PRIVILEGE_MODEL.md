# Privilege Model

HoRNDIS uses one administrator-authorized installation and does not persist an administrator password. Reboot, login, USB hot-plug, Android tethering changes, pause/resume, and menu-bar startup do not request authorization again.

## Why any root component remains

On macOS, `/dev/bpf*` is `root:wheel` with mode `0600`, and only the super-user may create or modify network interfaces. HoRNDIS needs both operations to connect a user-space USB implementation to the macOS TCP/IP stack.

Changing BPF device permissions, adding a broad passwordless sudo rule, or installing a setuid executable would widen access more than a fixed LaunchDaemon and is intentionally avoided.

## Process boundary

```text
root LaunchDaemon supervisor
  ├─ select and exclusively create an unused feth pair
  ├─ register an owned Network Service/bridge (direct feth fallback)
  ├─ start system DHCP on the selected macOS-facing interface
  ├─ open and bind one BPF descriptor
  └─ exec a bootstrap that restricts BPF ioctls, drops identity, then execs the agent
                    │
                    ▼
console-user data agent
  ├─ IOUSBHost discovery and interface ownership
  ├─ RNDIS initialization and parsing
  ├─ Ethernet frame forwarding through inherited BPF
  ├─ atomic runtime status
  └─ authenticated local pause/resume control

console-user Swift menu bar
  └─ read status and request pause/resume
```

The fork child only closes its unused channel and executes a fresh bootstrap;
it does not initialize frameworks or compile a sandbox profile after `fork`.
Before dropping identity, the bootstrap applies a process sandbox that denies
reconfiguration ioctls on the kernel-resolved BPF device path. Only the fixed
buffer-size query (`BIOCGBLEN`) and queue reset (`BIOCFLUSH`) remain available.
The policy survives descriptor duplication and `exec`. If sandbox setup fails,
the bootstrap exits; there is no unrestricted fallback. It then permanently
drops supplementary groups, group ID and user ID, and executes the data agent.
The data agent also refuses to run as root. See [AgentSandbox.cpp](../Sources/AgentSandbox.cpp)
and [main.mm](../Sources/main.mm).

After each successful RNDIS initialization, the agent sends one fixed `refresh
DHCP` request and waits for a one-byte success/failure response before reporting
the session as connected. The request carries no interface name, command
arguments, packet content, or other device-controlled data. The root supervisor
raises both prevalidated feth interfaces and restarts the macOS DHCP client. It
never receives or parses USB-controlled bytes.

While waiting for requests, the supervisor checks the console user every 250 ms.
Logout or a UID/GID change terminates the old agent with a bounded graceful wait,
then recreates the owned feth pair before another user receives a capability.
Recreating the pair matters: stopping one process cannot revoke a BPF descriptor
it has copied into a descendant. Surviving old descriptors cannot be rebound
under the inherited sandbox. Actual BPF detach/revocation across user switching
still requires device acceptance testing.

This per-session handshake prevents a boot-order race in which macOS networking clears the temporary DHCP state after the LaunchDaemon's initial setup but before an Android device connects. If activation fails, the supervisor rolls back the owned network and the agent reports an error and exits. The supervisor completes cleanup before restarting the agent; persistent cleanup failure terminates the supervisor instead of acknowledging a new session.

Automatic selection prefers `feth99`/`feth98`, then scans downward in odd/even pairs. A pair is eligible only when both names are absent. HoRNDIS never adopts, reconfigures, or destroys an interface that already existed; it removes only the pair created by the current supervisor. Explicit environment overrides remain available but must name two absent, distinct `feth<number>` interfaces.

The agent runtime directory `/var/run/horndis` is assigned to the current console
user with mode `0700`; its status and control endpoints use mode `0600`. Root
prepares it through a non-following directory descriptor, revoking the previous
owner before unlinking stale endpoints. Root status snapshots use a separate,
root-owned `/var/run/horndis-supervisor` directory. Ownership and permissions are
set on the open temporary file before atomic publication; root never follows a
user-writable `status.json` path to change its ownership. Readers select the
newest readable agent/supervisor snapshot. See [RuntimeStatus.cpp](../Sources/RuntimeStatus.cpp).

The local control socket validates the connecting peer against the current
console user. Pending clients are bounded and expire after two seconds; reads
are nonblocking and preserve fragmented newline-terminated commands. Idle
clients cannot block the forwarding loop.

## Installation lifecycle

`horndis install` delegates exactly one operation to `sudo horndis service install`; the Homebrew Cask and GitHub Release package perform the same operation inside the administrator-authorized Installer process. That privileged step:

1. ensure `/Library/PrivilegedHelperTools` exists as `root:wheel` with the
   standard `01755` permissions, creating it when absent;
2. copy the exact executable to `/Library/PrivilegedHelperTools/io.github.noahhhi.horndis`;
3. set root ownership and non-writable executable permissions;
4. install the root-owned LaunchDaemon plist;
5. bootstrap the service.

The Homebrew command-line path's password is handled by `sudo`, while the package path uses macOS Installer authorization. The menu app provides a third entry point: it verifies that the helper and LaunchDaemon plist are root-owned, non-writable by ordinary users, and structurally valid; when they are not, **Authorize and Install…** asks the macOS Security Agent to run only the fixed bundled `horndis service install` command. HoRNDIS never reads or stores a credential in any path. The LaunchDaemon supplies persistence, so storing credentials in a script, Skill, Keychain lookup, environment variable, or sudoers exception is unnecessary. The menu LaunchAgent runs as the console user and needs no additional authorization.

## Remaining risk

The owned **HoRNDIS USB** Network Service uses a registered bridge with a
feth member registered in both runtime and system bridge preferences. The member
is removed from both on pause/close and restored on connection, so System Settings
can determine the real bridge status. Bridge registration uses dynamically resolved private
SystemConfiguration APIs, with direct feth fallback if unavailable or unsafe.
The preferences session is locked and uses the default system domain. Bridge
options record ownership and the service ID; existing members prevent reuse or
removal. The supervisor never attaches physical network interfaces. A second
fixed, argument-free request clears DHCP and lowers the owned feth pair after
disconnect/pause; channel closure also suspends the network after an agent crash.
Suspension retries up to three times, including preferences that were committed
but not successfully applied. It lowers only the owned bridge/feth endpoints even
if cleanup fails. The agent cancels queued USB writes and requests suspension
before joining its output worker. Requests have a 30-second deadline and revoke
the stream on failure, so a late acknowledgement cannot satisfy a new operation.
Interface commands have a three-second deadline, and preferences locks are
nonblocking. SystemConfiguration's synchronous apply API still depends on configd.
USB capacity waits time out after five seconds; close aborts pending writes and
waits at most 250 ms on its completion condition. Late callbacks retain a separate
queue and request buffers, never a destroyed transport or a new session's queue.

The supervisor is still a root process and the feth/BPF backend depends on macOS
behavior that Apple does not promise as a stable public ABI. The BPF ioctl policy
uses the deprecated `sandbox_init` API and requires runtime validation on each
supported macOS version. It restricts that capability rather than confining the
entire USB agent's filesystem or network access. This is not an Apple-entitled
DriverKit component.

A future backend could replace BPF/feth with a suitable public unprivileged API if Apple provides one. The current source separates USB, RNDIS, Ethernet, service, control, and UI layers to keep that migration bounded.
