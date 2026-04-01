set pagination off
set logging file /tmp/blueyos-catch-writers.log
target remote 127.0.0.1:1234

# Simple set of breakpoints for writer paths
break vfs_write
commands
  printf "Hit vfs_write\n"
  bt
  continue
end

break fat_vfs_write
commands
  printf "Hit fat_vfs_write\n"
  bt
  continue
end

continue
