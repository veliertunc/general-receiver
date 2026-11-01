#include "receiver.h"
#include <arpa/inet.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int config_load(const char *path, struct config *c) {
  *c = (struct config){.port = 8080,
                       .adapter = 0,
                       .timeout = 10,
                       .fm_low = 87.5,
                       .fm_high = 108.0};
  strcpy(c->listen, "0.0.0.0");
  strcpy(c->radio, "/dev/radio0");
  strcpy(c->channels, "/etc/receiver/channels.conf");
  strcpy(c->interface, "0.0.0.0");
  FILE *f = fopen(path, "r");
  if (!f)
    return -1;
  char line[1024], key[80], value[512];
  int bad = 0;
  while (fgets(line, sizeof line, f)) {
    if (line[0] == '#' || line[0] == '\n' || line[0] == '[')
      continue;
    if (sscanf(line, " %79[^= ] = %511s", key, value) != 2) {
      bad = 1;
      break;
    }
    char *end = NULL;
    if (!strcmp(key, "port") || !strcmp(key, "adapter") ||
        !strcmp(key, "timeout")) {
      unsigned long n = strtoul(value, &end, 10);
      if (!*value || *end || n > 65535) {
        bad = 1;
        break;
      }
      if (!strcmp(key, "port"))
        c->port = (unsigned)n;
      else if (!strcmp(key, "adapter"))
        c->adapter = (unsigned)n;
      else
        c->timeout = (unsigned)n;
    } else if (!strcmp(key, "fm_low") || !strcmp(key, "fm_high")) {
      double n = strtod(value, &end);
      if (*end || !isfinite(n)) {
        bad = 1;
        break;
      }
      if (!strcmp(key, "fm_low"))
        c->fm_low = n;
      else
        c->fm_high = n;
    } else {
      char *dest = NULL;
      size_t size = 0;
#define FIELD(k, field)                                                        \
  if (!strcmp(key, k)) {                                                       \
    dest = c->field;                                                           \
    size = sizeof c->field;                                                    \
  }
      FIELD("listen", listen)
      else FIELD("device", radio) else FIELD("channels", channels) else FIELD(
          "interface", interface)
#undef FIELD
          if (!dest || strlen(value) >= size) {
        bad = 1;
        break;
      }
      strcpy(dest, value);
    }
  }
  if (ferror(f))
    bad = 1;
  fclose(f);
  struct in_addr a;
  if (!c->port || !c->timeout || c->timeout > 3600 || c->adapter > 255 ||
      c->fm_low <= 0 || c->fm_low >= c->fm_high ||
      inet_pton(AF_INET, c->listen, &a) != 1 ||
      inet_pton(AF_INET, c->interface, &a) != 1 || c->radio[0] != '/' ||
      c->channels[0] != '/')
    bad = 1;
  return bad ? -1 : 0;
}
