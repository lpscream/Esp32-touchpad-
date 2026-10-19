// Заглушка Wire с эмулятором maXTouch-чипа за адресом 0x4A (только для тестов).
#pragma once
#include <Arduino.h>

#include <deque>
#include <vector>

// Эмулятор: карта памяти + очередь сообщений T5.
struct FakeMxt {
  static constexpr uint16_t T5_ADDR = 0x0200;
  uint8_t mem[0x400] = {0};
  std::deque<std::vector<uint8_t>> queue;  // сообщения (T5_SIZE-1 байт)
  uint16_t ptr = 0;
  bool present = true;
  uint8_t addr = 0x4A;
  uint8_t t5Size = 9;  // как в таблице: размер T5 вместе с байтом CRC

  // Таблица: T44, T5, T6, T7, T9. Report ID: T6=1, T9=2..11.
  void setup(uint16_t xrange, uint16_t yrange) {
    memset(mem, 0, sizeof(mem));
    uint8_t info[7] = {0xA2, 0x00, 0x10, 0xAA, 14, 8, 5};
    memcpy(mem, info, 7);
    auto entry = [&](int i, uint8_t type, uint16_t addr, uint8_t size, uint8_t inst, uint8_t rep) {
      uint8_t* e = &mem[7 + i * 6];
      e[0] = type; e[1] = addr & 0xFF; e[2] = addr >> 8; e[3] = size - 1; e[4] = inst - 1; e[5] = rep;
    };
    entry(0, 44, 0x0180, 1, 1, 0);
    entry(1, 5, T5_ADDR, t5Size, 1, 0);
    entry(2, 6, 0x0100, 6, 1, 1);
    entry(3, 7, 0x0110, 3, 1, 0);
    entry(4, 9, 0x0120, 35, 1, 10);
    mem[0x0110] = 32; mem[0x0111] = 10; mem[0x0112] = 50;   // T7
    mem[0x0120] = 0x83;                                      // T9 CTRL
    mem[0x0120 + 18] = xrange & 0xFF; mem[0x0120 + 19] = xrange >> 8;
    mem[0x0120 + 20] = yrange & 0xFF; mem[0x0120 + 21] = yrange >> 8;
    queue.clear();
  }

  // Сообщение T9 (12-битная раскладка)
  void touch(uint8_t idx, uint8_t status, uint16_t x, uint16_t y) {
    std::vector<uint8_t> m(t5Size - 1, 0);
    m[0] = 2 + idx;
    m[1] = status;
    m[2] = x >> 4;
    m[3] = y >> 4;
    m[4] = ((x & 0xF) << 4) | (y & 0xF);
    queue.push_back(m);
  }
  void t6(uint8_t status) {
    std::vector<uint8_t> m(t5Size - 1, 0);
    m[0] = 1;
    m[1] = status;
    queue.push_back(m);
  }
};
extern FakeMxt g_mxt;

class TwoWire {
 public:
  void begin(int, int) {}
  void setClock(uint32_t) {}
  void setTimeOut(uint16_t) {}
  void beginTransmission(uint8_t a) { tx_ = a; n_ = 0; }
  size_t write(uint8_t b) {
    if (n_ < 2) hdr_[n_] = b;
    else if (g_mxt.present) g_mxt.mem[(g_mxt.ptr + n_ - 2) & 0x3FF] = b;  // запись данных
    if (n_ == 1) g_mxt.ptr = hdr_[0] | (hdr_[1] << 8);
    n_++;
    return 1;
  }
  uint8_t endTransmission() { return (tx_ == g_mxt.addr && g_mxt.present) ? 0 : 2; }
  uint8_t requestFrom(uint8_t a, uint8_t len) {
    if (a != g_mxt.addr || !g_mxt.present) return 0;
    rx_.clear();
    if (g_mxt.ptr == FakeMxt::T5_ADDR) {  // чтение T5 вынимает сообщение из очереди
      std::vector<uint8_t> m(len, 0xFF);
      if (!g_mxt.queue.empty()) {
        m = g_mxt.queue.front();
        g_mxt.queue.pop_front();
        m.resize(len, 0);
      }
      rx_ = m;
    } else {
      for (uint8_t i = 0; i < len; i++) rx_.push_back(g_mxt.mem[(g_mxt.ptr + i) & 0x3FF]);
    }
    pos_ = 0;
    return len;
  }
  int read() { return pos_ < rx_.size() ? rx_[pos_++] : -1; }

 private:
  uint8_t tx_ = 0, hdr_[2] = {0, 0};
  size_t n_ = 0, pos_ = 0;
  std::vector<uint8_t> rx_;
};
extern TwoWire Wire;
