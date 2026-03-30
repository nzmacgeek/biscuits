This is a generic list of todos for the project. It is not exhaustive and may be updated over time as new tasks are identified or priorities change. 

Core Kernel:
- [ ] Implement support for multiple CPU cores (SMP)
- [ ] Add support for advanced power management (APM/ACPI)
- [ ] Implement a more robust memory management system (e.g. buddy allocator)
- [ ] Add support for demand paging and swapping to disk
- [ ] Implement a more feature-rich scheduler (e.g. priority-based, CFS)
- [ ] Add support for inter-process communication (IPC) mechanisms (e.g. pipes, message queues, shared memory)
- [ ] Implement a more complete set of system calls (e.g. fork, execve, wait, signal handling)
- [ ] Add support for dynamic kernel modules (loadable kernel modules)
- [ ] Timestamp and logging improvements (e.g. monotonic clock, high-resolution timers, structured logging)
- [ ] Implement a more robust and secure syscall interface (e.g. syscall numbers, argument validation, sandboxing)
- [ ] Refactor and clean up existing code for better readability and maintainability

Networking:
- [ ] Implement loopback network support (127.0.0.1)
- [ ] Add support for DHCP to automatically obtain IP configuration (will need to be a separate user-space daemon that interacts with the NE2000 driver, needs a name and will probably be a package)


Hardware Support:
- [ ] Add support for PS/2 mouse input
- [ ] Implement basic sound output (e.g. PC speaker)
- [ ] Add support for USB devices (e.g. flash drives, keyboards)
- [ ] Enable module loading for additional hardware drivers

Access Control and Security:
- [ ] Implement user groups and permissions for files and processes
- [ ] Add support for setuid/setgid bits on executables
- [ ] Implement proper user authentication and password hashing (e.g. bcrypt) in a shadow file
- [ ] Implement a simple firewall for network traffic filtering

Filesystem:
- [ ] Make sure ACLs and permissions are properly enforced in the VFS layer
- [ ] Implement support for symbolic links in the filesystem

Userland - some to be built in other repos:
- [ ] Enable init to be swapped out for a more full-featured init system (ours is called "claw")
- [ ] Start porting musl libc or a similar C library to enable more complex user-space applications
- [ ] Port bash or a similar shell to provide a more user-friendly command line interface
- [ ] Implement a package manager for installing additional software (dimsim)
  - [ ] Create a simple text editor (e.g. "blueyedit") to allow editing files from the shell
  - [ ] Port VIM or a similar editor to provide a more powerful editing experience
- [ ] Implement a simple web browser (e.g. "blueyweb") to allow browsing the internet
- [ ] Port openSSL or a similar library to enable secure network communication