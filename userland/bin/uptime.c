/*
 * SPDX-License-Identifier: BSD-2-Clause
 * Copyright (c) 2026, ASD Project Contributors
 *
 * uptime — show how long the system has been running.
 *
 * Uses SYS_TIME (PIT ticks × 10 ms) to compute uptime.
 * Output format:  up X days, HH:MM:SS
 */
#include "common.h"

int main(int argc, const char **argv) {
    (void)argc; (void)argv;

    /* SYS_TIME returns pit_ticks * 10_000_000 ns */
    uint64_t ticks_ns  = asd_time();
    uint64_t secs      = ticks_ns / 1000000000ULL;
    uint64_t mins      = secs / 60;  secs  %= 60;
    uint64_t hours     = mins / 60;  mins  %= 60;
    uint64_t days      = hours / 24; hours %= 24;

    printf(" up ");
    if (days == 1)
        printf("1 day, ");
    else if (days > 1)
        printf("%lld days, ", (long long)days);

    printf("%02lld:%02lld:%02lld\n",
           (long long)hours, (long long)mins, (long long)secs);
    return 0;
}
