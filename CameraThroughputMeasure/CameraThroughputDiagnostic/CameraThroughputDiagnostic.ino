/*
 * 0: normal streaming, using CameraCaptureRawBytes' request-byte protocol.
 * 1: capture only; prints frame counts and elapsed time every 5 seconds.
 * 2: capture one frame, stop DCMI, and repeatedly transmit that same frame.
 * Use modes 0 and 2 with CameraRawBytesVisualizer or camera_throughput.py.
 * Reset between modes. Do not open a serial monitor during binary transfers.
 */
#include "camera.h"
#include <zephyr/device.h>
#include <zephyr/drivers/video.h>

#ifndef CAMERA_BENCH_MODE
#define CAMERA_BENCH_MODE 0
#endif

#if CAMERA_BENCH_MODE < 0 || CAMERA_BENCH_MODE > 2
#error "CAMERA_BENCH_MODE must be 0, 1 or 2"
#endif

Camera cam;
FrameBuffer frozen;

void fail(const char *message) {
  Serial.println(message);
  while (true) {
    delay(1000);
  }
}

void setup() {
  Serial.begin(115200);
  while (!Serial) {
    delay(10);
  }
  if (!cam.begin(320, 240, CAMERA_RGB565)) {
    fail("Camera begin failed");
  }
  cam.setVerticalFlip(false);
  cam.setHorizontalMirror(false);

#if CAMERA_BENCH_MODE == 2
  if (!cam.grabFrame(frozen)) {
    fail("Initial capture failed");
  }
  int ret = video_stream_stop(DEVICE_DT_GET(DT_CHOSEN(zephyr_camera)),
                             VIDEO_BUF_TYPE_OUTPUT);
  if (ret != 0) {
    fail("Camera stop failed");
  }
  // Keep the dequeued buffer owned by this application until reset.
#endif
}

void loop() {
#if CAMERA_BENCH_MODE == 2
  if (Serial.read() == 1) {
    Serial.write(frozen.getBuffer(), frozen.getBufferSize());
  } else {
    delay(1);
  }
#else
  FrameBuffer fb;
#if CAMERA_BENCH_MODE == 1
  static uint32_t start = millis();
  static uint32_t frames = 0;
  static uint32_t failures = 0;
  if (cam.grabFrame(fb)) {
    frames++;
    if (!cam.releaseFrame(fb)) {
      fail("Frame release failed");
    }
  } else {
    failures++;
  }
  uint32_t elapsed = millis() - start;
  if (elapsed >= 5000) {
    Serial.print("frames=");
    Serial.print(frames);
    Serial.print(" elapsed_ms=");
    Serial.print(elapsed);
    Serial.print(" grab_failures=");
    Serial.println(failures);
    frames = failures = 0;
    start = millis();
  }
#else
  if (cam.grabFrame(fb)) {
    if (Serial.read() == 1) {
      Serial.write(fb.getBuffer(), fb.getBufferSize());
    }
    if (!cam.releaseFrame(fb)) {
      fail("Frame release failed");
    }
  }
#endif
#endif
}
