// SPDX-FileCopyrightText: 2026 Paweł Kwiatkowski
// SPDX-License-Identifier: GPL-3.0-only

// Stub U8g2: logs draw ops for render.py to rasterize. Font metrics are approximate.
#pragma once
#include <Arduino.h>
#include <cstdint>
#include <cstdio>
#include <cstring>
typedef int u8g2_uint_t;
extern const uint8_t u8g2_font_helvR08_tf[], u8g2_font_helvB08_tf[], u8g2_font_profont15_tf[], u8g2_font_helvB10_tr[];
#define U8G2_R0 nullptr
extern double g_char_w[4][128];
inline int fontIndex(const uint8_t* f) {
  return f == u8g2_font_helvR08_tf ? 0 : f == u8g2_font_helvB08_tf ? 1 : f == u8g2_font_profont15_tf ? 2 : 3;
}
class U8G2_ST7565_ERC12864_ALT_F_4W_SW_SPI {
 public:
  U8G2_ST7565_ERC12864_ALT_F_4W_SW_SPI(const void*, uint8_t, uint8_t, uint8_t, uint8_t, uint8_t) {}
  void begin() {}
  void setContrast(uint8_t) {}
  void setPowerSave(uint8_t) {}
  void sendF(const char*, ...) {}
  void setFontMode(uint8_t) {}
  void setDrawColor(uint8_t c) { color = c; std::printf("color %d\n", c); }
  void setFont(const uint8_t* f) { font = fontIndex(f); }
  int getAscent() { static const int a[4] = {8, 8, 10, 10}; return a[font]; }
  int getStrWidth(const char* s) {
    double w = 0;
    for (; *s; ++s) w += g_char_w[font][(unsigned char)*s & 127];
    return (int)(w + 0.5);
  }
  int drawStr(int x, int y, const char* s) { std::printf("str %d %d %d %s\n", x, y, font, s); return getStrWidth(s); }
  void drawHLine(int x, int y, int w) { std::printf("box %d %d %d 1\n", x, y, w); }
  void drawBox(int x, int y, int w, int h) { std::printf("box %d %d %d %d\n", x, y, w, h); }
  void drawRBox(int x, int y, int w, int h, int) { std::printf("rbox %d %d %d %d\n", x, y, w, h); }
  void drawFrame(int x, int y, int w, int h) { std::printf("frame %d %d %d %d\n", x, y, w, h); }
  void drawPixel(int x, int y) { std::printf("box %d %d 1 1\n", x, y); }
  void drawTriangle(int a, int b, int c, int d, int e, int f) { std::printf("tri %d %d %d %d %d %d\n", a, b, c, d, e, f); }
  void drawXBMP(int x, int y, int w, int h, const uint8_t* bits) {
    std::printf("xbm %d %d %d %d", x, y, w, h);
    for (int i = 0; i < h * ((w + 7) / 8); ++i) std::printf(" %d", bits[i]);
    std::printf("\n");
  }
  void clearBuffer() { std::printf("clear\n"); }
  void sendBuffer() { std::printf("send\n"); }
 private:
  int font = 0, color = 1;
};
