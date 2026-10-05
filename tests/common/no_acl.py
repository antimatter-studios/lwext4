#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-3-Clause
"""Remove the POSIX ACLs of a directory tree, as `setfacl -R -b` does.

Usage: no_acl.py <dir>
       no_acl.py --plant <dir>

mke2fs -d copies the ACLs of the files it adds into the image, as
system.posix_acl_* xattrs. A tree made in a checkout whose directories have
default ACLs inherits them, and then the image depends on the machine (fork
issue #173). The containers have no setfacl (package acl); the xattr calls
need nothing but Python.

--plant gives <dir> a default ACL with a named user entry, as such a
checkout has, so that what is made in it inherits ACLs on every machine
and a test proves they are removed. It does nothing on a filesystem
without ACLs.

Both do nothing where Python has no xattr calls (they are Linux only):
macOS has no system.posix_acl_* xattrs for mke2fs to copy.
"""
import errno
import os
import struct
import sys

NAMES = ("system.posix_acl_access", "system.posix_acl_default")
IGNORE = (errno.ENODATA, errno.ENOTSUP, errno.EPERM)


def strip(path):
    for name in NAMES:
        try:
            os.removexattr(path, name, follow_symlinks=False)
        except OSError as e:
            # no such ACL, or a filesystem without xattrs or ACLs
            if e.errno not in IGNORE:
                raise


def plant(path):
    # posix_acl_xattr format: version 2, then (tag, perm, id) entries in
    # tag order: user::rwx, user:<uid>:rwx, group::r-x, mask::rwx, other::r-x
    entries = [(0x01, 7, 0xFFFFFFFF), (0x02, 7, os.getuid()),
               (0x04, 5, 0xFFFFFFFF), (0x10, 7, 0xFFFFFFFF),
               (0x20, 5, 0xFFFFFFFF)]
    acl = struct.pack("<I", 2) + b"".join(
        struct.pack("<HHI", *e) for e in entries)
    try:
        os.setxattr(path, "system.posix_acl_default", acl)
    except OSError as e:
        if e.errno not in IGNORE:
            raise


if not hasattr(os, "setxattr"):
    sys.exit(0)
if sys.argv[1] == "--plant":
    plant(sys.argv[2])
    sys.exit(0)
root = sys.argv[1]
strip(root)
for top, dirs, files in os.walk(root):
    for name in dirs + files:
        path = os.path.join(top, name)
        if not os.path.islink(path):
            strip(path)
