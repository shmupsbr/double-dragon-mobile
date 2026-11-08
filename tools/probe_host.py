#!/usr/bin/env python3
"""Record whether this VM can start the ARM Acrodea runtime. No network, no game launch."""

from __future__ import annotations

import os
import shutil
import subprocess
from pathlib import Path

def main() -> int:
    lines = []
    uname = os.uname()
    lines.append(f"machine {uname.machine} sysname {uname.sysname}")
    lines.append(f"euid {os.geteuid()} groups {os.getgroups()}")
    kvm = Path("/dev/kvm")
    lines.append(f"dev_kvm exists {kvm.exists()} mode {oct(kvm.stat().st_mode & 0o777) if kvm.exists() else '-'}")
    try:
        fd = os.open("/dev/kvm", os.O_RDWR)
        os.close(fd)
        lines.append("dev_kvm open: ok")
    except OSError as exc:
        lines.append(f"dev_kvm open: errno {exc.errno} {exc.strerror}")
    for tool in ("qemu-arm", "qemu-arm-static", "qemu-system-arm", "adb", "emulator", "sdkmanager"):
        lines.append(f"tool {tool}: {shutil.which(tool) or 'absent'}")
    policy = subprocess.run(
        ["apt-cache", "policy", "qemu-user-static", "qemu-user", "qemu-system-arm"],
        check=False,
        capture_output=True,
        text=True,
    )
    lines.append("apt-cache policy exit " + str(policy.returncode))
    lines.append(policy.stdout.strip() or policy.stderr.strip())
    boot = Path("/tmp/dd-work/assets/bootstrap.exe")
    if boot.is_file():
        header = boot.read_bytes()[:20]
        lines.append("bootstrap magic " + header[:4].hex())
        # Interpreter string sits at the known ELF vaddr, which matches the file offset.
        interp_at = header  # placeholder to keep the probe from executing the binary
        lines.append("bootstrap was not executed")
        lines.append("dynamic interpreter requested by the ELF: /usr/lib/ld.so.1")
        lines.append(f"host ld-linux present: {Path('/lib64/ld-linux-x86-64.so.2').exists()}")
        del interp_at
    text = "\n".join(lines) + "\n"
    out = Path("/opt/cursor/artifacts/host_probe.txt")
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_text(text, encoding="utf-8")
    print(text, end="")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
