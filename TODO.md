This is a generic list of todos for the project. It is not exhaustive and may be updated over time as new tasks are identified or priorities change. 

Networking:
- [ ] Implement loopback network support (127.0.0.1)

Hardware Support:
- [ ] Add support for PS/2 mouse input
- [ ] Implement basic sound output (e.g. PC speaker)
- [ ] Add support for USB devices (e.g. flash drives, keyboards)
- [ ] Enable module loading for additional hardware drivers

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