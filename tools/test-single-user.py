#!/usr/bin/env python3
"""
test-single-user.py - boot BlueyOS in single-user mode and verify command execution.
Launches QEMU with serial on a Unix domain socket, waits for the sh prompt,
sends 'echo SINGLE_USER_OK', and prints the response.
"""
import subprocess
import socket
import time
import sys
import os
import select

DISK_IMAGE    = os.path.join(os.path.dirname(__file__), "..", "build", "blueyos-disk.img")
SERIAL_SOCK   = "/tmp/blueyos-test-serial.sock"
BOOT_TIMEOUT  = 120   # seconds to wait for shell prompt
CMD_TIMEOUT   = 15    # seconds to wait for command output

PROMPT        = b"sh-5.2#"
ECHO_MARK     = b"__SHELL_ECHO_MARK__"
OUTPUT_MARK   = b"__SHELL_OUTPUT_MARK__"
TEST_CMD      = b"echo __SHELL_OUTPUT_MARK__ # __SHELL_ECHO_MARK__\r\n"

def remove_sock():
    try:
        os.remove(SERIAL_SOCK)
    except FileNotFoundError:
        pass

def main():
    remove_sock()

    disk_image = os.path.realpath(DISK_IMAGE)
    if not os.path.exists(disk_image):
        print(f"ERROR: disk image not found: {disk_image}")
        sys.exit(1)

    qemu_cmd = [
        "qemu-system-i386",
        "-drive", f"file={disk_image},format=raw,if=ide,index=0",
        "-boot", "c",
        "-m", "1024M",
        "-display", "none",
        "-netdev", "user,id=usernet",
        "-device", "ne2k_pci,netdev=usernet",
        "-serial", f"unix:{SERIAL_SOCK},server,nowait",
        "-no-reboot",
        "-no-shutdown",
    ]

    print(f"[*] Starting QEMU with serial socket {SERIAL_SOCK} ...")
    qemu = subprocess.Popen(qemu_cmd, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)

    # Wait for socket to appear
    deadline = time.time() + 15
    while not os.path.exists(SERIAL_SOCK):
        if time.time() > deadline:
            qemu.kill()
            print("ERROR: serial socket never appeared")
            sys.exit(1)
        time.sleep(0.1)

    # Connect (socket path may exist before QEMU is accepting connections)
    deadline = time.time() + 15
    while True:
        s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        try:
            s.connect(SERIAL_SOCK)
            break
        except (ConnectionRefusedError, FileNotFoundError):
            s.close()
            if time.time() > deadline:
                qemu.kill()
                remove_sock()
                print("ERROR: serial socket never accepted connections")
                sys.exit(1)
            time.sleep(0.1)
    s.setblocking(False)
    print("[*] Connected to serial socket")

    # Read until prompt
    buf = b""
    deadline = time.time() + BOOT_TIMEOUT
    print("[*] Waiting for shell prompt ...")
    while PROMPT not in buf:
        if time.time() > deadline:
            qemu.kill()
            remove_sock()
            print("ERROR: timed out waiting for shell prompt")
            print("--- Last 2000 bytes of output ---")
            sys.stdout.buffer.write(buf[-2000:])
            sys.exit(1)
        rlist, _, _ = select.select([s], [], [], 1.0)
        if rlist:
            chunk = s.recv(4096)
            if chunk:
                buf += chunk

    # Print boot tail for diagnostics
    idx = buf.rfind(PROMPT)
    context = buf[max(0, idx-200):idx+len(PROMPT)].decode("latin-1", errors="replace")
    print("[*] Shell prompt reached:")
    print(context)

    # Send command
    print(f"\n[*] Sending: {TEST_CMD.strip().decode()}")
    s.sendall(TEST_CMD)

    # Read response until we observe both command echo and command output.
    # The echoed command includes ECHO_MARK in a shell comment, while
    # OUTPUT_MARK should appear once in the echoed command and once in output.
    response = b""
    deadline = time.time() + CMD_TIMEOUT
    while not (ECHO_MARK in response and response.count(OUTPUT_MARK) >= 2):
        if time.time() > deadline:
            print("ERROR: timed out waiting for echoed command + command output")
            break
        rlist, _, _ = select.select([s], [], [], 1.0)
        if rlist:
            chunk = s.recv(4096)
            if chunk:
                response += chunk

    s.close()
    qemu.terminate()
    try:
        qemu.wait(timeout=5)
    except subprocess.TimeoutExpired:
        qemu.kill()
    remove_sock()

    decoded = response.decode("latin-1", errors="replace")
    echo_mark_text = ECHO_MARK.decode()
    output_mark_text = OUTPUT_MARK.decode()
    lines = decoded.splitlines()
    echo_lines = [line for line in lines if echo_mark_text in line]
    output_lines = [line for line in lines if line.strip() == output_mark_text]

    if echo_lines and output_lines:
        print("[SUCCESS] Command executed successfully in single-user mode!")
        print("--- Echoed command line(s) ---")
        for line in echo_lines:
            print(line)
        print("--- Output line(s) ---")
        for line in output_lines:
            print(line)
        print("--- Response ---")
        print(decoded.strip())
        sys.exit(0)
    else:
        print("[FAIL] Did not observe both command echo and command output.")
        print(f"[FAIL] Echo lines found: {len(echo_lines)}")
        print(f"[FAIL] Output-only lines found: {len(output_lines)}")
        print("--- Response ---")
        print(decoded)
        sys.exit(1)

if __name__ == "__main__":
    main()
