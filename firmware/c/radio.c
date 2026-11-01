#include "receiver.h"
#include <errno.h>
#include <fcntl.h>
#include <linux/videodev2.h>
#include <math.h>
#include <pthread.h>
#include <stdint.h>
#include <sys/ioctl.h>
#include <unistd.h>

static pthread_mutex_t radio_lock = PTHREAD_MUTEX_INITIALIZER;
static int radio_fd = -1;

int radio_tune(const struct config *c, double mhz) {
  if (!isfinite(mhz) || mhz < c->fm_low || mhz > c->fm_high) {
    errno = EINVAL;
    return -1;
  }
  pthread_mutex_lock(&radio_lock);
  if (radio_fd < 0)
    radio_fd = open(c->radio, O_RDWR | O_CLOEXEC);
  int fd = radio_fd;
  if (fd < 0) {
    pthread_mutex_unlock(&radio_lock);
    return -1;
  }
  struct v4l2_tuner tuner = {.index = 0};
  int result = ioctl(fd, VIDIOC_G_TUNER, &tuner);
  if (!result) {
    double scale = (tuner.capability & V4L2_TUNER_CAP_1HZ)   ? 1000000.0
                   : (tuner.capability & V4L2_TUNER_CAP_LOW) ? 16000.0
                                                             : 16.0;
    struct v4l2_frequency f = {.tuner = 0,
                               .type = V4L2_TUNER_RADIO,
                               .frequency = (uint32_t)llround(mhz * scale)};
    result = ioctl(fd, VIDIOC_S_FREQUENCY, &f);
    if (!result) {
      struct v4l2_control mute = {.id = V4L2_CID_AUDIO_MUTE, .value = 0};
      result = ioctl(fd, VIDIOC_S_CTRL, &mute);
    }
  }
  /* Keep the radio open: Si470x powers down when its last handle closes. */
  int saved = errno;
  pthread_mutex_unlock(&radio_lock);
  errno = saved;
  return result;
}
