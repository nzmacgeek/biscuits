set pagination off
set logging file /tmp/blueyos-target-breaks.log
target remote 127.0.0.1:1234

# Generic breakpoints used during development
break isr_handler
break vfs_write
break syslog_write

continue
