#!/usr/bin/env python3
"""Upload a local directory to the device (tar over ssh; the phone has no sftp).

    python os/sync.py os /home/user/os

Skips build/ and *.o so the device keeps its own objects.

Environment:

    GRAPHITE_HOST   device address (required)
    GRAPHITE_USER   ssh user (default: user)
    GRAPHITE_PASS   ssh password (required)
"""
import io
import os
import sys
import tarfile

import paramiko


def env(name, default=None, required=False):
    val = os.environ.get(name, default)
    if required and not val:
        sys.exit("error: %s is not set" % name)
    return val


HOST = env("GRAPHITE_HOST", required=True)
USER = env("GRAPHITE_USER", "user")
PWD = env("GRAPHITE_PASS", required=True)


def main():
    if len(sys.argv) < 3:
        print(__doc__)
        return 1
    local, remote = sys.argv[1], sys.argv[2]

    buf = io.BytesIO()
    n = 0
    with tarfile.open(fileobj=buf, mode="w:gz") as tar:
        for root, dirs, files in os.walk(local):
            dirs[:] = [d for d in dirs if d not in ("build", ".git", "__pycache__")]
            for f in files:
                if f.endswith(".o") or f.endswith(".pyc"):
                    continue
                p = os.path.join(root, f)
                arc = os.path.relpath(p, local).replace("\\", "/")
                tar.add(p, arcname=arc)
                n += 1
    data = buf.getvalue()

    c = paramiko.SSHClient()
    c.set_missing_host_key_policy(paramiko.AutoAddPolicy())
    c.connect(HOST, username=USER, password=PWD, timeout=20,
              banner_timeout=60, look_for_keys=False, allow_agent=False)

    remote_tar = "/tmp/graphite-sync.tar.gz"
    stdin, stdout, stderr = c.exec_command("cat > " + remote_tar, timeout=120)
    stdin.write(data)
    stdin.flush()
    stdin.channel.shutdown_write()
    stdout.read()
    stderr.read()

    cmd = ("mkdir -p %s && tar xzf %s -C %s && rm -f %s && echo SYNC_OK"
           % (remote, remote_tar, remote, remote_tar))
    stdin, stdout, stderr = c.exec_command(cmd, timeout=120)
    out = stdout.read().decode("utf-8", "replace").strip()
    err = stderr.read().decode("utf-8", "replace").strip()
    c.close()
    if "SYNC_OK" not in out:
        print("sync failed:", err or out)
        return 1
    print("uploaded %d files (%d KB) to %s:%s" % (n, len(data) // 1024, HOST, remote))
    return 0


if __name__ == "__main__":
    sys.exit(main())
