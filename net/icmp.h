#pragma once
// BlueyOS ICMP - Internet Control Message Protocol (ping!)
// "Bluey sends a ping!" - Episode ref: "The Creek"
// Bluey and all related characters are trademarks of Ludo Studio Pty Ltd,
// licensed by BBC Studios. BlueyOS is an unofficial fan/research project.
#include "../include/types.h"
#include "tcpip.h"

void icmp_init(void);

// Called by ip_handle() when IP protocol = IPPROTO_ICMP
void icmp_handle(uint32_t src_ip, const uint8_t *data, uint16_t len);

// Send an ICMP echo request (ping) to dst_ip (network byte order).
// Returns 0 on send success, -1 on failure.
int icmp_ping(uint32_t dst_ip, uint16_t id, uint16_t seq);
