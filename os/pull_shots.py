#!/usr/bin/env python3
"""Pull /tmp/shot-*.xwd from the device as a tar stream.

    python os/pull_shots.py artifacts/host/device

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
    outdir = sys.argv[1] if len(sys.argv) > 1 else "artifacts/host/device"
    pattern = sys.argv[2] if len(sys.argv) > 2 else "shot-*.xwd"
    os.makedirs(outdir, exist_ok=True)
    c = paramiko.SSHClient()
    c.set_missing_host_key_policy(paramiko.AutoAddPolicy())
    c.connect(HOST, username=USER, password=PWD, timeout=20,
              banner_timeout=60, look_for_keys=False, allow_agent=False)
    cmd = "cd /tmp && tar czf - " + pattern + " 2>/dev/null"
    stdin, stdout, stderr = c.exec_command(cmd, timeout=120)
    data = stdout.read()
    c.close()
    if not data:
        print("no shots")
        return 1
    with tarfile.open(fileobj=io.BytesIO(data), mode="r:gz") as tar:
        members = [m for m in tar.getmembers() if m.name.endswith(".xwd")]
        for m in members:
            f = tar.extractfile(m)
            if not f:
                continue
            with open(os.path.join(outdir, os.path.basename(m.name)), "wb") as out:
                out.write(f.read())
    print("pulled %d shots -> %s" % (len(members), outdir))
    return 0


if __name__ == "__main__":
    sys.exit(main())
