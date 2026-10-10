// Минимальная заглушка Arduino API для запуска кода на ПК (только для тестов).
#pragma once
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define HIGH 1
#define LOW 0
#define INPUT 0
#define OUTPUT 1
#define INPUT_PULLUP 2

extern uint32_t g_millis;
extern int g_pin[64];
inline uint32_t millis() { return g_millis; }
inline void delay(uint32_t ms) { g_millis += ms; }
inline void pinMode(int, int) {}
inline void digitalWrite(int p, int v) { g_pin[p] = v; }
int digitalRead(int p);  // реализуется в тесте (CHG берётся из эмулятора чипа)

struct Print {
  virtual size_t write(const char* s, size_t n) {
    fwrite(s, 1, n, stdout);
    return n;
  }
  int printf(const char* fmt, ...) __attribute__((format(printf, 2, 3))) {
    char b[512];
    va_list a;
    va_start(a, fmt);
    int n = vsnprintf(b, sizeof(b), fmt, a);
    va_end(a);
    write(b, n);
    return n;
  }
  void println(const char* s) { printf("%s\n", s); }
  void println() { printf("\n"); }
  void print(const char* s) { printf("%s", s); }
};

struct HardwareSerial : Print {
  void begin(unsigned long) {}
};
extern HardwareSerial Serial;
