set pagination off
# watchpoint on suspect stack slot observed in latest run
watch *(unsigned int*)0x1232c0
commands
 silent
 printf "=== WATCH HIT: 0x1232c0 written ===\n"
 info registers
 bt full
 x/32wx $esp
 x/32wx 0x125330
 x/64wx 0x12c360
 continue
end
# Break on key kernel points and dump useful context
# isr_handler
break *0x0010115e
commands
 silent
 printf "=== BREAK isr_handler ===\n"
 info registers
 bt full
 x/32wx $esp
 x/32wx 0x125330
 x/64wx 0x12c360
 continue
end

# syslog_itoa -- print args at entry
break *0x00108e92
commands
 silent
 printf "=== BREAK syslog_itoa ===\n"
 info registers
 printf "stack args (ret, arg1, arg2):\n"
 x/wx $esp
 x/wx $esp+4
 x/wx $esp+8
 bt
 x/32wx $esp
 x/32wx 0x125330
 x/64wx 0x12c360
 continue
end

# kvprintf_impl - capture fmt and va_list pointer before formatting
break kvprintf_impl
commands
 silent
 printf "=== BREAK kvprintf_impl ===\n"
 info registers
 printf "stack (ret, emit, ctx, fmt, ap):\n"
 x/wx $esp
 x/wx $esp+4
 x/wx $esp+8
 x/wx $esp+12
 printf "fmt ptr = %p\n", *(void**)($esp+8)
 x/s *(char**)($esp+8)
 bt full
 x/32wx $esp
 x/32wx 0x125330
 x/64wx 0x12c360
 continue
end

# kprintf_emit_number - inspect numeric formatting args
break kprintf_emit_number
commands
 silent
 printf "=== BREAK kprintf_emit_number ===\n"
 info registers
 printf "stack (ret, emit, ctx, value, negative, base, upper, width):\n"
 x/wx $esp
 x/wx $esp+4
 x/wx $esp+8
 x/8wx $esp+12
 bt full
 x/32wx $esp
 x/32wx 0x125330
 x/64wx 0x12c360
 continue
end

# syslog_vsnprintf - capture caller args used by syslog path
break syslog_vsnprintf
commands
 silent
 printf "=== BREAK syslog_vsnprintf ===\n"
 info registers
 printf "stack (ret, out, sz, fmt, ap):\n"
 x/wx $esp
 x/wx $esp+4
 x/wx $esp+8
 x/wx $esp+12
 printf "fmt ptr = %p\n", *(void**)($esp+8)
 x/s *(char**)($esp+8)
 bt full
 x/32wx $esp
 x/32wx 0x125330
 x/64wx 0x12c360
 continue
end

# syslog_write
break *0x001091b5
commands
 silent
 printf "=== BREAK syslog_write ===\n"
 info registers
 bt full
 x/32wx $esp
 x/32wx 0x125330
 x/64wx 0x12c360
 continue
end

# vfs_write
break *0x0010c774
commands
 silent
 printf "=== BREAK vfs_write ===\n"
 info registers
 bt full
 x/32wx $esp
 x/32wx 0x125330
 x/64wx 0x12c360
 continue
end

# fat_vfs_write
break *0x0010dbbf
commands
 silent
 printf "=== BREAK fat_vfs_write ===\n"
 info registers
 bt full
 x/32wx $esp
 x/32wx 0x125330
 x/64wx 0x12c360
 continue
end

# biscuitfs_write_cb
break *0x0010f300
commands
 silent
 printf "=== BREAK biscuitfs_write_cb ===\n"
 info registers
 bt full
 x/32wx $esp
 x/32wx 0x125330
 x/64wx 0x12c360
 continue
end

# kprintf (entry)
break *0x0011420a
commands
 silent
 printf "=== BREAK kprintf ===\n"
 info registers
 bt full
 x/32wx $esp
 x/32wx 0x125330
 x/64wx 0x12c360
 continue
end

# keyboard_poll
break *0x0010ad6a
commands
 silent
 printf "=== BREAK keyboard_poll ===\n"
 info registers
 bt full
 x/32wx $esp
 x/32wx 0x125330
 x/64wx 0x12c360
 continue
end

# itoa -- inspect args and buffer at entry
break *0x00113898
commands
 silent
 printf "=== BREAK itoa (entry) ===\n"
 info registers
 printf "ret/arg1/arg2/arg3:\n"
 x/wx $esp
 x/wx $esp+4
 x/wx $esp+8
 x/wx $esp+12
 set $buf = *(void** )($esp+8)
 printf "buf ptr = 0x%08x\n", $buf
 x/s $buf
 disassemble $pc-32, $pc+64
 continue
end

# single-step the store at itoa+170 (0x001138b0) to observe write
break *0x001138b0
commands
 silent
 printf "=== BREAK itoa STORE (0x001138b0) ===\n"
 info registers
 printf "EBX(ptr) = 0x%08x, EDX(val) = 0x%08x\n", $ebx, $edx
 printf "mem before around EBX:\n"
 x/8wx $ebx-16
 printf "single-stepping the store instruction...\n"
 stepi
 printf "mem after around EBX:\n"
 x/8wx $ebx-16
 bt full
 continue
end

# End of script
printf "GDB breakpoint script loaded.\n"
