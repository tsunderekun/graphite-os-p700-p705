#!/usr/bin/env python3
"""Download one file from the device (no sftp server; reads it over exec).

    python os/pull.py /path/on/device ./local [sudo]

With the optional third argument "sudo" the cat runs through sudo -S and the
password is fed on stdin from GRAPHITE_PASS.

Environment:

    GRAPHITE_HOST   device address (required)
    GRAPHITE_USER   ssh user (default: user)
    GRAPHITE_PASS   ssh password (required)
"""
import os
import sys

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
        print("usage: pull.py <remote> <local> [sudo]")
        return 1
    remote, local = sys.argv[1], sys.argv[2]
    sudo = len(sys.argv) > 3 and sys.argv[3] == "sudo"
    c = paramiko.SSHClient()
    c.set_missing_host_key_policy(paramiko.AutoAddPolicy())
    c.connect(HOST, username=USER, password=PWD, timeout=20,
              banner_timeout=60, look_for_keys=False, allow_agent=False)
    cmd = "cat " + remote
    if sudo:
        # -S reads the password from stdin; keep it out of the command line.
        cmd = "sudo -S -p '' cat " + remote
    stdin, stdout, stderr = c.exec_command(cmd, timeout=120)
    if sudo:
        stdin.write(PWD + "\n")
        stdin.flush()
    data = stdout.read()
    err = stderr.read()
    c.close()
    with open(local, "wb") as f:
        f.write(data)
    print("pulled %d bytes -> %s%s" % (len(data), local,
          (" (" + err.decode("utf-8", "replace").strip() + ")") if err else ""))
    return 0


if __name__ == "__main__":
    sys.exit(main())
