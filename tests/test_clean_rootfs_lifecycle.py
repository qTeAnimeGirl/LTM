#!/usr/bin/env python3
import os
import pty
import select
import sys
import time
import shutil

def run_clean_test(cwd, timeout_sec=900):
    print(f"==================================================", flush=True)
    print(f"[*] Starting Clean Rootfs Lifecycle Test in {cwd}", flush=True)
    print(f"==================================================", flush=True)

    arch_rootfs = os.path.join(cwd, "arch_rootfs")
    if os.path.exists(arch_rootfs):
        print(f"[*] Deleting existing arch_rootfs to force clean install...", flush=True)
        import subprocess
        subprocess.run(f"chmod -R u+w '{arch_rootfs}' 2>/dev/null || true; rm -rf '{arch_rootfs}'", shell=True)
        if os.path.exists(arch_rootfs):
            subprocess.run(f"find '{arch_rootfs}' -exec chmod 777 {{}} + 2>/dev/null || true; rm -rf '{arch_rootfs}'", shell=True)

    env = os.environ.copy()
    env["TERM"] = "xterm-256color"

    pid, master_fd = pty.fork()
    if pid == 0:
        os.chdir(cwd)
        os.execve("./ltm", ["./ltm", "/usr/bin/bash"], env)
        os._exit(1)

    import fcntl
    fl = fcntl.fcntl(master_fd, fcntl.F_GETFL)
    fcntl.fcntl(master_fd, fcntl.F_SETFL, fl | os.O_NONBLOCK)

    buffer = ""

    def wait_for_pattern(pattern, timeout=180, interactive_respond=None):
        nonlocal buffer
        end_time = time.time() + timeout
        while time.time() < end_time:
            r, _, _ = select.select([master_fd], [], [], 0.5)
            if r:
                try:
                    data = os.read(master_fd, 4096).decode("utf-8", errors="replace")
                    sys.stdout.write(data)
                    sys.stdout.flush()
                    buffer += data

                    if interactive_respond:
                        for prompt_txt, reply in interactive_respond:
                            if prompt_txt in buffer:
                                print(f"\n[INTERACTIVE] Responding with '{reply.strip()}' to prompt '{prompt_txt}'", flush=True)
                                os.write(master_fd, reply.encode("utf-8"))
                                buffer = buffer.replace(prompt_txt, "")

                    if pattern in buffer:
                        return True
                except (OSError, EOFError):
                    break
        return False

    def send_cmd(cmd):
        nonlocal buffer
        buffer = ""
        print(f"\n[TEST_PTY] >>> Sending: {cmd.strip()}", flush=True)
        os.write(master_fd, cmd.encode("utf-8"))

    try:
        # Wait for rootfs installation and initial bash prompt
        print("[*] Waiting for rootfs installation and bash prompt (up to 300s)...", flush=True)
        if not wait_for_pattern("[root@ltm-mac", timeout=300):
            print("[-] Timeout waiting for rootfs installer / bash prompt!", flush=True)
            return False

        # Step 1: pacman -Syy
        send_cmd("pacman -Syy\n")
        if not wait_for_pattern("[root@ltm-mac", timeout=180):
            print("[-] Timeout during pacman -Syy!", flush=True)
            return False

        # Step 2: pacman -S sudo (interactive y prompt)
        send_cmd("pacman -S sudo\n")
        if not wait_for_pattern("[root@ltm-mac", timeout=180, interactive_respond=[("Proceed with installation? [Y/n]", "y\n")]):
            print("[-] Timeout during pacman -S sudo!", flush=True)
            return False

        # Step 3: pacman -S fastfetch (interactive y prompt)
        send_cmd("pacman -S fastfetch\n")
        if not wait_for_pattern("[root@ltm-mac", timeout=180, interactive_respond=[("Proceed with installation? [Y/n]", "y\n")]):
            print("[-] Timeout during pacman -S fastfetch!", flush=True)
            return False

        # Step 4: pacman -Su (interactive y prompt)
        send_cmd("pacman -Su\n")
        if not wait_for_pattern("[root@ltm-mac", timeout=900, interactive_respond=[("Proceed with installation? [Y/n]", "y\n"), ("[Y/n]", "y\n")]):
            print("[-] Timeout during pacman -Su!", flush=True)
            return False

        # Step 5: verify fastfetch execution
        send_cmd("fastfetch --version\n")
        if not wait_for_pattern("[root@ltm-mac", timeout=60):
            print("[-] Timeout during fastfetch execution!", flush=True)
            return False

        # Step 6: verify sudo execution
        send_cmd("sudo --version\n")
        if not wait_for_pattern("[root@ltm-mac", timeout=60):
            print("[-] Timeout during sudo execution!", flush=True)
            return False

        print("\n[+] FULL CLEAN ROOTFS LIFECYCLE PASSED SUCCESSFULLY!", flush=True)
        send_cmd("exit\n")
        return True

    finally:
        try:
            os.kill(pid, 9)
        except OSError:
            pass
        os.close(master_fd)

if __name__ == "__main__":
    cwd = sys.argv[1] if len(sys.argv) > 1 else "/Users/qteanimegirl/Documents/Test_LTM"
    success = run_clean_test(cwd)
    sys.exit(0 if success else 1)
