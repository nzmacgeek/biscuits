// BlueyOS System Information - hostname, domain, timezone, epoch
// "It's a big day!" - Bluey Heeler
// Episode ref: "The Sign" - Bandit knows where home is
// Bluey and all related characters are trademarks of Ludo Studio Pty Ltd,
// licensed by BBC Studios. BlueyOS is an unofficial fan/research project.
#include "../include/types.h"
#include "../lib/string.h"
#include "../lib/stdio.h"
#include "sysinfo.h"

static char hostname[64]   = "blueyos";
static char domainname[64] = "local";
static uint32_t total_ram_mb = 0;

static timezone_t brissy_tz = {
    .offset_seconds = BLUEYOS_TZ_OFFSET_SEC,
    .uses_dst       = BLUEYOS_TZ_DST,
    .name           = BLUEYOS_TZ_NAME
};

void sysinfo_init(void) {
    kprintf("[SYS]  Hostname: %s.%s  Timezone: %s (UTC+10, no DST - Queensland style!)\n",
            hostname, domainname, brissy_tz.name);
    kprintf("[SYS]  Bandit's Birthday Epoch: %s\n", BANDIT_EPOCH_NAME);
}

const char *sysinfo_get_hostname(void)   { return hostname; }
const char *sysinfo_get_domainname(void) { return domainname; }
uint32_t sysinfo_get_total_ram_mb(void)  { return total_ram_mb; }

void sysinfo_set_hostname(const char *name) {
    strncpy(hostname, name, sizeof(hostname) - 1);
    hostname[sizeof(hostname) - 1] = '\0';
}

void sysinfo_set_domainname(const char *name) {
    strncpy(domainname, name, sizeof(domainname) - 1);
    domainname[sizeof(domainname) - 1] = '\0';
}

const timezone_t *sysinfo_get_timezone(void) { return &brissy_tz; }

void sysinfo_set_total_ram_mb(uint32_t ram_mb) {
    total_ram_mb = ram_mb;
}

// ---------------------------------------------------------------------------
// Epoch conversion
// BANDIT_BIRTHDAY_UNIX = Unix timestamp of Bandit's Birthday epoch start
// ---------------------------------------------------------------------------

uint32_t bluey_to_unix(uint32_t bluey_secs) {
    return bluey_secs + BANDIT_BIRTHDAY_UNIX;
}

uint32_t unix_to_bluey(uint32_t unix_secs) {
    if (unix_secs < BANDIT_BIRTHDAY_UNIX) return 0;
    return unix_secs - BANDIT_BIRTHDAY_UNIX;
}

// ---------------------------------------------------------------------------
// unix_to_datetime: convert Unix seconds (UTC) to a date+time struct
// Handles leap years correctly.
// ---------------------------------------------------------------------------
static int is_leap(int y) {
    return (y % 4 == 0 && y % 100 != 0) || (y % 400 == 0);
}

static const int days_in_month[12] = {
    31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31
};

void unix_to_datetime(uint32_t unix_secs, int *year, int *mon, int *mday,
                      int *hour, int *min, int *sec) {
    // Apply Brisbane UTC+10 offset
    unix_secs += (uint32_t)BLUEYOS_TZ_OFFSET_SEC;

    *sec  = (int)(unix_secs % 60); unix_secs /= 60;
    *min  = (int)(unix_secs % 60); unix_secs /= 60;
    *hour = (int)(unix_secs % 24); unix_secs /= 24;

    // Days since 1970-01-01
    uint32_t days = unix_secs;
    int y = 1970;
    while (1) {
        uint32_t days_in_year = is_leap(y) ? 366u : 365u;
        if (days < days_in_year) break;
        days -= days_in_year;
        y++;
    }
    *year = y;

    int m = 1;
    while (m <= 12) {
        int dim = days_in_month[m-1] + ((m == 2 && is_leap(y)) ? 1 : 0);
        if ((int)days < dim) break;
        days -= (uint32_t)dim;
        m++;
    }
    *mon  = m;
    *mday = (int)days + 1;
}

void bluey_to_datetime(uint32_t bluey_secs, int *year, int *mon, int *mday,
                       int *hour, int *min, int *sec) {
    unix_to_datetime(bluey_to_unix(bluey_secs), year, mon, mday, hour, min, sec);
}
