#pragma once
// 空闲表盘：移植自 rpi-led-webpush（HH:MM + MM-DD + 中文星期）
// 5×7 ASCII + 8×8 汉字，直接画在 64×64 上，不依赖外部字体资源。

#include <Arduino.h>
#include <Adafruit_GFX.h>
#include "IClockface.h"
#include "CWDateTime.h"

#define CLOCKFACE_NAME "idle-clock"

class IdleClock : public IClockface
{
private:
  Adafruit_GFX *_display;
  CWDateTime *_dateTime = nullptr;

  void drawChar(int16_t x, int16_t y, char ch, uint8_t scale, uint16_t color)
  {
    const uint8_t *cols = nullptr;
    // 手写 5×7，与 rpi-led-webpush/src/clock.rs 一致
    switch (ch)
    {
    case '0': { static const uint8_t g[5] = {0x3E, 0x51, 0x49, 0x45, 0x3E}; cols = g; break; }
    case '1': { static const uint8_t g[5] = {0x00, 0x42, 0x7F, 0x40, 0x00}; cols = g; break; }
    case '2': { static const uint8_t g[5] = {0x42, 0x61, 0x51, 0x49, 0x46}; cols = g; break; }
    case '3': { static const uint8_t g[5] = {0x21, 0x41, 0x45, 0x4B, 0x31}; cols = g; break; }
    case '4': { static const uint8_t g[5] = {0x18, 0x14, 0x12, 0x7F, 0x10}; cols = g; break; }
    case '5': { static const uint8_t g[5] = {0x27, 0x45, 0x45, 0x45, 0x39}; cols = g; break; }
    case '6': { static const uint8_t g[5] = {0x3C, 0x4A, 0x49, 0x49, 0x30}; cols = g; break; }
    case '7': { static const uint8_t g[5] = {0x01, 0x71, 0x09, 0x05, 0x03}; cols = g; break; }
    case '8': { static const uint8_t g[5] = {0x36, 0x49, 0x49, 0x49, 0x36}; cols = g; break; }
    case '9': { static const uint8_t g[5] = {0x06, 0x49, 0x49, 0x29, 0x1E}; cols = g; break; }
    case ':': { static const uint8_t g[5] = {0x00, 0x36, 0x36, 0x00, 0x00}; cols = g; break; }
    case '-': { static const uint8_t g[5] = {0x08, 0x08, 0x08, 0x08, 0x08}; cols = g; break; }
    case ' ': { static const uint8_t g[5] = {0x00, 0x00, 0x00, 0x00, 0x00}; cols = g; break; }
    default: return;
    }
    for (uint8_t cx = 0; cx < 5; cx++)
    {
      uint8_t bits = cols[cx];
      for (uint8_t cy = 0; cy < 7; cy++)
      {
        if (!(bits & (1 << cy)))
          continue;
        for (uint8_t sy = 0; sy < scale; sy++)
          for (uint8_t sx = 0; sx < scale; sx++)
            _display->drawPixel(x + cx * scale + sx, y + cy * scale + sy, color);
      }
    }
  }

  void drawText(int16_t x, int16_t y, const char *s, uint8_t scale, uint16_t color)
  {
    while (*s)
    {
      drawChar(x, y, *s++, scale, color);
      x += 6 * scale;
    }
  }

  int16_t textWidth(const char *s, uint8_t scale) { return strlen(s) * 6 * scale; }

  // 8×8 汉字星期，bit7 最左 —— 与 rpi-led-webpush/clock.rs 相同
  void drawWeekday(int16_t x, int16_t y, int weekday, uint8_t scale, uint16_t color)
  {
    static const uint8_t glyphs[7][8] = {
        // 日
        {0b01111110, 0b01000010, 0b01000010, 0b01111110, 0b01000010, 0b01000010, 0b01111110, 0b00000000},
        // 一
        {0b00000000, 0b00000000, 0b00000000, 0b11111111, 0b00000000, 0b00000000, 0b00000000, 0b00000000},
        // 二
        {0b00111100, 0b00000000, 0b00000000, 0b00000000, 0b00000000, 0b01111110, 0b00000000, 0b00000000},
        // 三
        {0b00111100, 0b00000000, 0b01111110, 0b00000000, 0b00111100, 0b00000000, 0b00000000, 0b00000000},
        // 四
        {0b01111110, 0b01000010, 0b01011010, 0b01011010, 0b01011010, 0b01011010, 0b01111110, 0b00000000},
        // 五
        {0b01111110, 0b00001000, 0b00001000, 0b00111110, 0b00001000, 0b00001000, 0b01111110, 0b00000000},
        // 六
        {0b00010000, 0b00010000, 0b01111110, 0b00000000, 0b00100100, 0b00100100, 0b01000010, 0b00000000},
    };
    weekday = weekday % 7;
    const uint8_t *rows = glyphs[weekday];
    for (uint8_t cy = 0; cy < 8; cy++)
    {
      for (uint8_t cx = 0; cx < 8; cx++)
      {
        if (!(rows[cy] & (0x80 >> cx)))
          continue;
        for (uint8_t sy = 0; sy < scale; sy++)
          for (uint8_t sx = 0; sx < scale; sx++)
            _display->drawPixel(x + cx * scale + sx, y + cy * scale + sy, color);
      }
    }
  }

public:
  IdleClock(Adafruit_GFX *display) { _display = display; }

  void setup(CWDateTime *dateTime) override
  {
    _dateTime = dateTime;
    _display->clearScreen();
  }

  void update() override
  {
    if (!_dateTime)
      return;

    _display->fillScreen(0x0000);

    char timeBuf[6];
    snprintf(timeBuf, sizeof(timeBuf), "%02d:%02d", _dateTime->getHour(), _dateTime->getMinute());
    char dateBuf[6];
    snprintf(dateBuf, sizeof(dateBuf), "%02d-%02d", _dateTime->getMonth(), _dateTime->getDay());

    // 64×64：时间 4×（约 28px 高），日期 2× + 星期 2×
    const uint8_t timeScale = 4;
    const uint8_t dateScale = 2;
    const uint8_t wdScale = 2;
    const uint16_t timeColor = _display->color565(200, 200, 200);
    const uint16_t dimColor = _display->color565(90, 130, 180);

    int16_t tw = textWidth(timeBuf, timeScale);
    int16_t tx = (64 - tw) / 2;
    int16_t ty = 8;
    drawText(tx, ty, timeBuf, timeScale, timeColor);

    // 日期 + 星期（中文单字）
    int16_t dw = textWidth(dateBuf, dateScale) + 4 + 8 * wdScale;
    int16_t dx = (64 - dw) / 2;
    int16_t dy = 48;
    drawText(dx, dy, dateBuf, dateScale, dimColor);
    drawWeekday(dx + textWidth(dateBuf, dateScale) + 4, dy, _dateTime->getWeekday(), wdScale, dimColor);
  }
};
