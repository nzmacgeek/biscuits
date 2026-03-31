set confirm off
set pagination off
set breakpoint pending on
set logging file /tmp/wide_writer_poll.log
set logging overwrite on
set logging enabled on

target remote 127.0.0.1:1234

printf "Connected to paused QEMU GDB stub. Arming widened writer traps.\n"

watch *(unsigned int*)0x1232c0
commands 1
  silent
  printf "\n=== WATCH HIT: 0x1232c0 written ===\n"
  info registers eax ebx ecx edx esi edi esp ebp eip
  x/16wx $esp
  x/16wx 0x1232a0
  bt full
  continue
end

watch *(unsigned int*)0x123274
commands 2
  silent
  printf "\n=== WATCH HIT: 0x123274 written ===\n"
  info registers eax ebx ecx edx esi edi esp ebp eip
  x/16wx $esp
  x/16wx 0x123250
  bt full
  continue
end

break *0x001138b0 if ((unsigned int)$ebx >= 0x123000) && ((unsigned int)$ebx < 0x123600)
commands 3
  silent
  printf "\n=== CONDITIONAL itoa STORE hit ===\n"
  printf "EBX(ptr)=0x%08x EDX(val)=0x%08x\n", $ebx, $edx
  info registers eax ebx ecx edx esi edi esp ebp eip
  x/16wx $esp
  x/12wx $ebx-16
  bt full
  continue
end

break *0x0010c774
commands 4
  silent
  printf "\n=== BREAK vfs_write ===\n"
  info registers eax ebx ecx edx esi edi esp ebp eip
  x/16wx $esp
  bt full
  continue
end

break *0x0010dbbf
commands 5
  silent
  printf "\n=== BREAK fat_vfs_write ===\n"
  info registers eax ebx ecx edx esi edi esp ebp eip
  x/16wx $esp
  bt full
  continue
end

break *0x0010f300
commands 6
  silent
  printf "\n=== BREAK biscuitfs_write_cb ===\n"
  info registers eax ebx ecx edx esi edi esp ebp eip
  x/16wx $esp
  bt full
  continue
end

break *0x001091b5
commands 7
  silent
  printf "\n=== BREAK syslog_write ===\n"
  info registers eax ebx ecx edx esi edi esp ebp eip
  x/16wx $esp
  bt full
  continue
end

printf "Widened writer trap script loaded. Continuing boot.\n"
continue