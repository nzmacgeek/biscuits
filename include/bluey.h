#pragma once
// BlueyOS - Bluey-themed constants, messages, and ASCII art
// "This is the best day EVER!" - Bluey Heeler
//
// ⚠️  VIBE CODED RESEARCH PROJECT - NOT FOR PRODUCTION USE ⚠️
//
// Episode refs throughout: Hammerbarn, Baby Race, Camping, Magic Xylophone,
//                          Kids Car Wash, The Creek, Flat Pack, Army Kids

#include "types.h"
#include "version.h"

// ---------------------------------------------------------------------------
// Character name constants
// ---------------------------------------------------------------------------
#define BLUEY    "Bluey"
#define BINGO    "Bingo"
#define BANDIT   "Bandit"
#define CHILLI   "Chilli"
#define NANA     "Nana"
#define JACK     "Jack"
#define JUDO     "Judo"
#define CHLOE    "Chloe"
#define CALYPSO  "Calypso"

// ---------------------------------------------------------------------------
// Australian greetings and phrases
// ---------------------------------------------------------------------------
#define BLUEY_GREETING        "G'day mate! Welcome to BlueyOS!"
#define BLUEY_KERNEL_TITLE    "BlueyOS - Where Every Boot is a New Adventure!"
#define BLUEY_CHEEKY_MODE     "Cheeky Nuggies Mode: ENABLED"
#define BLUEY_READY           "She's ready to roll, mate!"
#define BLUEY_RIPPER          "Ripper! Everything's working!"

// ---------------------------------------------------------------------------
// ASCII art banner - fits in 80 columns
// ---------------------------------------------------------------------------
#define BLUEY_BANNER \
    "\n" \
    "  ____  _                    ___  ____  \n" \
    " | __ )| |_   _  ___ _   _ / _ \\/ ___| \n" \
    " |  _ \\| | | | |/ _ \\ | | | | | \\___ \\ \n" \
    " | |_) | | |_| |  __/ |_| | |_| |___) |\n" \
    " |____/|_|\\__,_|\\___|\\__, |\\___/|____/ \n" \
    "                     |___/              \n" \
    " Where Every Boot is a New Adventure!  \n" \
    " Codename: Bandit | v0.1.0             \n" \
    "\n"

// ---------------------------------------------------------------------------
// Component banners (printed during init)
// ---------------------------------------------------------------------------
#define MSG_GDT_INIT    "[GDT]  Bandit set up the Descriptor Table - like building a cubby house!"
#define MSG_IDT_INIT    "[IDT]  Chilli configured the Interrupts - she's got it sorted!"
#define MSG_ISR_INIT    "[ISR]  Exception handlers online - no crashing allowed at this playdate!"
#define MSG_IRQ_INIT    "[IRQ]  Hardware interrupts remapped - Nana would be proud!"
#define MSG_TIMER_INIT  "[TMR]  Bingo's Tick Tock Timer is ticking!"
#define MSG_KB_INIT     "[KBD]  Bingo's Keyboard ready - Tap tap tap!"
#define MSG_VGA_INIT    "[VGA]  Mum! The screen works!"
#define MSG_PAGE_INIT   "[PGE]  Paging enabled - Bandit mapped all the rooms!"
#define MSG_HEAP_INIT   "[HEP]  Kernel heap ready - plenty of room to play!"
#define MSG_SCHED_INIT  "[SCH]  Bandit's Homework Scheduler is running!"
#define MSG_PROC_INIT   "[PRC]  Process table initialised - everyone gets a turn!"
#define MSG_SYSCALL_INIT "[SYS] Bluey's Daddy Daughter Syscalls are ready!"
#define MSG_VFS_INIT    "[VFS]  Bingo's Backpack Filesystem mounted!"
#define MSG_ATA_INIT    "[ATA]  ATA disk driver online - let's find some data!"
#define MSG_USER_INIT   "[USR]  Multiuser system ready - who's playing today?"
#define MSG_NET_INIT    "[NET]  Jack's Network Snorkel: initialised!"
#define MSG_ELF_INIT    "[ELF]  Judo's ELF Loader: ready to flip some programs!"
#define MSG_DONE        "[OK]   G'day mate! Welcome to BlueyOS! She's all yours!"

// ---------------------------------------------------------------------------
// Kernel panic macro
// "Oh no! [Bandit voice]: KERNEL PANIC!"
// ---------------------------------------------------------------------------
// Forward declaration - kprintf is in lib/stdio.h, but we use a simple macro
// that calls it at panic time (defined after stdio is included in kernel.c)

// PANIC is defined in kernel/kernel.c after including stdio.h to avoid
// circular dependency. Individual subsystems call bluey_panic() instead.
void bluey_panic(const char *msg);

#define PANIC(msg) bluey_panic(msg)

// ---------------------------------------------------------------------------
// Heap magic number - "B10E" loosely reads as "BLUE"
// Episode ref: "Magic Xylophone" - magic numbers everywhere!
// ---------------------------------------------------------------------------
#define BLUEY_HEAP_MAGIC  0xB10EB10E

// ---------------------------------------------------------------------------
// Privilege rings
// ---------------------------------------------------------------------------
#define RING0  0   // Bandit's domain - the grown-ups only zone
#define RING3  3   // Bluey's domain - where the kids play
