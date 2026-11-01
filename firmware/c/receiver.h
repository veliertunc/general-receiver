#ifndef RECEIVER_H
#define RECEIVER_H
#include <stddef.h>
struct config {
  unsigned port, adapter, timeout;
  char listen[64], radio[256], channels[256], interface[64];
  double fm_low, fm_high;
};
struct tv_stream {
  int frontend, demux, data;
};
int config_load(const char *path, struct config *cfg);
int radio_tune(const struct config *cfg, double mhz);
int television_open(const struct config *cfg, const char *channel,
                    struct tv_stream *stream);
void television_close(struct tv_stream *stream);
int ethernet_open(const struct config *cfg, const char *group, unsigned port);
int ethernet_status(char *buffer, size_t size);
#endif
