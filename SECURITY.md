# Security policy

Please report vulnerabilities privately through GitHub's security-advisory feature rather than a public issue.

The supervisor retains root for BPF and system network configuration. A fresh
bootstrap restricts the inherited BPF device's ioctls before permanently dropping
groups and user identity and executing the USB data agent. Sandbox setup failure
must stop startup; do not add an unrestricted fallback. USB discovery, RNDIS
parsing and forwarding execute in the unprivileged agent. See the
[privilege model](docs/PRIVILEGE_MODEL.md).

Treat USB descriptors, RNDIS lengths, and Ethernet frame lengths as untrusted.
New code must validate every device-controlled offset before reading or allocating
memory and must never invoke a shell with device-controlled data. Root status
publication must remain in its root-owned directory and set metadata through
opened file descriptors, never through names in the agent's user-writable directory.

Supported releases receive security fixes on the latest minor version. The project does not require disabling System Integrity Protection or changing Startup Security Utility settings; instructions that require either are outside its supported threat model.
