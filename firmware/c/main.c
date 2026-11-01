#include "receiver.h"
#include <arpa/inet.h>
#include <errno.h>
#include <math.h>
#include <poll.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

static struct config cfg;
static volatile sig_atomic_t stopping;
static pthread_mutex_t clients_lock = PTHREAD_MUTEX_INITIALIZER;
static unsigned clients;
static void stop(int sig) {
  (void)sig;
  stopping = 1;
}
static int send_all(int fd, const void *data, size_t size) {
  const char *p = data;
  while (size) {
    ssize_t n = send(fd, p, size, MSG_NOSIGNAL);
    if (n < 0 && errno == EINTR)
      continue;
    if (n <= 0)
      return -1;
    p += n;
    size -= (size_t)n;
  }
  return 0;
}
static void reply(int fd, int code, const char *body) {
  char h[256];
  int n = snprintf(
      h, sizeof h,
      "HTTP/1.1 %d %s\r\nContent-Type: application/json\r\nContent-Length: "
      "%zu\r\nConnection: close\r\n\r\n",
      code,
      code == 200   ? "OK"
      : code == 409 ? "Conflict"
      : code == 404 ? "Not Found"
      : code == 400 ? "Bad Request"
                    : "Service Unavailable",
      strlen(body));
  if (send_all(fd, h, (size_t)n) == 0)
    send_all(fd, body, strlen(body));
}
static int query(const char *url, const char *key, char *out, size_t size) {
  const char *p = strchr(url, '?');
  if (!p)
    return -1;
  p++;
  while (*p) {
    const char *end = strchr(p, '&');
    if (!end)
      end = p + strlen(p);
    size_t k = strlen(key);
    if ((size_t)(end - p) > k && !strncmp(p, key, k) && p[k] == '=') {
      p += k + 1;
      size_t n = 0;
      while (p < end) {
        unsigned char ch = (unsigned char)*p++;
        if (ch == '%') {
          if (end - p < 2)
            return -1;
          char hex[3] = {p[0], p[1], 0};
          char *tail;
          long v = strtol(hex, &tail, 16);
          if (*tail)
            return -1;
          ch = (unsigned char)v;
          p += 2;
        } else if (ch == '+')
          ch = ' ';
        if (ch < 32 || ch == 127 || n + 1 >= size)
          return -1;
        out[n++] = (char)ch;
      }
      out[n] = 0;
      return n ? 0 : -1;
    }
    p = *end ? end + 1 : end;
  }
  return -1;
}
static void stream(int client, int input) {
  const char *h =
      "HTTP/1.1 200 OK\r\nContent-Type: video/mp2t\r\nCache-Control: "
      "no-store\r\nConnection: close\r\n\r\n";
  if (send_all(client, h, strlen(h)))
    return;
  char buffer[65536];
  while (!stopping) {
    struct pollfd p[2] = {{.fd = input, .events = POLLIN},
                          {.fd = client, .events = POLLIN}};
    int ready = poll(p, 2, (int)cfg.timeout * 1000);
    if (ready < 0 && errno == EINTR)
      continue;
    if (ready <= 0 || p[1].revents ||
        p[0].revents & (POLLERR | POLLHUP | POLLNVAL))
      break;
    if (p[0].revents & POLLIN) {
      ssize_t n = read(input, buffer, sizeof buffer);
      if (n < 0 && (errno == EAGAIN || errno == EINTR))
        continue;
      if (n <= 0 || send_all(client, buffer, (size_t)n))
        break;
    }
  }
}
static void *serve_client(void *arg) {
  int fd = *(int *)arg;
  free(arg);
  struct timeval timeout = {.tv_sec = 10};
  setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof timeout);
  setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof timeout);
  char request[8192] = {0};
  size_t used = 0;
  char *headers = NULL;
  while (used < sizeof request - 1) {
    ssize_t n = recv(fd, request + used, sizeof request - 1 - used, 0);
    if (n <= 0)
      goto done;
    used += (size_t)n;
    request[used] = 0;
    if ((headers = strstr(request, "\r\n\r\n")))
      break;
  }
  if (!headers) {
    reply(fd, 400, "{\"error\":\"Request headers too large\"}");
    goto done;
  }
  char method[8], url[1024];
  if (sscanf(request, "%7s %1023s", method, url) != 2)
    goto done;
  char path[1024];
  snprintf(path, sizeof path, "%s", url);
  char *q = strchr(path, '?');
  if (q)
    *q = 0;
  if (!strcmp(method, "GET") &&
      (!strcmp(path, "/status") || !strcmp(path, "/tv/status") ||
       !strcmp(path, "/radio/status"))) {
    char frontend[128], b[512];
    snprintf(frontend, sizeof frontend, "/dev/dvb/adapter%u/frontend0",
             cfg.adapter);
    snprintf(b, sizeof b,
             "{\"television\":{\"available\":%s},\"radio\":{\"available\":%s},"
             "\"ethernet\":{\"multicast\":true}}",
             access(frontend, F_OK) ? "false" : "true",
             access(cfg.radio, F_OK) ? "false" : "true");
    reply(fd, 200, b);
  } else if (!strcmp(method, "GET") && !strcmp(path, "/ethernet/status")) {
    char b[2048];
    if (ethernet_status(b, sizeof b))
      reply(fd, 503, "{\"error\":\"Interface enumeration failed\"}");
    else
      reply(fd, 200, b);
  } else if (!strcmp(method, "GET") && !strcmp(path, "/tv.ts")) {
    char name[128];
    struct tv_stream tv;
    if (query(url, "channel", name, sizeof name)) {
      reply(fd, 400, "{\"error\":\"Missing channel\"}");
      goto done;
    }
    if (television_open(&cfg, name, &tv)) {
      reply(
          fd, errno == EBUSY ? 409 : 503,
          "{\"error\":\"Tuner busy, channel missing, or signal unavailable\"}");
      goto done;
    }
    stream(fd, tv.data);
    television_close(&tv);
  } else if (!strcmp(method, "GET") && !strcmp(path, "/udp.ts")) {
    char group[64], port[16] = "1234", *end;
    query(url, "port", port, sizeof port);
    unsigned long number = strtoul(port, &end, 10);
    if (query(url, "group", group, sizeof group) || *end || number > 65535) {
      reply(fd, 400, "{\"error\":\"Invalid multicast address or port\"}");
      goto done;
    }
    int input = ethernet_open(&cfg, group, (unsigned)number);
    if (input < 0) {
      reply(fd, 503, "{\"error\":\"Cannot join multicast group\"}");
      goto done;
    }
    stream(fd, input);
    close(input);
  } else if (!strcmp(method, "POST") && !strcmp(path, "/radio/tune")) {
    char *length = strstr(request, "\r\nContent-Length:");
    if (!length)
      length = strstr(request, "\r\ncontent-length:");
    unsigned count = 0;
    if (!length || sscanf(strchr(length + 2, ':') + 1, "%u", &count) != 1 ||
        !count || count > 1024) {
      reply(fd, 400, "{\"error\":\"Content-Length required (1-1024)\"}");
      goto done;
    }
    size_t offset = (size_t)(headers + 4 - request);
    if (offset + count >= sizeof request) {
      reply(fd, 400, "{\"error\":\"Request too large\"}");
      goto done;
    }
    while (used < offset + count) {
      ssize_t n = recv(fd, request + used, offset + count - used, 0);
      if (n <= 0)
        goto done;
      used += (size_t)n;
    }
    request[offset + count] = 0;
    double mhz = NAN;
    int consumed = 0;
    sscanf(headers + 4, " { \"frequency_mhz\" : %lf } %n", &mhz, &consumed);
    if (consumed != (int)count || !isfinite(mhz) || mhz < cfg.fm_low ||
        mhz > cfg.fm_high) {
      reply(fd, 400, "{\"error\":\"Invalid FM frequency\"}");
      goto done;
    }
    if (radio_tune(&cfg, mhz))
      reply(fd, 503, "{\"error\":\"FM hardware unavailable\"}");
    else
      reply(fd, 200, "{\"tuned\":true,\"audio\":\"analog line output\"}");
  } else
    reply(fd, 404, "{\"error\":\"Unknown endpoint\"}");
done:
  close(fd);
  pthread_mutex_lock(&clients_lock);
  clients--;
  pthread_mutex_unlock(&clients_lock);
  return NULL;
}
int main(int argc, char **argv) {
  const char *file = "/etc/receiver/receiver.conf";
  int check = 0;
  for (int i = 1; i < argc; i++) {
    if (!strcmp(argv[i], "--config") && i + 1 < argc)
      file = argv[++i];
    else if (!strcmp(argv[i], "--check-config"))
      check = 1;
    else {
      fprintf(stderr, "Usage: %s [--config FILE] [--check-config]\n", argv[0]);
      return 2;
    }
  }
  if (config_load(file, &cfg)) {
    fprintf(stderr, "Invalid or unreadable configuration: %s\n", file);
    return 2;
  }
  if (check) {
    puts("Configuration valid");
    return 0;
  }
  signal(SIGINT, stop);
  signal(SIGTERM, stop);
  signal(SIGPIPE, SIG_IGN);
  int server = socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0);
  if (server < 0)
    return 1;
  int yes = 1;
  setsockopt(server, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof yes);
  struct sockaddr_in addr = {.sin_family = AF_INET,
                             .sin_port = htons((unsigned short)cfg.port)};
  inet_pton(AF_INET, cfg.listen, &addr.sin_addr);
  if (bind(server, (void *)&addr, sizeof addr) || listen(server, 16)) {
    perror("HTTP listen");
    close(server);
    return 1;
  }
  while (!stopping) {
    struct pollfd p = {.fd = server, .events = POLLIN};
    if (poll(&p, 1, 500) <= 0)
      continue;
    int client = accept(server, NULL, NULL);
    if (client < 0)
      continue;
    pthread_mutex_lock(&clients_lock);
    if (clients >= 16) {
      pthread_mutex_unlock(&clients_lock);
      close(client);
      continue;
    }
    clients++;
    pthread_mutex_unlock(&clients_lock);
    int *arg = malloc(sizeof *arg);
    pthread_t t;
    if (arg)
      *arg = client;
    if (!arg || pthread_create(&t, NULL, serve_client, arg)) {
      free(arg);
      close(client);
      pthread_mutex_lock(&clients_lock);
      clients--;
      pthread_mutex_unlock(&clients_lock);
    } else
      pthread_detach(t);
  }
  close(server);
  return 0;
}
