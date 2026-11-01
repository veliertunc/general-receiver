#include "receiver.h"
#include <errno.h>
#include <fcntl.h>
#include <linux/dvb/dmx.h>
#include <linux/dvb/frontend.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

static pthread_mutex_t tuner = PTHREAD_MUTEX_INITIALIZER;
void television_close(struct tv_stream *s) {
  if (s->data >= 0)
    close(s->data);
  if (s->demux >= 0)
    close(s->demux);
  if (s->frontend >= 0)
    close(s->frontend);
  s->data = s->demux = s->frontend = -1;
  pthread_mutex_unlock(&tuner);
}
int television_open(const struct config *c, const char *channel,
                    struct tv_stream *s) {
  if (pthread_mutex_trylock(&tuner)) {
    errno = EBUSY;
    return -1;
  }
  *s = (struct tv_stream){-1, -1, -1};
  FILE *file = fopen(c->channels, "r");
  if (!file)
    goto fail;
  char line[512], name[128], system[24];
  unsigned frequency = 0, bandwidth = 0, symbols = 0;
  int found = 0;
  while (fgets(line, sizeof line, file)) {
    if (line[0] == '#')
      continue;
    if (sscanf(line, "%127s %23s %u %u %u", name, system, &frequency,
               &bandwidth, &symbols) == 5 &&
        !strcmp(name, channel)) {
      found = 1;
      break;
    }
  }
  fclose(file);
  if (!found) {
    errno = ENOENT;
    goto fail;
  }
  unsigned delivery;
  if (!strcmp(system, "DVB-T"))
    delivery = SYS_DVBT;
  else if (!strcmp(system, "DVB-T2"))
    delivery = SYS_DVBT2;
  else if (!strcmp(system, "DVB-C"))
    delivery = SYS_DVBC_ANNEX_A;
  else {
    errno = EINVAL;
    goto fail;
  }
  char path[128];
  snprintf(path, sizeof path, "/dev/dvb/adapter%u/frontend0", c->adapter);
  s->frontend = open(path, O_RDWR | O_CLOEXEC);
  if (s->frontend < 0)
    goto fail;
  struct dtv_property properties[] = {
      {.cmd = DTV_CLEAR},
      {.cmd = DTV_DELIVERY_SYSTEM, .u.data = delivery},
      {.cmd = DTV_FREQUENCY, .u.data = frequency},
      {.cmd = DTV_BANDWIDTH_HZ, .u.data = bandwidth},
      {.cmd = DTV_SYMBOL_RATE, .u.data = symbols},
      {.cmd = DTV_MODULATION, .u.data = QAM_AUTO},
      {.cmd = DTV_INVERSION, .u.data = INVERSION_AUTO},
      {.cmd = DTV_TUNE}};
  struct dtv_properties p = {.num = sizeof properties / sizeof properties[0],
                             .props = properties};
  if (ioctl(s->frontend, FE_SET_PROPERTY, &p) < 0)
    goto fail;
  fe_status_t status = 0;
  for (unsigned i = 0; i < c->timeout * 10; i++) {
    if (ioctl(s->frontend, FE_READ_STATUS, &status) < 0)
      goto fail;
    if (status & FE_HAS_LOCK)
      break;
    usleep(100000);
  }
  if (!(status & FE_HAS_LOCK)) {
    errno = ETIMEDOUT;
    goto fail;
  }
  snprintf(path, sizeof path, "/dev/dvb/adapter%u/demux0", c->adapter);
  s->demux = open(path, O_RDWR | O_CLOEXEC);
  if (s->demux < 0)
    goto fail;
  struct dmx_pes_filter_params filter = {.pid = 0x2000,
                                         .input = DMX_IN_FRONTEND,
                                         .output = DMX_OUT_TS_TAP,
                                         .pes_type = DMX_PES_OTHER,
                                         .flags = DMX_IMMEDIATE_START};
  if (ioctl(s->demux, DMX_SET_PES_FILTER, &filter) < 0)
    goto fail;
  snprintf(path, sizeof path, "/dev/dvb/adapter%u/dvr0", c->adapter);
  s->data = open(path, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
  if (s->data < 0)
    goto fail;
  return 0;
fail: {
  int saved = errno;
  television_close(s);
  errno = saved;
  return -1;
}
}
