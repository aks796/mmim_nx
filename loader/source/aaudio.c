/* aaudio.c -- Android's AAudio, enough for Unity 6's FMOD, over audout
 *
 * Unity 6's FMOD opens its output through AAudio (dlopen libaaudio.so,
 * then the AAudioStreamBuilder_* calls) and does not fall back to OpenSL ES
 * when that fails ("FMOD failed to initialize the output device (60)": no
 * sound at all). So libaaudio.so is one of the "system" libraries here
 * (so_util.c) and these are its functions.
 *
 * FMOD asks for an output stream with a data callback: a thread of ours calls
 * it for one burst of frames at a time, converts what it mixed (float or
 * 16-bit, mono or stereo) to 16-bit stereo, and hands that to audio.c, whose
 * wait for a free output buffer paces the thread like a real device. FMOD
 * never asks the stream for its rate, format or channels back: it gets what
 * it asked for (48000 Hz, as Unity forces it to the device's rate).
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.  See the LICENSE file for details.
 */

#include <math.h>
#include <pthread.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <switch.h>

#include "audio.h"
#include "log.h"
#include "pthr.h"
#include "so_util.h"

#define AAUDIO_OK 0
#define AAUDIO_ERROR_ILLEGAL_ARGUMENT (-898)
#define AAUDIO_ERROR_INVALID_STATE (-895)
#define AAUDIO_ERROR_TIMEOUT (-885)

#define AAUDIO_FORMAT_PCM_I16 1
#define AAUDIO_FORMAT_PCM_FLOAT 2

enum {
  ST_OPEN = 2, ST_STARTING = 3, ST_STARTED = 4, ST_STOPPING = 9, ST_STOPPED = 10,
  ST_CLOSING = 11, ST_CLOSED = 12,
};

#define BURST 512 // frames per callback unless asked: 10.7 ms at 48 kHz

typedef int32_t (*DataCb)(void *stream, void *user, void *audio, int32_t frames);
typedef void (*ErrorCb)(void *stream, void *user, int32_t error);

typedef struct {
  int32_t rate, channels, format, frames_per_cb, capacity, direction;
  DataCb data_cb;
  void *data_user;
  ErrorCb error_cb;
  void *error_user;
} Builder;

typedef struct {
  Builder b;
  volatile int state;
  volatile int run;
  pthread_t thread;
  int has_thread;
  int32_t buffer_size;
} Stream;

static int32_t AAudio_createStreamBuilder(Builder **out) {
  Builder *b = calloc(1, sizeof(*b));
  b->rate = 48000;
  b->channels = 2;
  b->format = AAUDIO_FORMAT_PCM_FLOAT;
  *out = b;
  return AAUDIO_OK;
}

static int32_t AAudioStreamBuilder_delete(Builder *b) { free(b); return AAUDIO_OK; }
static void AAudioStreamBuilder_setSampleRate(Builder *b, int32_t v) { if (v > 0) b->rate = v; }
static void AAudioStreamBuilder_setChannelCount(Builder *b, int32_t v) { if (v > 0) b->channels = v; }
static void AAudioStreamBuilder_setFormat(Builder *b, int32_t v) { if (v > 0) b->format = v; }
static void AAudioStreamBuilder_setFramesPerDataCallback(Builder *b, int32_t v) { b->frames_per_cb = v; }
static void AAudioStreamBuilder_setBufferCapacityInFrames(Builder *b, int32_t v) { b->capacity = v; }
static void AAudioStreamBuilder_setDirection(Builder *b, int32_t v) { b->direction = v; }
static void AAudioStreamBuilder_setIgnored(Builder *b, int32_t v) { (void)b, (void)v; }
static void AAudioStreamBuilder_setDataCallback(Builder *b, DataCb cb, void *user) {
  b->data_cb = cb;
  b->data_user = user;
}
static void AAudioStreamBuilder_setErrorCallback(Builder *b, ErrorCb cb, void *user) {
  b->error_cb = cb;
  b->error_user = user;
}

static int32_t frames_per_cb(const Stream *s) {
  return s->b.frames_per_cb > 0 ? s->b.frames_per_cb : BURST;
}

static int32_t AAudioStreamBuilder_openStream(Builder *b, Stream **out) {
  if (b->direction != 0) { // input: no microphone
    log_printf("[aaudio] an input stream asked for: refused");
    return AAUDIO_ERROR_ILLEGAL_ARGUMENT;
  }
  if (b->format != AAUDIO_FORMAT_PCM_FLOAT && b->format != AAUDIO_FORMAT_PCM_I16) {
    log_printf("[aaudio] format %d asked for: refused", b->format);
    return AAUDIO_ERROR_ILLEGAL_ARGUMENT;
  }
  if (!nx_audio_init(b->rate, b->channels >= 2 ? 2 : 1))
    return AAUDIO_ERROR_INVALID_STATE;
  Stream *s = calloc(1, sizeof(*s));
  s->b = *b;
  s->state = ST_OPEN;
  s->buffer_size = frames_per_cb(s) * 2;
  log_printf("[aaudio] output stream: %d Hz, %d channel(s), %s, %d frames per callback%s", b->rate,
             b->channels, b->format == AAUDIO_FORMAT_PCM_FLOAT ? "float" : "16-bit", frames_per_cb(s),
             b->data_cb ? "" : " (no callback: writes)");
  *out = s;
  return AAUDIO_OK;
}

static void *stream_thread(void *arg) {
  Stream *s = arg;
  // ahead of the game's threads (priority 59): sound that waits stutters
  svcSetThreadPriority(CUR_THREAD_HANDLE, 0x2b);
  const int32_t n = frames_per_cb(s), ch = s->b.channels;
  const size_t esize = s->b.format == AAUDIO_FORMAT_PCM_FLOAT ? 4 : 2;
  void *buf = calloc((size_t)n * (size_t)ch, esize);
  int16_t *out = calloc((size_t)n * 2, sizeof(int16_t));
  s->state = ST_STARTED;
  while (s->run) {
    if (s->b.data_cb(s, s->b.data_user, buf, n) != 0) { // AAUDIO_CALLBACK_RESULT_STOP
      s->run = 0;
      break;
    }
    for (int32_t i = 0; i < n; i++) {
      for (int c = 0; c < 2; c++) {
        const int src = c < ch ? c : 0;
        int v;
        if (esize == 4) {
          float f = ((const float *)buf)[i * ch + src] * 32767.f;
          v = f > 32767.f ? 32767 : f < -32768.f ? -32768 : (int)lrintf(f);
        } else {
          v = ((const int16_t *)buf)[i * ch + src];
        }
        out[i * 2 + c] = (int16_t)v;
      }
    }
    nx_audio_write(out, n * 2);
  }
  free(buf);
  free(out);
  s->state = ST_STOPPED;
  return NULL;
}

static int32_t AAudioStream_requestStart(Stream *s) {
  if (!s)
    return AAUDIO_ERROR_ILLEGAL_ARGUMENT;
  if (s->run)
    return AAUDIO_OK;
  if (!s->b.data_cb) {
    s->state = ST_STARTED;
    return AAUDIO_OK;
  }
  if (s->has_thread) {
    pthread_join_soloader(s->thread, NULL);
    s->has_thread = 0;
  }
  s->run = 1;
  s->state = ST_STARTING;
  if (pthread_create_soloader(&s->thread, NULL, stream_thread, s) != 0) {
    s->run = 0;
    s->state = ST_STOPPED;
    return AAUDIO_ERROR_INVALID_STATE;
  }
  pthr_set_name(s->thread, "AAudio output");
  s->has_thread = 1;
  return AAUDIO_OK;
}

static int32_t AAudioStream_requestStop(Stream *s) {
  if (!s)
    return AAUDIO_ERROR_ILLEGAL_ARGUMENT;
  if (s->run) {
    s->state = ST_STOPPING;
    s->run = 0;
  } else {
    s->state = ST_STOPPED;
  }
  return AAUDIO_OK;
}

static int32_t AAudioStream_close(Stream *s) {
  if (!s)
    return AAUDIO_ERROR_ILLEGAL_ARGUMENT;
  s->run = 0;
  if (s->has_thread)
    pthread_join_soloader(s->thread, NULL);
  free(s);
  return AAUDIO_OK;
}

static int32_t AAudioStream_waitForStateChange(Stream *s, int32_t input, int32_t *next, int64_t timeout_ns) {
  if (!s)
    return AAUDIO_ERROR_ILLEGAL_ARGUMENT;
  const u64 t0 = armGetSystemTick();
  while (s->state == input && (int64_t)armTicksToNs(armGetSystemTick() - t0) < timeout_ns)
    svcSleepThread(1000000);
  if (next)
    *next = s->state;
  return s->state == input && timeout_ns > 0 ? AAUDIO_ERROR_TIMEOUT : AAUDIO_OK;
}

static int32_t AAudioStream_getFramesPerBurst(Stream *s) { return s ? frames_per_cb(s) : 0; }
static int32_t AAudioStream_getBufferSizeInFrames(Stream *s) { return s ? s->buffer_size : 0; }
static int32_t AAudioStream_setBufferSizeInFrames(Stream *s, int32_t v) {
  if (!s)
    return AAUDIO_ERROR_ILLEGAL_ARGUMENT;
  if (v > 0)
    s->buffer_size = v;
  return s->buffer_size;
}
static int32_t AAudioStream_getBufferCapacityInFrames(Stream *s) {
  return s ? (s->b.capacity > 0 ? s->b.capacity : frames_per_cb(s) * 8) : 0;
}
static int32_t AAudioStream_getDeviceId(Stream *s) { (void)s; return 0; }
static int32_t AAudioStream_getSessionId(Stream *s) { (void)s; return -1; }
static int32_t AAudioStream_getXRunCount(Stream *s) { (void)s; return 0; }
static uint8_t AAudioStream_isMMapUsed(Stream *s) { (void)s; return 0; }

DynLibFunction aaudio_functions[] = {
  { "AAudio_createStreamBuilder", (uintptr_t)&AAudio_createStreamBuilder },
  { "AAudioStreamBuilder_delete", (uintptr_t)&AAudioStreamBuilder_delete },
  { "AAudioStreamBuilder_openStream", (uintptr_t)&AAudioStreamBuilder_openStream },
  { "AAudioStreamBuilder_setBufferCapacityInFrames", (uintptr_t)&AAudioStreamBuilder_setBufferCapacityInFrames },
  { "AAudioStreamBuilder_setChannelCount", (uintptr_t)&AAudioStreamBuilder_setChannelCount },
  { "AAudioStreamBuilder_setDataCallback", (uintptr_t)&AAudioStreamBuilder_setDataCallback },
  { "AAudioStreamBuilder_setDeviceId", (uintptr_t)&AAudioStreamBuilder_setIgnored },
  { "AAudioStreamBuilder_setDirection", (uintptr_t)&AAudioStreamBuilder_setDirection },
  { "AAudioStreamBuilder_setErrorCallback", (uintptr_t)&AAudioStreamBuilder_setErrorCallback },
  { "AAudioStreamBuilder_setFormat", (uintptr_t)&AAudioStreamBuilder_setFormat },
  { "AAudioStreamBuilder_setFramesPerDataCallback", (uintptr_t)&AAudioStreamBuilder_setFramesPerDataCallback },
  { "AAudioStreamBuilder_setPerformanceMode", (uintptr_t)&AAudioStreamBuilder_setIgnored },
  { "AAudioStreamBuilder_setSampleRate", (uintptr_t)&AAudioStreamBuilder_setSampleRate },
  { "AAudioStreamBuilder_setSessionId", (uintptr_t)&AAudioStreamBuilder_setIgnored },
  { "AAudioStreamBuilder_setSharingMode", (uintptr_t)&AAudioStreamBuilder_setIgnored },
  { "AAudioStreamBuilder_setUsage", (uintptr_t)&AAudioStreamBuilder_setIgnored },
  { "AAudioStreamBuilder_setContentType", (uintptr_t)&AAudioStreamBuilder_setIgnored },
  { "AAudioStream_close", (uintptr_t)&AAudioStream_close },
  { "AAudioStream_getBufferCapacityInFrames", (uintptr_t)&AAudioStream_getBufferCapacityInFrames },
  { "AAudioStream_getBufferSizeInFrames", (uintptr_t)&AAudioStream_getBufferSizeInFrames },
  { "AAudioStream_getDeviceId", (uintptr_t)&AAudioStream_getDeviceId },
  { "AAudioStream_getFramesPerBurst", (uintptr_t)&AAudioStream_getFramesPerBurst },
  { "AAudioStream_getSessionId", (uintptr_t)&AAudioStream_getSessionId },
  { "AAudioStream_getXRunCount", (uintptr_t)&AAudioStream_getXRunCount },
  { "AAudioStream_isMMapUsed", (uintptr_t)&AAudioStream_isMMapUsed },
  { "AAudioStream_requestStart", (uintptr_t)&AAudioStream_requestStart },
  { "AAudioStream_requestStop", (uintptr_t)&AAudioStream_requestStop },
  { "AAudioStream_setBufferSizeInFrames", (uintptr_t)&AAudioStream_setBufferSizeInFrames },
  { "AAudioStream_waitForStateChange", (uintptr_t)&AAudioStream_waitForStateChange },
};

size_t aaudio_numfunctions = sizeof(aaudio_functions) / sizeof(*aaudio_functions);
