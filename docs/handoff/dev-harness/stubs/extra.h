// SPDX-FileCopyrightText: 2026 Paweł Kwiatkowski
// SPDX-License-Identifier: GPL-3.0-only

#pragma once
#include <Arduino.h>
#include <cstdio>
#include <cstdarg>
#ifndef constrain
#define constrain(a, lo, hi) ((a) < (lo) ? (lo) : ((a) > (hi) ? (hi) : (a)))
#endif
#define HEX 16
#define HIGH 1
#define LOW 0
#define INPUT 0
#define OUTPUT 1
#define INPUT_PULLUP 2
#define INPUT_PULLDOWN 3
#define SERIAL_8N1 0
#define ADC_11db 3
typedef uint8_t byte;
inline void pinMode(int, int) {} inline void digitalWrite(int, int) {} inline int digitalRead(int) { return 0; }
inline void analogSetPinAttenuation(int, int) {} inline unsigned analogReadMilliVolts(int) { return 0; }
struct ExtraSerial { void begin(unsigned long) {} template <typename T> void print(const T&) {} template <typename T> void println(const T&) {} void println() {} void printf(const char*, ...) {} template <typename T> void print(T, int) {} explicit operator bool() const { return true; } };
struct EspClass { void restart() {} };
inline EspClass ESP;
struct HardwareSerial { explicit HardwareSerial(int) {} void setRxBufferSize(size_t) {} void begin(unsigned long, int, int, int) {} void end() {} void setTimeout(unsigned long) {} int available() { return 0; } int read() { return 0; } size_t write(const uint8_t*, size_t) { return 0; } void flush() {} };
#include <esp_system.h>
