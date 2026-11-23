#!/usr/bin/env python3
import ftplib, hashlib, os, socket, sys, time

HOST = "169.254.1.1"
IMG = "/home/sean/retrofw-2.4/rootfs-2.4.squashfs"

size = os.path.getsize(IMG)
exp_md5 = hashlib.md5(open(IMG, "rb").read()).hexdigest()
print(f"Local rootfs-2.4.squashfs: {size} bytes ({size/1048576:.2f} MiB), md5={exp_md5}")

s = socket.create_connection((HOST, 23), timeout=5)
s.settimeout(0.5)
def wait_for(tok, timeout=30.0):
    end = time.time() + timeout
    b = b""
    while time.time() < end:
        try:
            d = s.recv(4096)
            if not d: break
            b += d
            if tok in b: break
        except: pass
    return b
def run(cmd, timeout=30.0):
    s.sendall((cmd + "\r\n").encode())
    return wait_for(b"# ", timeout).decode("utf-8", "replace")

wait_for(b"login:")
s.sendall(b"root\r\n")
wait_for(b"\x1b[6n", 1.5)
s.sendall(b"\x1b[24;80R")
wait_for(b"# ", 2.0)

print("1. Cleaning partial file & moving stock backup to /home/retrofw/ (keeping /boot >65 MiB free for atomic swap)...")
print(run("rm -f /boot/rootfs-2.4.squashfs"))
print(run("if [ -f /boot/rootfs.squashfs.2.3.bak ]; then [ -f /home/retrofw/rootfs.squashfs.2.3.bak ] || cp /boot/rootfs.squashfs.2.3.bak /home/retrofw/rootfs.squashfs.2.3.bak; rm -f /boot/rootfs.squashfs.2.3.bak; fi; sync"))
print(run("ls -lh /boot /home/retrofw/rootfs.squashfs.2.3.bak && df -h /boot"))

print("2. Uploading rootfs-2.4.squashfs to /boot/rootfs-2.4.squashfs via FTP (~25s)...")
t0 = time.time()
ftp = ftplib.FTP(HOST, timeout=60)
ftp.login("root", "")
with open(IMG, "rb") as f:
    ftp.storbinary("STOR /boot/rootfs-2.4.squashfs", f, blocksize=65536)
ftp.quit()
dt = time.time() - t0
print(f"   Uploaded in {dt:.1f}s ({size/1048576/dt:.2f} MB/s)")

print("3. Flushing & verifying MD5 of /boot/rootfs-2.4.squashfs BEFORE swap...")
run("sync; echo 3 > /proc/sys/vm/drop_caches", timeout=15.0)
out = run("md5sum /boot/rootfs-2.4.squashfs", timeout=30.0)
print(out)
assert exp_md5 in out, f"MD5 mismatch on /boot/rootfs-2.4.squashfs! Expected {exp_md5}"

print("4. Atomically swapping /boot/rootfs-2.4.squashfs -> /boot/rootfs.squashfs...")
print(run("mv -f /boot/rootfs-2.4.squashfs /boot/rootfs.squashfs && sync && sync"))

print("5. Final drop_caches and physical MD5 verification of /boot/rootfs.squashfs...")
run("echo 3 > /proc/sys/vm/drop_caches; sleep 1", timeout=45.0)
out = run("ls -lh /boot && md5sum /boot/rootfs.squashfs", timeout=30.0)
print(out)
assert exp_md5 in out, f"FINAL READ-BACK MD5 MISMATCH! Expected {exp_md5}"
print("RETROFW 2.4 ROOTFS DEPLOYED & PHYSICALLY VERIFIED OK:", exp_md5)
s.close()
