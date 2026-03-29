// BlueyOS System Information - hostname, domain, timezone, epoch
// "It's a big day!" - Bluey
// Bluey and all related characters are trademarks of Ludo Studio Pty Ltd,
// licensed by BBC Studios. BlueyOS is an unofficial fan/research project.
#pragma once
#include "../include/types.h"

// ----------------------------------------------------------------------------
// Bandit's Birthday Epoch
// Bandit Heeler's fictional birthday: October 15, 1980 00:00:00 AEST
// AEST = UTC+10, so in UTC that is: October 14, 1980 14:00:00
// Unix timestamp of that UTC moment:
//   Days from 1970-01-01 to 1980-10-14 UTC:
//     1970-1980 = 10 years, with leap years 1972,1976 in complete years = 3652 days
//     plus 1980: Jan31+Feb29+Mar31+Apr30+May31+Jun30+Jul31+Aug31+Sep30+14 = 288 days
//     total = 3652 + 288 = 3940 days; unix = 3940*86400 + 14*3600 = 340,405,200
// Episode ref: "Dad Baby" - the one about Bandit being born
// ----------------------------------------------------------------------------
#define BANDIT_BIRTHDAY_UNIX   340405200UL
#define BANDIT_EPOCH_NAME      "Bandit's Birthday (15 Oct 1980 AEST)"

// Brisbane / Queensland timezone: AEST UTC+10, no DST
// Queensland famously does not observe daylight saving time.
// "Why change the clocks? That's not how things work." - Bandit (probably)
#define BLUEYOS_TZ_NAME        "AEST"
#define BLUEYOS_TZ_OFFSET_SEC  36000    /* +10:00 in seconds */
#define BLUEYOS_TZ_DST         0        /* no DST in Queensland */

typedef struct {
    int32_t offset_seconds;
    uint8_t uses_dst;
    char    name[8];
} timezone_t;

void        sysinfo_init(void);
const char *sysinfo_get_hostname(void);
const char *sysinfo_get_domainname(void);
void        sysinfo_set_hostname(const char *name);
void        sysinfo_set_domainname(const char *name);
const timezone_t *sysinfo_get_timezone(void);

// Epoch conversion utilities
// bluey_secs = seconds since Bandit's Birthday (15 Oct 1980 00:00 AEST)
// unix_secs  = seconds since Unix epoch (1 Jan 1970 00:00 UTC)
uint32_t bluey_to_unix(uint32_t bluey_secs);
uint32_t unix_to_bluey(uint32_t unix_secs);
void     unix_to_datetime(uint32_t unix_secs, int *year, int *mon, int *mday,
                          int *hour, int *min, int *sec);
void     bluey_to_datetime(uint32_t bluey_secs, int *year, int *mon, int *mday,
                           int *hour, int *min, int *sec);
