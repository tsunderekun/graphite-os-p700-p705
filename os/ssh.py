#!/usr/bin/env python3
"""Run one command on the device over SSH.

Connection settings come from the environment; there are no built-in defaults
for the address or the password:

    GRAPHITE_HOST   device address (USB RNDIS gadget or Wi-Fi)
    GRAPHITE_USER   ssh user (default: user)
    GRAPHITE_PASS   ssh password (required)

    python os/ssh.py 'uname -a'
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
    cmd = " ".join(sys.argv[1:]) or "uptime"
    c = paramiko.SSHClient()
    c.set_missing_host_key_policy(paramiko.AutoAddPolicy())
    c.connect(HOST, username=USER, password=PWD, timeout=15,
              banner_timeout=60, look_for_keys=False, allow_agent=False)
    stdin, stdout, stderr = c.exec_command(cmd, timeout=60)
    out = stdout.read().decode("utf-8", "replace")
    err = stderr.read().decode("utf-8", "replace")
    c.close()
    print(out, end="")
    if err:
        print(err, file=sys.stderr, end="")
    return 0


if __name__ == "__main__":
    sys.exit(main())
