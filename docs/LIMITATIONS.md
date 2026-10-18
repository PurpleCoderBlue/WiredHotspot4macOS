# Known Limitations

## Platform and distribution

- macOS 11 is the minimum deployment target. The current end-to-end reference machine is Apple Silicon on macOS 27; older macOS and Intel builds are produced and compile-checked but cannot all be hardware-tested for every release.
- Homebrew installs the prebuilt universal `.pkg` through a Cask; the Cask itself does not compile on the target Mac and can be installed without developer tools. Homebrew still lists Xcode Command Line Tools or Xcode as a requirement for a fully supported Homebrew installation. The release `.pkg` has no such requirement.
- GitHub Releases contain a universal arm64/x86_64 ZIP and the same unified `.pkg` used by the Cask. This personal project cannot sign and notarize the package without a paid Developer ID.

## USB protocols and devices

- RNDIS is implemented. CDC-ECM and CDC-NCM functions are detected but their data backends are not implemented yet.
- Android gadget layouts `e0/01/03`, `02/02/ff`, and `ef/04/01` paired with a CDC data interface are recognized. Vendor-specific layouts may need a descriptor report and code update.
- The RNDIS interfaces are claimed exclusively. ADB remains available only when Android exposes it as a separate USB interface.
- Control/data pairing uses CDC Union or IAD descriptors. A unique single-function
  layout without these descriptors is supported; ambiguous composite layouts
  are rejected rather than claiming another function. Discovery may display
  `?` until the control interface's descriptors resolve the data interface.
- The network backend keeps a 1500-byte MTU. Devices advertising a receive limit
  too small for a full frame plus RNDIS framing are rejected with an explicit
  error. Host receive capacity and the device's receive limit are independent.

## macOS networking

- **HoRNDIS USB** in Network settings represents the actual DHCP/traffic bridge. Registration uses private bridge configuration APIs resolved at runtime; compatibility is not guaranteed across OS updates. If unavailable or unsafe, the direct feth fallback still works but is not listed in Network settings. Existing occupied bridges are left untouched. Only the owned service/bridge is removed at uninstall; an ownership conflict must be resolved before removal.
- A minimal root supervisor remains resident because BPF access and system DHCP/interface configuration are privileged. It does not process USB or RNDIS data; the inherited-BPF data agent runs as the current console user and can only request the fixed per-session DHCP refresh over a private channel.
- The data agent's BPF descriptor is protected by an inherited ioctl sandbox;
  setup failure stops the agent. This uses a deprecated macOS API, so older
  macOS versions require runtime testing, not only successful compilation.
- The feth/BPF backend is not a promised long-term Apple ABI. The code keeps this backend isolated so a future utun or other public backend can replace it.
- VPN route priority, fake-IP DNS, endpoint security products, and other network extensions can change which path a normal application uses. Interface-bound tests are required to prove the USB path independently. A global TUN VPN can also intercept traffic explicitly bound to the feth interface before it reaches the bridge; exclude the tether subnet in the VPN client if the Mac must reach phone-side addresses directly.
- A VPN transition or other network reordering can remove the feth interface's IPv4 address while the bridge keeps forwarding. HoRNDIS detects this during a connected session and automatically requests a fresh DHCP lease after a short grace period.

## Android tethering

- The phone controls upstream selection and NAT. Android VPNs do not necessarily share their tunnel through native USB tethering.
- Some rooted or modified Android builds have broken tethering BPF/conntrack offload. If DHCP and ICMP work while TCP stalls, temporarily disabling Android tether offload can distinguish that device problem from the macOS bridge. Restore the property after diagnosis if the device works with its default.
- The checked USB Tethering menu command resumes or pauses HoRNDIS discovery; it cannot enable Android USB tethering or unlock the phone.

## Menu bar status

- The status UI is a standard AppKit pull-down `NSMenu` on every supported macOS version. USB Tethering and Launch at Login are checked menu commands rather than draggable switches, and Details opens a native side submenu rather than expanding in place. AppKit owns the menu geometry and animation. See [MENU_UI_GUIDELINES.md](MENU_UI_GUIDELINES.md) for the full framework contract.
- The root summary uses three native information rows. A valid authorization state is intentionally omitted; the authorization warning and install action appear only when repair is required. The traffic row uses its leading image as the upload arrow and begins its title with the transmitted total; other summary rows retain their semantic images.
- The menu bar app includes English, Simplified Chinese, Traditional Chinese, Japanese, Korean, French, German, Spanish, Brazilian Portuguese, Italian, and Russian bundle localizations. The default is Simplified Chinese unless a macOS per-app language is explicitly selected.
- Traffic totals are Ethernet frame bytes for the current connection, not carrier-billing counters. They reset after disconnect, service restart, or a new USB session.
- Status refreshes every two seconds while the menu is closed and every second while it is visible, so very short transitions can still be missed.
- The status file contains the device product name, RNDIS MAC, interface name, counters, and daemon PID. It is local, restricted to the current console user and root, and never uploaded by this project.
- Diagnostic reports are generated locally and are never uploaded automatically. Account/full names, host/device names, USB serial values and location IDs, MAC/IP addresses, hardware serial numbers, hardware UUIDs, packet contents, and credentials are not collected into the report. Users are labeled `user`; devices receive stable first-seen `device N` aliases across reconnects for the lifetime of the unprivileged data process. The in-memory alias map is not persisted or sent to root, so numbering restarts with that process. Older service-log lines are sanitized during copying; service events and errors remain for diagnosis, so users must still review the file before attaching it to a public issue.
- Quitting or crashing the menu bar process does not stop tethering. Reopen it from `/Applications` or run `horndis start`. A missing or invalid privileged installation can be repaired with the menu's one-time **Authorize and Install…** action or `horndis install`; both still require normal macOS administrator authentication.

## Acceptance required for the current fixes

The regression suite uses temporary files/sockets, in-memory USB API doubles and
`/dev/null` sandbox checks. It does not install a service, open a real BPF device,
claim a real USB interface or modify network preferences. The network-service
unit test only checks rejection while unprivileged; its root lifecycle branch
requires a separate explicit `--isolated-root-lifecycle` invocation.

After authorizing installation/network changes on a test machine, verify:

1. On each target Mac/Android combination, acquire DHCP, resolve DNS, and transfer
   TCP and UDP in both directions with traffic explicitly bound to the selected
   interface. Wi-Fi/VPN routes must not mask the USB result.
2. Exercise USB packet-boundary lengths and aggregated receive transfers; confirm
   short-packet padding, negotiated limits and asynchronous RNDIS status events
   with a protocol trace or controlled gadget when available.
3. Test RNDIS plus ADB and composite layouts with multiple CDC data interfaces;
   verify only the associated RNDIS control/data interfaces are claimed.
4. Repeat unplug/replug, phone tethering off/on, menu pause/resume, sleep/wake and
   an agent crash. Confirm reconnect, DHCP recovery and no accumulating interfaces.
5. On a real inherited BPF descriptor, confirm rebind/filter reconfiguration is
   denied while normal forwarding, buffer queries and flushing still work. Verify
   sandbox setup failure stops startup on every supported macOS version.
6. Switch console users and log out while idle and while forwarding. Confirm the
   old agent stops, old BPF descriptors cannot observe/inject into the new pair,
   runtime endpoints change ownership, and status/menu controls work for the new user.
7. Verify fresh installation, upgrade from the older shared-status-directory
   layout, reboot and uninstallation on the test machine. Check unrelated
   interfaces and network services before and after.

Passing local regression tests does not mark these device checks as complete.

Disconnect recovery regression coverage includes injected activation failures,
commit/apply split failures, transient and persistent cleanup failure, a stalled
supervisor, full output queues, absent USB completion callbacks and late callbacks.
These tests isolate OS/USB operations; they do not reproduce a complete macOS
Wi-Fi/third-party TUN handover. Validate both orders on hardware: Wi-Fi already
connected before unplug, and Wi-Fi connected only after unplug. Include a loaded
USB transfer and repeat unplug/replug and menu pause/resume, keeping proxy and
Wi-Fi settings unchanged. Check owned bridge carrier/address/route removal and
new-session DHCP and traffic. A working handover does not prove an earlier
intermittent outage's cause, and HoRNDIS does not reset a third-party TUN's routes.
