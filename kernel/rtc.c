#include "rtc.h"

#if defined(BLUEYOS_ARCH_I386)
#include "../include/ports.h"
#elif defined(BLUEYOS_ARCH_M68K)
#include "../arch/m68k/mac_lc3.h"
#endif

#define RTC_SYNC_INTERVAL_MS      1000u
#define RTC_RETRY_INTERVAL_MS      250u
#define MAC_TO_UNIX_EPOCH_DIFF 2082844800UL

static volatile uint32_t rtc_monotonic_ms = 0;
static uint32_t rtc_base_unix = 0;
static uint32_t rtc_base_ms = 0;
static uint32_t rtc_last_poll_ms = 0;
static bool rtc_synced = false;
static volatile bool rtc_poll_pending = false;

static uint32_t rtc_estimated_unix(void) {
    if (!rtc_synced) {
        return 0;
    }

    return rtc_base_unix + ((rtc_monotonic_ms - rtc_base_ms) / 1000u);
}

static void rtc_accept_hardware_time(uint32_t unix_secs) {
    rtc_base_unix = unix_secs;
    rtc_base_ms = rtc_monotonic_ms;
    rtc_synced = true;
}

#if defined(BLUEYOS_ARCH_I386)
static int rtc_is_leap_year(int year) {
    return (year % 4 == 0 && year % 100 != 0) || (year % 400 == 0);
}

static uint32_t rtc_datetime_to_unix(int year, int mon, int mday,
                                     int hour, int min, int sec) {
    static const int days_before_month[12] = {
        0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334
    };

    uint32_t days = 0;
    for (int y = 1970; y < year; y++) {
        days += (uint32_t)(rtc_is_leap_year(y) ? 366 : 365);
    }

    days += (uint32_t)days_before_month[mon - 1];
    if (mon > 2 && rtc_is_leap_year(year)) {
        days++;
    }
    days += (uint32_t)(mday - 1);

    return (((days * 24u) + (uint32_t)hour) * 60u + (uint32_t)min) * 60u + (uint32_t)sec;
}

typedef struct {
    uint8_t sec;
    uint8_t min;
    uint8_t hour;
    uint8_t mday;
    uint8_t mon;
    uint8_t year;
    uint8_t status_b;
} rtc_cmos_sample_t;

static uint8_t rtc_cmos_read(uint8_t reg) {
    outb(0x70, (uint8_t)(reg | 0x80));
    io_wait();
    return inb(0x71);
}

static uint8_t rtc_bcd_to_bin(uint8_t value) {
    return (uint8_t)(((value >> 4) * 10u) + (value & 0x0Fu));
}

static int rtc_cmos_update_in_progress(void) {
    return (rtc_cmos_read(0x0A) & 0x80) != 0;
}

static int rtc_cmos_read_sample(rtc_cmos_sample_t *sample) {
    if (rtc_cmos_update_in_progress()) {
        return 0;
    }

    sample->sec = rtc_cmos_read(0x00);
    sample->min = rtc_cmos_read(0x02);
    sample->hour = rtc_cmos_read(0x04);
    sample->mday = rtc_cmos_read(0x07);
    sample->mon = rtc_cmos_read(0x08);
    sample->year = rtc_cmos_read(0x09);
    sample->status_b = rtc_cmos_read(0x0B);
    return 1;
}

static int rtc_cmos_samples_match(const rtc_cmos_sample_t *lhs,
                                  const rtc_cmos_sample_t *rhs) {
    return lhs->sec == rhs->sec &&
           lhs->min == rhs->min &&
           lhs->hour == rhs->hour &&
           lhs->mday == rhs->mday &&
           lhs->mon == rhs->mon &&
           lhs->year == rhs->year &&
           lhs->status_b == rhs->status_b;
}

static bool rtc_arch_read_unix_seconds(uint32_t *unix_secs) {
    rtc_cmos_sample_t first;
    rtc_cmos_sample_t second;

    for (int attempt = 0; attempt < 8; attempt++) {
        if (!rtc_cmos_read_sample(&first)) {
            continue;
        }
        if (!rtc_cmos_read_sample(&second)) {
            continue;
        }
        if (!rtc_cmos_samples_match(&first, &second)) {
            continue;
        }

        int sec = first.sec;
        int min = first.min;
        int hour = first.hour;
        int mday = first.mday;
        int mon = first.mon;
        int year = first.year;

        if ((first.status_b & 0x04) == 0) {
            sec = rtc_bcd_to_bin((uint8_t)sec);
            min = rtc_bcd_to_bin((uint8_t)min);
            hour = rtc_bcd_to_bin((uint8_t)(hour & 0x7Fu)) | (hour & 0x80);
            mday = rtc_bcd_to_bin((uint8_t)mday);
            mon = rtc_bcd_to_bin((uint8_t)mon);
            year = rtc_bcd_to_bin((uint8_t)year);
        }

        if ((first.status_b & 0x02) == 0 && (hour & 0x80) != 0) {
            hour = ((hour & 0x7F) + 12) % 24;
        }

        year += (year < 70) ? 2000 : 1900;
        *unix_secs = rtc_datetime_to_unix(year, mon, mday, hour, min, sec);
        return true;
    }

    return false;
}

static const char *rtc_arch_source_name(void) {
    return "CMOS RTC (polled)";
}

static bool rtc_arch_poll_supported(void) {
    return true;
}

#elif defined(BLUEYOS_ARCH_M68K)

#define MAC_LMG_TIME 0x020CUL

__attribute__((noinline))
static uint32_t rtc_m68k_read_u32(uintptr_t addr) {
    volatile uint32_t *ptr = (volatile uint32_t *)addr;
    return *ptr;
}

static bool rtc_arch_read_unix_seconds(uint32_t *unix_secs) {
    uint32_t mac_secs = rtc_m68k_read_u32((uintptr_t)MAC_LMG_TIME);
    if (mac_secs < MAC_TO_UNIX_EPOCH_DIFF) {
        return false;
    }

    *unix_secs = mac_secs - MAC_TO_UNIX_EPOCH_DIFF;
    return true;
}

static const char *rtc_arch_source_name(void) {
    return "Mac ROM RTC seed";
}

static bool rtc_arch_poll_supported(void) {
    return false;
}

#else

static bool rtc_arch_read_unix_seconds(uint32_t *unix_secs) {
    (void)unix_secs;
    return false;
}

static const char *rtc_arch_source_name(void) {
    return "No hardware RTC backend";
}

static bool rtc_arch_poll_supported(void) {
    return false;
}

#endif

void rtc_init(void) {
    rtc_last_poll_ms = rtc_monotonic_ms;
    rtc_synced = false;

    rtc_poll();
}

void rtc_notify_tick(void) {
    rtc_monotonic_ms++;

    if (rtc_arch_poll_supported()) {
        uint32_t interval = rtc_synced ? RTC_SYNC_INTERVAL_MS : RTC_RETRY_INTERVAL_MS;
        if ((rtc_monotonic_ms - rtc_last_poll_ms) >= interval) {
            /* Set a flag so rtc_poll() can be called from non-interrupt context
             * (e.g., the kernel idle loop) to avoid hardware I/O in IRQ context. */
            rtc_poll_pending = true;
        }
    }
}

void rtc_poll(void) {
    uint32_t unix_secs = 0;

    rtc_poll_pending = false;
    rtc_last_poll_ms = rtc_monotonic_ms;
    if (!rtc_arch_read_unix_seconds(&unix_secs)) {
        return;
    }

    rtc_accept_hardware_time(unix_secs);
}

void rtc_poll_if_pending(void) {
    if (rtc_poll_pending) {
        rtc_poll();
    }
}

bool rtc_get_unix_time(uint32_t *unix_secs) {
    if (!rtc_synced) {
        return false;
    }

    if (unix_secs) {
        *unix_secs = rtc_estimated_unix();
    }
    return true;
}

uint32_t rtc_get_uptime_seconds(void) {
    return rtc_monotonic_ms / 1000u;
}

const char *rtc_source_name(void) {
    return rtc_arch_source_name();
}