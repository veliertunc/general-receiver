#include "receiver.h"
#include <arpa/inet.h>
#include <errno.h>
#include <net/if.h>
#include <stdio.h>
#include <sys/socket.h>
#include <unistd.h>

int ethernet_open(const struct config *c, const char *group, unsigned port) {
  struct ip_mreq m = {0};
  if (!port || port > 65535 ||
      inet_pton(AF_INET, group, &m.imr_multiaddr) != 1 ||
      (ntohl(m.imr_multiaddr.s_addr) >> 24) != 239 ||
      inet_pton(AF_INET, c->interface, &m.imr_interface) != 1) {
    errno = EINVAL;
    return -1;
  }
  int fd = socket(AF_INET, SOCK_DGRAM | SOCK_CLOEXEC, 0);
  if (fd < 0)
    return -1;
  int yes = 1;
  struct sockaddr_in a = {.sin_family = AF_INET,
                          .sin_port = htons((unsigned short)port),
                          .sin_addr = {.s_addr = INADDR_ANY}};
  if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof yes) < 0 ||
      bind(fd, (void *)&a, sizeof a) < 0 ||
      setsockopt(fd, IPPROTO_IP, IP_ADD_MEMBERSHIP, &m, sizeof m) < 0) {
    int e = errno;
    close(fd);
    errno = e;
    return -1;
  }
  return fd;
}
int ethernet_status(char *b, size_t size) {
  struct if_nameindex *all = if_nameindex();
  if (!all)
    return -1;
  size_t used = 0;
  int n = snprintf(b, size, "{\"interfaces\":[");
  if (n < 0 || (size_t)n >= size) {
    if_freenameindex(all);
    return -1;
  }
  used = (size_t)n;
  for (struct if_nameindex *p = all; p->if_index; p++) {
    /* Interface names are encoded as indexes to avoid JSON escaping ambiguity.
     */
    n = snprintf(b + used, size - used, "%s%u", p == all ? "" : ",",
                 p->if_index);
    if (n < 0 || (size_t)n >= size - used) {
      if_freenameindex(all);
      return -1;
    }
    used += (size_t)n;
  }
  if_freenameindex(all);
  return snprintf(b + used, size - used, "]}") >= 0 ? 0 : -1;
}
