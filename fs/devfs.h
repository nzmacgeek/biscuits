#pragma once
// BlueyOS devfs — "Bandit's Toolbelt"
// An in-memory virtual filesystem mounted at /dev.
// Provides synthesised device nodes (null, zero, random, tty, disks, …)
// without requiring mknod or a writable root filesystem.
//
// Episode ref: "Dad Baby" — every tool has its place.
// Bluey and all related characters are trademarks of Ludo Studio Pty Ltd,
// licensed by BBC Studios. BlueyOS is an unofficial fan/research project.

#include "vfs.h"

// Return the filesystem_t vtable for devfs.  Register with vfs_register_fs()
// before calling vfs_mount("/dev", "devfs", 0).
filesystem_t *devfs_get_filesystem(void);
