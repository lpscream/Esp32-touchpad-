// Тесты движка жестов (без Arduino). Сборка и запуск: tests/run.sh
#include <stdio.h>
#include <stdlib.h>

#include <vector>

#include "../mxt112s_ble_touchpad/gestures.h"

using namespace tp;

static int g_fail = 0;
#define CHECK(cond, ...)                                  \
  do {                                                    \
    if (!(cond)) {                                        \
      g_fail++;                                           \
      printf("FAIL %s:%d: %s  ", __FILE__, __LINE__, #cond); \
      printf(__VA_ARGS__);                                \
      printf("\n");                                       \
    }                                                     \
  } while (0)

struct Sim {
  Gestures g;
  uint32_t t = 1000;
  int sumX = 0, sumY = 0, sumW = 0, sumP = 0;
  int pressesL = 0, pressesR = 0, pressesM = 0;
  uint8_t buttons = 0;
  bool leftHeldNow = false;
  std::vector<MouseReport> log;

  explicit Sim(const GestureConfig& c = GestureConfig()) : g(c) { g.setRange(4095, 4095); }

  void drain() {
    MouseReport r;
    while (g.poll(t, r)) {
      sumX += r.x; sumY += r.y; sumW += r.wheel; sumP += r.pan;
      if ((r.buttons & BTN_LEFT) && !(buttons & BTN_LEFT)) pressesL++;
      if ((r.buttons & BTN_RIGHT) && !(buttons & BTN_RIGHT)) pressesR++;
      if ((r.buttons & BTN_MIDDLE) && !(buttons & BTN_MIDDLE)) pressesM++;
      buttons = r.buttons;
      log.push_back(r);
    }
  }
  // один кадр: до 2 пальцев
  void frame(int n, int x0, int y0, int x1 = 0, int y1 = 0, uint32_t dtMs = 10) {
    t += dtMs;
    Finger f[MAX_FINGERS] = {};
    if (n >= 1) f[0] = {true, (int16_t)x0, (int16_t)y0};
    if (n >= 2) f[1] = {true, (int16_t)x1, (int16_t)y1};
    g.update(t, f, MAX_FINGERS);
    drain();
  }
  void lift(uint32_t dtMs = 10) { frame(0, 0, 0, 0, 0, dtMs); }
  void wait(uint32_t ms) {
    for (uint32_t i = 0; i < ms; i += 5) {
      t += 5;
      drain();
    }
  }
};

static void test_tap_left() {
  Sim s;
  for (int i = 0; i < 8; i++) s.frame(1, 2000, 2000);
  s.lift();
  s.wait(60);
  CHECK(s.pressesL == 1, "left presses=%d", s.pressesL);
  CHECK(s.buttons == 0, "buttons stuck: %d", s.buttons);
  CHECK(s.sumX == 0 && s.sumY == 0, "tap moved cursor");
}

static void test_no_tap_when_slow() {
  Sim s;
  for (int i = 0; i < 40; i++) s.frame(1, 2000, 2000);  // 400 мс
  s.lift();
  s.wait(60);
  CHECK(s.pressesL == 0, "long touch must not click");
}

static void test_no_tap_when_moved() {
  Sim s;
  for (int i = 0; i < 10; i++) s.frame(1, 1000 + i * 40, 2000);  // 360 counts
  s.lift();
  s.wait(60);
  CHECK(s.pressesL == 0, "swipe must not click");
}

static void test_pointer_move() {
  Sim s;
  for (int i = 0; i <= 10; i++) s.frame(1, 1000 + i * 40, 2000);  // +400 counts по X
  s.wait(10);
  float basePx = 400 * 1800.0f / 4095;
  CHECK(s.sumX >= (int)basePx - 2 && s.sumX <= (int)(basePx * 3), "sumX=%d base=%.1f", s.sumX, basePx);
  CHECK(abs(s.sumY) <= 1, "sumY=%d", s.sumY);

  Sim d;  // вниз по Y -> положительный Y (вниз по экрану)
  for (int i = 0; i <= 10; i++) d.frame(1, 2000, 1000 + i * 40);
  CHECK(d.sumY > 100 && abs(d.sumX) <= 1, "sumY=%d sumX=%d", d.sumY, d.sumX);
}

static void test_accel_is_faster() {
  Sim slow, fast;
  for (int i = 0; i <= 20; i++) slow.frame(1, 1000 + i * 10, 2000, 0, 0, 20);  // 200 counts / 400 ms
  for (int i = 0; i <= 4; i++) fast.frame(1, 1000 + i * 50, 2000, 0, 0, 10);   // 200 counts / 40 ms
  CHECK(fast.sumX > slow.sumX, "fast=%d slow=%d", fast.sumX, slow.sumX);
}

static void test_big_delta_is_split() {
  Sim s;
  s.frame(1, 0, 2000);
  s.frame(1, 4000, 2000, 0, 0, 5);  // огромный скачок: > 127 px
  for (const auto& r : s.log) CHECK(r.x >= -127 && r.x <= 127, "x out of range");
  CHECK(s.sumX > 127, "delta lost: %d", s.sumX);
}

static void test_right_click() {
  Sim s;
  for (int i = 0; i < 8; i++) s.frame(2, 1500, 2000, 2000, 2000);
  s.lift();
  s.wait(60);
  CHECK(s.pressesR == 1 && s.pressesL == 0, "R=%d L=%d", s.pressesR, s.pressesL);
  CHECK(s.buttons == 0, "buttons stuck");
}

static void test_middle_click() {
  Sim s;
  for (int i = 0; i < 8; i++) {
    s.t += 10;
    Finger f[MAX_FINGERS] = {{true, 1000, 2000}, {true, 2000, 2000}, {true, 3000, 2000}};
    s.g.update(s.t, f, MAX_FINGERS);
    s.drain();
  }
  s.lift();
  s.wait(60);
  CHECK(s.pressesM == 1, "M=%d", s.pressesM);
}

static void test_scroll() {
  Sim s;
  for (int i = 0; i <= 12; i++) s.frame(2, 1500, 3000 - i * 50, 2000, 3000 - i * 50);  // пальцы вверх на 600
  s.lift();
  s.wait(60);
  CHECK(s.sumW >= 3 && s.sumW <= 5, "wheel=%d (ожидаем ~4)", s.sumW);
  CHECK(s.sumX == 0 && s.sumY == 0, "scroll moved cursor");
  CHECK(s.pressesL + s.pressesR == 0, "scroll produced click");
  CHECK(s.sumP == 0, "pan=%d", s.sumP);

  GestureConfig nat;
  nat.naturalScroll = true;
  Sim n(nat);
  for (int i = 0; i <= 12; i++) n.frame(2, 1500, 3000 - i * 50, 2000, 3000 - i * 50);
  CHECK(n.sumW <= -3, "natural wheel=%d", n.sumW);
}

static void test_horizontal_scroll() {
  Sim s;
  for (int i = 0; i <= 12; i++) s.frame(2, 1500 + i * 50, 2000, 2000 + i * 50, 2000);  // вправо
  CHECK(s.sumP >= 3, "pan=%d", s.sumP);
  CHECK(s.sumW == 0, "wheel=%d", s.sumW);
}

static void test_drag() {
  Sim s;
  for (int i = 0; i < 6; i++) s.frame(1, 2000, 2000);
  s.lift();
  s.wait(40);
  CHECK(s.pressesL == 1, "tap click");
  // сразу касаемся снова и ведём
  for (int i = 0; i <= 10; i++) s.frame(1, 1000 + i * 40, 2000);
  CHECK(s.pressesL == 2 && (s.buttons & BTN_LEFT), "drag must hold left (presses=%d buttons=%d)", s.pressesL, s.buttons);
  CHECK(s.sumX > 100, "drag must move");
  s.lift();
  s.wait(40);
  CHECK(s.buttons == 0, "left must be released after drag");
}

static void test_drag_window_expires() {
  Sim s;
  for (int i = 0; i < 6; i++) s.frame(1, 2000, 2000);
  s.lift();
  s.wait(500);
  for (int i = 0; i <= 4; i++) s.frame(1, 1000 + i * 40, 2000);
  CHECK(s.pressesL == 1 && s.buttons == 0, "late touch must not drag (presses=%d)", s.pressesL);
}

static void test_lift_one_of_two_no_jump() {
  Sim s;
  for (int i = 0; i <= 6; i++) s.frame(2, 1500, 3000 - i * 30, 2500, 3000 - i * 30);
  int x0 = s.sumX, y0 = s.sumY;
  for (int i = 0; i <= 6; i++) s.frame(1, 2500 + i * 20, 2800);  // остался один палец
  CHECK(s.sumX == x0 && s.sumY == y0, "pointer moved after lifting one finger");
}

static void test_tap_disabled() {
  GestureConfig c;
  c.tapToClick = false;
  Sim s(c);
  for (int i = 0; i < 8; i++) s.frame(1, 2000, 2000);
  s.lift();
  s.wait(60);
  CHECK(s.pressesL == 0, "tapToClick=false must not click");
}

static void test_jitter_ignored() {
  Sim s;
  for (int i = 0; i < 30; i++) s.frame(1, 2000 + (i % 2), 2000 + ((i / 2) % 2));  // дрожание 1 count
  CHECK(s.sumX == 0 && s.sumY == 0, "jitter moved cursor: %d,%d", s.sumX, s.sumY);
}

int main() {
  test_tap_left();
  test_no_tap_when_slow();
  test_no_tap_when_moved();
  test_pointer_move();
  test_accel_is_faster();
  test_big_delta_is_split();
  test_right_click();
  test_middle_click();
  test_scroll();
  test_horizontal_scroll();
  test_drag();
  test_drag_window_expires();
  test_lift_one_of_two_no_jump();
  test_tap_disabled();
  test_jitter_ignored();
  printf(g_fail ? "gestures: %d FAILED\n" : "gestures: all passed\n", g_fail);
  return g_fail ? 1 : 0;
}
