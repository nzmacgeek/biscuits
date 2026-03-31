// BlueyOS Kernel Main Entry Point
// "It's a big day!" - Bluey Heeler (every episode, without fail)
//
// ⚠️  VIBE CODED RESEARCH PROJECT — NOT FOR PRODUCTION USE ⚠️
// This OS was created by an AI agent for learning and research purposes.
// See README.md, SECURITY.md and TESTING.md for full details.
//
// Bluey and all related characters are trademarks of Ludo Studio Pty Ltd,
// licensed by BBC Studios. BlueyOS is an unofficial fan/research project
// with no affiliation to Ludo Studio or the BBC.
//
// Episode refs: Hammerbarn, Takeaway, Camping, The Creek, Magic Xylophone
#include "../include/types.h"
#include "../include/bluey.h"
#include "../include/version.h"
#include "../lib/stdio.h"
#include "../lib/string.h"
#include "../drivers/vga.h"
#include "gdt.h"
#include "idt.h"
#include "isr.h"
#include "irq.h"
#include "timer.h"
#include "kheap.h"
#include "paging.h"
#include "bootui.h"
#include "bootargs.h"
#include "process.h"
#include "rtc.h"
#include "rootfs.h"
#include "scheduler.h"
#include "signal.h"
#include "syscall.h"
#include "multiuser.h"
#include "smp.h"
#include "sysinfo.h"
#include "tty.h"
#include "elf.h"
#include "swap.h"
#include "module.h"
#include "../drivers/keyboard.h"
#include "../drivers/ata.h"
#include "../drivers/driver.h"
#include "../drivers/modules.h"
#include "../drivers/net/network.h"
#include "../drivers/net/ne2000.h"
#include "../drivers/net/loopback.h"
#include "../drivers/vt100.h"
#include "../fs/vfs.h"
#include "../fs/fat.h"
#include "../fs/blueyfs.h"
#include "../net/tcpip.h"
#include "../shell/shell.h"
#include "syslog.h"
#include "netcfg.h"
#include "devev.h"

// Kernel end symbol from linker script
extern uint32_t kernel_end;

static uint32_t i386_multiboot_ram_mb(const uint32_t *mboot_info) {
    if (!mboot_info) {
        return 0;
    }

    if ((mboot_info[0] & 0x1u) == 0) {
        return 0;
    }

    return (mboot_info[2] + 1024u) / 1024u;
}

// ---------------------------------------------------------------------------
// bluey_panic - called by PANIC() macro everywhere in the kernel
// "Oh no! [Bandit voice]: KERNEL PANIC!" - the worst playdate outcome
// ---------------------------------------------------------------------------
void bluey_panic(const char *msg) {
    __asm__ volatile("cli");
    vga_set_color(VGA_WHITE, VGA_RED);
    kprintf("\n\n");
    kprintf("  *** Oh no! [Bandit voice]: KERNEL PANIC! ***\n");
    kprintf("  %s\n", msg);
    kprintf("  The playdate is over. Please reset the computer.\n");
    kprintf("  (BlueyOS research OS - see SECURITY.md for known limitations)\n");
    kprintf("\n");
    __asm__ volatile("hlt");
    for (;;) {}
}

// ---------------------------------------------------------------------------
// Idle process - runs when nothing else is scheduled
// "Bandit stares at the ceiling." - Camping episode
// ---------------------------------------------------------------------------
static void idle_task(void) {
    while (1) {
        rtc_poll_if_pending();
        __asm__ volatile("hlt");
    }
}

static int kernel_bootstrap_first_user(void) {
    static const char *bootstrap_paths[] = { "/bin/init", "/bin/bash", NULL };

    for (size_t index = 0; bootstrap_paths[index]; index++) {
        process_t *process = elf_exec(bootstrap_paths[index], 0);
        if (!process) continue;

        scheduler_add(process);
        kprintf("[KERN] Bootstrap launching %s as pid=%u\n",
                bootstrap_paths[index], process->pid);
        process_enter_first_user(process);
    }

    return -1;
}

// ---------------------------------------------------------------------------
// kernel_main - called from boot/boot.asm after setting up the stack
// Arguments pushed by boot.asm: eax=multiboot magic, ebx=multiboot info ptr
// ---------------------------------------------------------------------------
void kernel_main(uint32_t magic, uint32_t *mboot_info) {
    boot_args_t boot_args;
    rootfs_config_t rootfs;
    uint32_t ram_mb = i386_multiboot_ram_mb(mboot_info);

    // Step 1: Screen up first so we can print messages
    vga_init();
    tty_init();

    // Validate multiboot magic
    if (magic != 0x2BADB002) {
        bluey_panic("Not booted by a Multiboot-compliant bootloader! (Bandit: 'What?!')");
    }

    bluey_boot_show_splash("I386", ram_mb);
    boot_args_init(&boot_args, mboot_info);
    rootfs_config_init(&rootfs);
    rootfs_apply_boot_args(&rootfs, &boot_args);

    // Step 1b: Syslog — initialise ring buffer before any other subsystem
    syslog_init();
    syslog_info("KERN", "BlueyOS kernel starting up");

    kprintf("  %s\n", BLUEYOS_VERSION_STRING);
    kprintf("  Codename : %s\n", BLUEYOS_CODENAME);
    kprintf("  Built by : %s@%s\n", BLUEYOS_BUILD_USER, BLUEYOS_BUILD_HOST);
    kprintf("  Date     : %s %s\n", BLUEYOS_BUILD_DATE, BLUEYOS_BUILD_TIME);
    kprintf("  %s\n\n", BLUEY_CHEEKY_MODE);
    if (boot_args.cmdline && boot_args.cmdline[0]) {
        kprintf("  Cmdline  : %s\n\n", boot_args.cmdline);
    }

    // Step 2: CPU tables (must be done before enabling interrupts)
    gdt_init();
    kprintf("%s\n", MSG_GDT_INIT);

    idt_init();   // also calls isr_init() and irq_init() internally
    kprintf("%s\n", MSG_IDT_INIT);
    kprintf("%s\n", MSG_ISR_INIT);
    kprintf("%s\n", MSG_IRQ_INIT);
    smp_init();

    // Step 3: Timer - enables IRQ0, enables interrupts
    timer_init(1000);   // 1000 Hz = 1ms resolution

    // Step 4: Driver framework + module loader
    driver_framework_init();
    module_framework_init();
    driver_modules_register();

    // Step 5: Keyboard - PS/2, IRQ1 (module)
    module_load("keyboard");

    // Step 6: Heap - uses memory just after kernel image
    kheap_init((uint32_t)&kernel_end, 0x100000);  // 1MB heap
    kprintf("%s\n", MSG_HEAP_INIT);

    // Step 7: Paging / virtual memory
    paging_init();
    signal_init();

    // Step 8: System information (hostname, timezone, epoch)
    sysinfo_set_ram_mb(ram_mb);
    sysinfo_init();
    rtc_init();

    // Step 9: Multi-user system (passwd + shadow)
    multiuser_init();

    // Step 10: VFS, FAT16, and BiscuitFS
    vfs_init();
    vfs_register_fs(fat_get_filesystem());
    vfs_register_fs(biscuitfs_get_filesystem());

    // Step 11: ATA disk driver (module)
    if (module_load("ata") == 0) {
        if (rootfs_mount_config(&rootfs) != 0) {
            kprintf("[VFS]  No recognised filesystem - running diskless\n");
        } else {
            rootfs_apply_fstab();
            // Flush early boot log once /var/log is reachable.
            syslog_flush_to_fs();
        }
    }

    /* Debug: if /bin/init cannot be opened after mounting root, dump
     * the mount table, list the root directory and /bin to help diagnose
     * missing init payloads created by the host mkfs tool. */
    {
        const int rootdbg_max_entries = 16;
        vfs_dirent_t *entries = (vfs_dirent_t *)kheap_alloc(sizeof(vfs_dirent_t) * rootdbg_max_entries, 0);

        kprintf("[ROOTDBG] Dumping VFS mount table:\n");
        vfs_print_mounts();

        kprintf("[ROOTDBG] Listing root ('/') entries:\n");
        if (!entries) {
            kprintf("[ROOTDBG] unable to allocate entry buffer\n");
        } else {
            int rn = vfs_readdir("/", entries, rootdbg_max_entries);
            if (rn < 0) {
            kprintf("[ROOTDBG] vfs_readdir('/') failed\n");
            } else if (rn == 0) {
            kprintf("[ROOTDBG] root is empty (0 entries)\n");
            } else {
                for (int i = 0; i < rn; i++) {
                    kprintf("  %s%s\n", entries[i].name, entries[i].is_dir ? "/" : "");
                }
            }
        }

        kprintf("[ROOTDBG] Listing /bin entries and checking /bin/init:\n");
        if (!entries) {
            kprintf("[ROOTDBG] unable to allocate entry buffer\n");
        } else {
            int bn = vfs_readdir("/bin", entries, rootdbg_max_entries);
            if (bn < 0) {
            kprintf("[ROOTDBG] vfs_readdir('/bin') failed\n");
            } else if (bn == 0) {
            kprintf("[ROOTDBG] /bin is empty (0 entries)\n");
            } else {
                for (int i = 0; i < bn; i++) {
                    kprintf("  %s%s\n", entries[i].name, entries[i].is_dir ? "/" : "");
                }
            }
        }

        int fd = vfs_open("/bin/init", VFS_O_RDONLY);
        if (fd < 0) {
            kprintf("[ROOTDBG] /bin/init not found (vfs_open failed)\n");
            vfs_stat_t st;
            if (vfs_stat("/bin/init", &st) == 0) {
                kprintf("[ROOTDBG] vfs_stat('/bin/init') reports size=%u mode=0%o is_dir=%u\n",
                        st.size, st.mode, st.is_dir);
            } else {
                kprintf("[ROOTDBG] vfs_stat('/bin/init') failed\n");
            }
        } else {
            kprintf("[ROOTDBG] /bin/init opened successfully (fd=%d)\n", fd);
            vfs_close(fd);
        }

        if (entries) kheap_free(entries);
    }

    // Step 12: Syscall interface (int 0x80)
    syscall_init();

    // Step 13: Process management + scheduler
    process_init();
    scheduler_init();

    // Step 13b: Device event channel (must be after process_init)
    devev_init();

    // Create idle process (runs when nothing else is ready)
    process_t *idle = process_create("bandit-idle", idle_task, 0, 0);
    if (idle) scheduler_add(idle);

    // Step 14: Network (Ethernet layer)
    net_init();
    loopback_init();
    module_load("ne2000");

    // Step 15: TCP/IP IPv4 stack
    tcpip_init();

    // Apply network interface configuration from /etc/interfaces.
    // Must be after tcpip_init() — tcpip_init() resets the config to compiled-in
    // defaults, so any config loaded here correctly overrides those defaults.
    netcfg_apply();

    // Step 16: ELF loader ready
    kprintf("%s\n", MSG_ELF_INIT);

    // All done!
    vga_set_color(VGA_LIGHT_GREEN, VGA_BLACK);
    kprintf("\n%s\n", MSG_DONE);
    vga_set_color(VGA_WHITE, VGA_BLACK);
    kprintf("Hostname : %s.%s\n", sysinfo_get_hostname(), sysinfo_get_domainname());
    kprintf("Timezone : %s (UTC+10, Brisbane - no DST because Queensland!)\n",
            sysinfo_get_timezone()->name);
    kprintf("Epoch    : %s\n", BANDIT_EPOCH_NAME);
    kprintf("\nBlueyOS is ready.\n");
    kprintf("\"This is the best day EVER!\" - Bluey Heeler\n\n");

    // Enable interrupts
    __asm__ volatile("sti");

    if (kernel_bootstrap_first_user() == 0) {
        for (;;) __asm__ volatile("hlt");
    }

    // Step 17: Shell - run interactively (never returns)
    shell_init();
    shell_run();

    // Should never reach here
    for (;;) __asm__ volatile("hlt");
}
