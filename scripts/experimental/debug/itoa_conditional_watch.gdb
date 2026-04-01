set confirm off
set pagination off
set breakpoint pending on
set logging file /tmp/blueyos-refined-gdb.log
set logging overwrite on
set logging enabled on

target remote 127.0.0.1:1234

printf "Attached to paused QEMU on 127.0.0.1:1234\n"

watch fs_block_size
disable 1
commands
  silent
  printf "\n=== WATCH HIT: fs_block_size changed ===\n"
  printf "fs_block_size=0x%08x\n", fs_block_size
  info registers eip esp ebp eax ebx ecx edx esi edi
  bt full
  continue
end

watch *(unsigned int*)0x1232c0
disable 2
commands
  silent
  printf "\n=== WATCH HIT: 0x1232c0 written ===\n"
  info registers eip esp ebp eax ebx ecx edx esi edi
  x/12wx 0x1232c0
  bt full
  continue
end

break dir_lookup if name == 0 || dir_ino == 0
disable 3
commands
  silent
  printf "\n=== BREAK dir_lookup suspicious args ===\n"
  printf "fs_block_size=0x%08x\n", fs_block_size
  info args
  info locals
  info registers eip esp ebp eax ebx ecx edx esi edi
  bt full
  continue
end

break fs/blueyfs.c:629 if name == 0 || dir_ino == 0 || ino == 0 || ftype == 0 || namelen > 255 || dir_inode.mode == 0
disable 4
commands
  silent
  printf "\n=== BREAK dir_add_entry suspicious state ===\n"
  printf "fs_block_size=0x%08x\n", fs_block_size
  info args
  info locals
  info registers eip esp ebp eax ebx ecx edx esi edi
  bt full
  continue
end

break biscuitfs_read_cb if fs_block_size == 0
disable 5
commands
  silent
  printf "\n=== BREAK biscuitfs_read_at_cb div with fs_block_size==0 ===\n"
  printf "fs_block_size=0x%08x\n", fs_block_size
  info locals
  info registers eip esp ebp eax ebx ecx edx esi edi
  bt full
  continue
end

break biscuitfs_read_at_cb if fs_block_size == 0
disable 6
commands
  silent
  printf "\n=== BREAK biscuitfs_read_at_cb mod with fs_block_size==0 ===\n"
  printf "fs_block_size=0x%08x\n", fs_block_size
  info locals
  info registers eip esp ebp eax ebx ecx edx esi edi
  bt full
  continue
end

break biscuitfs_write_cb if fs_block_size == 0
disable 7
commands
  silent
  printf "\n=== BREAK biscuitfs_write_cb div with fs_block_size==0 ===\n"
  printf "fs_block_size=0x%08x\n", fs_block_size
  info locals
  info registers eip esp ebp eax ebx ecx edx esi edi
  bt full
  continue
end

break biscuitfs_dump_dir
commands
  silent
  printf "\n=== BREAK biscuitfs_dump_dir: enabling post-mount traps ===\n"
  printf "dir_ino=%u label=%p fs_block_size=0x%08x\n", dir_ino, label, fs_block_size
  enable 1 2 3 4 5 6 7
  disable 8
  continue
end

break path_to_inode if path == 0
disable 9
commands
  silent
  printf "\n=== BREAK path_to_inode null path ===\n"
  printf "fs_block_size=0x%08x\n", fs_block_size
  info args
  info locals
  info registers eip esp ebp eax ebx ecx edx esi edi
  bt full
  continue
end

continue
