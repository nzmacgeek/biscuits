BUILDING BlueyOS (biscuits)
=================================

This file describes how to build the BlueyOS repository from scratch, component-by-component.

Prerequisites
-------------
- A POSIX build host (Debian/Ubuntu tested).
- Common packages: `gcc` (multilib for i386), `binutils`, `nasm`, `make`, `python3`, `curl`, `pkg-config`.
- QEMU for running/testing: `qemu-system-i386` (and `qemu-system-m68k` / `qemu-system-ppc` if needed).
- Tools for creating ISOs: `grub-pc-bin`, `xorriso` (only for `make iso`).
- Cross-compilers when building cross-target userland (optional): `i686-linux-gnu-gcc`, and toolchains for `m68k`/`ppc` if building those kernels/userlands.

Quick build (i386)
-------------------
1. Build host-side helper tools (mkfs/list/fsck):

   `make tools-host`

2. Build the kernel (default: i386):

   `make`

   - To build other architectures: `make ARCH=m68k` or `make ARCH=ppc`.

3. (Optional) Build musl-based test init (uses the helper that builds musl):

   `make musl-init`

   - You can override the local musl install prefix used by repo builds: `MUSL_PREFIX=/path/to/musl make musl-init`.
   - To build and install musl into all supported destinations, use `make build-musl-blueyos`.
   - That target installs musl into:
     - `build/userspace/musl` for repo-local builds
     - `/opt/blueyos-sysroot` for the runtime sysroot used by `make disk`
     - `/opt/blueyos-cross/musl` for musl wrapper/tools alongside the cross toolchain

4. (Optional) Build optional userspace programs (bash)

   - There's a helper to build `readline`, `ncurses` and `bash` against a musl sysroot:

     `scripts/build-bash.sh --musl-prefix build/userspace/musl --workdir build/userspace/bash-build`

   - After building, copy the resulting `bash` binary into `build/userspace/bash/bin/` (create that path) so the `sysroot` step picks it up.

5. Assemble a sysroot that will be packaged into the disk image:

   `make sysroot`

   - This creates `build/sysroot/` and copies the kernel (`build/kernel/bkernel`), the musl init (if present) into `build/sysroot/bin/init`, and any `bash` found under `build/userspace/bash/bin/` into `build/sysroot/bin/`.

6. Create a bootable disk image:

   `make disk`

   - For creating a disk using the musl init test: `make disk-musl`.
   - To create an additional FAT16 disk image that QEMU will attach as a second IDE disk when present: `make fat-log-disk`.
   - The root partition is now sized from the assembled sysroot payload with an additional 30% buffer, so larger userlands automatically produce larger disk images.
   - There are helper wrappers: `tools/mkbluey_disk.py` and `scripts/build-disk-with-stage.sh` to assemble images including host-provided stage dirs.

7. (Optional) Build a read-only Linux BiscuitFS mounter:

    `make build/tools/mount_blueyfs`

    - Requires FUSE3 development headers on the host, typically `libfuse3-dev` on Debian/Ubuntu.
    - Example usage against the root partition inside the BlueyOS disk image:

       `build/tools/mount_blueyfs --start-sector 67584 build/blueyos-disk.img /mnt/blueyos -f`

    - This mounter is read-only and intended for inspection, copying files out, and debugging host-side images.

7. Run the image in QEMU:

   `make run`

   - The i386 launcher now writes guest serial output to `build/qemu-serial.log` by default, which captures kernel `kprintf()` output and early boot diagnostics.
   - To force the old stdio behavior instead, run: `SERIAL_MODE=stdio bash tools/qemu-run.sh`
   - To extract the persisted kernel syslog from the built disk image after a boot that reached the root filesystem, run: `make extract-kernel-log > build/kernel.log`
   - For non-default images or alternate destinations, use: `python3 tools/extract_kernel_log.py --output build/kernel.log build/blueyos-disk.img`

Default init choice
-------------------
- The build system now prefers the musl-backed test init as the default `/bin/init` when available.
- To make the musl init the active init payload, build it first:

   `make musl-init`

   Then run `make sysroot` and `make disk` as usual. If the musl init is not present the repository will fall back to the built-in `user/init` payload.

   `make run`

   or (manual):

   `BUILD_DIR=build bash tools/qemu-run.sh`

Debugging and GDB helpers
-------------------------
- There are a number of debug helpers under `tools/` (wrappers) and the relocated versions under `scripts/experimental/debug/`.
- Common helpers:
  - `tools/gdb-auto-attach.sh` (wrapper) -> `scripts/experimental/debug/gdb-auto-attach.sh`
  - `tools/gdb-on-oops.sh` (wrapper) -> `scripts/experimental/debug/gdb-on-oops.sh`
  - `tools/watch-eip.sh`, `tools/break-writers.sh`, `tools/debug-gdb.sh`, `tools/refined-debug.sh` now exec the scripts in `scripts/experimental/debug/`.

Cleaning
--------
- `make clean` removes generated build outputs in `build/`.
- The repository keeps source files in the repository root and `arch/`, `kernel/`, `lib/`, `tools/` and `scripts/` — compiled binaries and image artifacts are placed under `build/`.

Layout notes
------------
- Kernel build output: `build/kernel/bkernel`.
- Sysroot assembled at: `build/sysroot/`.
- Musl install / sysroot used by the helpers is `build/userspace/musl` by default (see `Makefile` and `scripts/build-bash.sh`).
- Experimental debug scripts: `scripts/experimental/debug/` (non-essential helpers collected there).

Advanced
--------
- To automate building a complete userland (readline/ncurses/bash) for musl, use `scripts/build-bash.sh` and then `make sysroot`.
- To reproduce the image-building sequence used by CI/dev workflows: build `tools-host`, build kernel, assemble sysroot, then `make disk`.

If something fails
------------------
- Check that required cross compilers exist (e.g. `i686-linux-gnu-gcc`) and that the `MUSL_PREFIX` used by the helpers points at an installed musl sysroot.
- Look at the `build/` tree, especially `build/qemu-serial.log`, or extract `/var/log/kernel.log` from the image with `make extract-kernel-log`.

Contributing
------------
- Keep generated artifacts out of the repo root — use `build/` for build outputs.
- Put throwaway and experimental helpers into `scripts/experimental/` rather than `tools/` when they're not required by projects' normal build targets.

Questions or preferred changes: open an issue or ping the maintainer — happy to make the BUILDING guide more prescriptive for your environment.
