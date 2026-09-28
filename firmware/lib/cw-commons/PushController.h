#pragma once
// 浏览器推流：POST /push 收 64x64 RGB888，空闲超时后交还 clockface。
#include <Arduino.h>
#include <ESP32-HUB75-MatrixPanel-I2S-DMA.h>

struct PushController {
  static const uint16_t W = 64;
  static const uint16_t H = 64;
  static const uint32_t FRAME_LEN = (uint32_t)W * H * 3;
  static const uint32_t IDLE_MS = 5000;

  uint8_t *buf = nullptr;
  bool dirty = false;
  uint32_t lastFrameMs = 0;

  static PushController *getInstance() {
    static PushController p;
    return &p;
  }

  void begin() {
    if (!buf) {
      buf = (uint8_t *)malloc(FRAME_LEN);
      if (buf) memset(buf, 0, FRAME_LEN);
    }
  }

  bool active() {
    return buf != nullptr && (millis() - lastFrameMs) < IDLE_MS;
  }

  // 直接向内部缓冲写入（避免 HTTP 层再 malloc 一帧）
  uint8_t *lockBuf() {
    if (!buf) begin();
    return buf;
  }

  // n==FRAME_LEN 才算有效帧
  void finishFrame(size_t n) {
    if (buf && n >= FRAME_LEN) {
      dirty = true;
      lastFrameMs = millis();
    }
  }

  void draw(MatrixPanel_I2S_DMA *disp) {
    if (!buf || !disp) return;
    for (uint16_t y = 0; y < H; y++) {
      for (uint16_t x = 0; x < W; x++) {
        uint32_t i = ((uint32_t)y * W + x) * 3;
        disp->drawPixel(x, y, disp->color565(buf[i], buf[i + 1], buf[i + 2]));
      }
    }
    dirty = false;
  }
};
