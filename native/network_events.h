// SPDX-License-Identifier: MIT
#ifndef TAILADB_NETWORK_EVENTS_H
#define TAILADB_NETWORK_EVENTS_H

#include <arpa/inet.h>
#include <linux/netlink.h>
#include <linux/rtnetlink.h>
#include <net/if.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

// Keep changes to the selected VPN and new VPN candidates. Physical-network
// route churn does not change an already bound Tailscale listener's identity.
// Without a live listener every event matters, so recovery stays prompt.
static bool tailadb_relevant_network_event(const void *data, int length, unsigned vpn_index) {
  if (!vpn_index)
    return true;
  for (const struct nlmsghdr *n = data;
       length >= (int)sizeof(*n) && n->nlmsg_len >= sizeof(*n) &&
       n->nlmsg_len <= (unsigned)length;
       n = NLMSG_NEXT(n, length)) {
    if (n->nlmsg_type == NLMSG_OVERRUN || n->nlmsg_type == NLMSG_ERROR)
      return true;
    if (n->nlmsg_type == RTM_NEWLINK || n->nlmsg_type == RTM_DELLINK) {
      if (NLMSG_PAYLOAD(n, 0) < sizeof(struct ifinfomsg))
        return true;
      const struct ifinfomsg *link = NLMSG_DATA(n);
      if ((unsigned)link->ifi_index == vpn_index || (link->ifi_flags & IFF_POINTOPOINT))
        return true;
    } else if (n->nlmsg_type == RTM_NEWADDR || n->nlmsg_type == RTM_DELADDR) {
      if (NLMSG_PAYLOAD(n, 0) < sizeof(struct ifaddrmsg))
        return true;
      const struct ifaddrmsg *address = NLMSG_DATA(n);
      if (address->ifa_family != AF_INET)
        continue;
      if (address->ifa_index == vpn_index)
        return true;
      int remaining = IFA_PAYLOAD(n);
      for (const struct rtattr *a = IFA_RTA(address); RTA_OK(a, remaining);
           a = RTA_NEXT(a, remaining)) {
        if ((a->rta_type == IFA_LOCAL || a->rta_type == IFA_ADDRESS) &&
            RTA_PAYLOAD(a) == sizeof(uint32_t) && address->ifa_prefixlen == 32) {
          uint32_t ip;
          memcpy(&ip, RTA_DATA(a), sizeof(ip));
          if ((ntohl(ip) & 0xffc00000) == 0x64400000)
            return true;
        }
      }
      if (remaining)
        return true;
    } else if (n->nlmsg_type == RTM_NEWROUTE || n->nlmsg_type == RTM_DELROUTE) {
      if (NLMSG_PAYLOAD(n, 0) < sizeof(struct rtmsg))
        return true;
      const struct rtmsg *route = NLMSG_DATA(n);
      if (route->rtm_family != AF_INET)
        continue;
      int remaining = RTM_PAYLOAD(n);
      for (const struct rtattr *a = RTM_RTA(route); RTA_OK(a, remaining);
           a = RTA_NEXT(a, remaining)) {
        if (a->rta_type == RTA_OIF && RTA_PAYLOAD(a) == sizeof(unsigned)) {
          unsigned index;
          memcpy(&index, RTA_DATA(a), sizeof(index));
          if (index == vpn_index)
            return true;
        }
      }
      if (remaining)
        return true;
    }
  }
  return length != 0; // Lost/truncated notifications require a full inspection.
}
#endif
