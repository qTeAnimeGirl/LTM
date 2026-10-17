#!/usr/bin/env python3
import os
import pty
import select
import sys
import time
import termios

def run_lifecycle_test(cwd, timeout_sec=600):
    print(f"[*] Starting bash session in {cwd}...")
    master_fd, slave_fd = pty.openpty()

    env = os.environ.copy()
    env["TERM"] = "xterm-256color"

    pid = os.fork()
    if pid == 0:
        # Child process
        os.close(master_fd)
        os.setsid()
        os.dup2(slave_fd, 0)
        os.dup2(slave_fd, 1)
        os.dup2(slave_fd, 2)
        if slave_fd > 2:
            os.close(slave_fd)
        os.chdir(cwd)
        os.execve("./ltm", ["./ltm", "/usr/bin/bash"], env)
        sys.exit(1)

    # Parent process
    os.close(slave_fd)
    
    # Set non-blocking on master_fd
    import fcntl
    fl = fcntl.fcntl(master_fd, fcntl.F_GETFL)
    fcntl.fcntl(master_fd, fcntl.F_SETFL, fl | os.O_NONBLOCK)

    buffer = ""
    start_time = time.time()

    def wait_for_pattern(pattern, timeout=120):
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
                    if pattern in buffer:
                        return True
                except (OSError, EOFError):
                    break
        return False

    def send_cmd(cmd):
        nonlocal buffer
        buffer = ""
        print(f"\n[TEST_PTY] Sending command: {cmd.strip()}")
        os.write(master_fd, cmd.encode("utf-8"))

    try:
        # Wait for initial bash prompt
        print("[*] Waiting for bash prompt...")
        if not wait_for_pattern("[root@ltm-mac", timeout=30):
            print("[-] Timeout waiting for bash prompt!")
            return False

        # Step 1: pacman -Syy
        send_cmd("pacman -Syy\n")
        if not wait_for_pattern("[root@ltm-mac", timeout=120):
            print("[-] Timeout during pacman -Syy!")
            return False

        # Step 2: pacman -S --noconfirm sudo
        send_cmd("pacman -S --noconfirm sudo\n")
        if not wait_for_pattern("[root@ltm-mac", timeout=180):
            print("[-] Timeout during pacman -S sudo!")
            return False

        # Step 3: pacman -S --noconfirm fastfetch
        send_cmd("pacman -S --noconfirm fastfetch\n")
        if not wait_for_pattern("[root@ltm-mac", timeout=180):
            print("[-] Timeout during pacman -S fastfetch!")
            return False

        # Step 4: pacman -Su --noconfirm
        send_cmd("pacman -Su --noconfirm\n")
        # Wait up to 300s for pacman -Su to finish or crash
        if not wait_for_pattern("[root@ltm-mac", timeout=300):
            print("[-] Timeout during pacman -Su!")
            return False

        print("\n[+] All steps completed successfully in single bash session!")
        send_cmd("exit\n")
        return True

    finally:
        try:
            os.kill(pid, 9)
        except OSError:
            pass
        os.close(master_fd)

if __name__ == "__main__":
    cwd = sys.argv[1] if len(sys.argv) > 1 else os.getcwd()
    success = run_lifecycle_test(cwd)
    sys.exit(0 if success else 1)
