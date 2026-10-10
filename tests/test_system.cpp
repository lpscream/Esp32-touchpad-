// Сквозной тест: скетч + драйвер + эмулятор mXT112S + заглушка BLE. Запуск: tests/run.sh
#include <Arduino.h>
#include <Wire.h>

#include "../mxt112s_ble_touchpad/mxt112s.cpp"
#include "../mxt112s_ble_touchpad/mxt112s_ble_touchpad.ino"

uint32_t g_millis = 0;
int g_pin[64];
HardwareSerial Serial;
FakeMxt g_mxt;
TwoWire Wire;
std::vector<std::vector<uint8_t>> g_ble_reports;
int digitalRead(int p) { return p == PIN_TP_CHG ? (g_mxt.queue.empty() ? HIGH : LOW) : g_pin[p]; }

static int g_fail = 0;
#define CHECK(cond, ...)                                       \
  do {                                                         \
    if (!(cond)) {                                             \
      g_fail++;                                                \
      printf("FAIL %s:%d: %s  ", __FILE__, __LINE__, #cond);   \
      printf(__VA_ARGS__);                                     \
      printf("\n");                                            \
    }                                                          \
  } while (0)

static void runLoops(int n, int stepMs = 10) {
  for (int i = 0; i < n; i++) {
    loop();
    g_millis += stepMs - 1;  // loop() уже сделал delay(1)
  }
}

struct Sum { int x = 0, y = 0, w = 0, p = 0; int leftPress = 0; uint8_t b = 0; };
static Sum summarize() {
  Sum s;
  for (auto& r : g_ble_reports) {
    s.b = 0;
    s.x += (int8_t)r[1]; s.y += (int8_t)r[2]; s.w += (int8_t)r[3]; s.p += (int8_t)r[4];
    if ((r[0] & 1) && !(s.b & 1)) s.leftPress++;
    s.b = r[0];
  }
  return s;
}

static void test_decode() {
  uint8_t m[9] = {2, 0xC0, 0, 0, 0};
  uint16_t x = 1234, y = 567;
  m[2] = x >> 4; m[3] = y >> 4; m[4] = ((x & 15) << 4) | (y & 15);
  mxt::Touch t;
  mxt::Device::decodeTouch(m, 4095, 4095, t);
  CHECK(t.x == 1234 && t.y == 567, "12-bit x=%u y=%u", t.x, t.y);
  mxt::Device::decodeTouch(m, 1023, 1023, t);
  CHECK(t.x == 1234 / 4 && t.y == 567 / 4, "10-bit x=%u y=%u", t.x, t.y);
}

static void test_init_and_table() {
  g_mxt.setup(4095, 2047);
  g_millis = 0;
  setup();
  CHECK(touchReady, "init failed");
  CHECK(touch.xRange() == 4095 && touch.yRange() == 2047, "range %u %u", touch.xRange(), touch.yRange());
  CHECK(touch.messageSize() == 8, "msg size %u", touch.messageSize());
  const mxt::Object* t9 = touch.find(mxt::T9_TOUCH);
  CHECK(t9 && t9->firstReportId == 2 && t9->reports == 10, "T9 report ids");
  CHECK(touch.isTouchReport(2) && touch.isTouchReport(11) && !touch.isTouchReport(12) && !touch.isTouchReport(1), "touch id range");
  CHECK(touch.isCommandReport(1), "T6 id");
}

static void test_move_and_tap_over_ble() {
  ble.onConnect(nullptr);
  g_ble_reports.clear();
  // движение: 400 counts вправо
  for (int i = 0; i <= 10; i++) {
    g_mxt.touch(0, i == 0 ? (mxt::T9_DETECT | mxt::T9_PRESS) : (mxt::T9_DETECT | mxt::T9_MOVE), 1000 + i * 40, 1000);
    runLoops(1);
  }
  g_mxt.touch(0, mxt::T9_RELEASE, 1400, 1000);
  runLoops(10);
  Sum s = summarize();
  CHECK(s.x > 150 && abs(s.y) <= 1, "move x=%d y=%d", s.x, s.y);
  CHECK(g_ble_reports.size() > 3 && g_ble_reports[0].size() == 5, "report format");

  // тап
  g_ble_reports.clear();
  for (int i = 0; i < 6; i++) {
    g_mxt.touch(0, i == 0 ? (mxt::T9_DETECT | mxt::T9_PRESS) : (mxt::T9_DETECT | mxt::T9_MOVE), 2000, 1000);
    runLoops(1);
  }
  g_mxt.touch(0, mxt::T9_RELEASE, 2000, 1000);
  runLoops(10);
  s = summarize();
  CHECK(s.leftPress == 1 && s.b == 0, "tap clicks=%d final buttons=%d", s.leftPress, s.b);
  CHECK(g_mxt.queue.empty(), "CHG must be released (queue drained)");
}

static void test_two_finger_scroll_over_ble() {
  g_ble_reports.clear();
  for (int i = 0; i <= 12; i++) {
    uint8_t st = i == 0 ? (mxt::T9_DETECT | mxt::T9_PRESS) : (mxt::T9_DETECT | mxt::T9_MOVE);
    g_mxt.touch(0, st, 1500, 1800 - i * 40);
    g_mxt.touch(1, st, 2200, 1800 - i * 40);
    runLoops(1);
  }
  g_mxt.touch(0, mxt::T9_RELEASE, 1500, 1300);
  g_mxt.touch(1, mxt::T9_RELEASE, 2200, 1300);
  runLoops(10);
  Sum s = summarize();
  CHECK(s.w >= 3, "wheel=%d", s.w);
  CHECK(s.x == 0 && s.y == 0, "cursor moved during scroll %d,%d", s.x, s.y);
}

static void test_not_connected_discards() {
  ble.onDisconnect(nullptr);
  g_ble_reports.clear();
  for (int i = 0; i <= 10; i++) {
    g_mxt.touch(0, mxt::T9_DETECT | mxt::T9_MOVE, 1000 + i * 40, 1000);
    runLoops(1);
  }
  g_mxt.touch(0, mxt::T9_RELEASE, 1400, 1000);
  runLoops(5);
  CHECK(g_ble_reports.empty(), "nothing must be sent while disconnected");
}

static void test_missing_chip_then_recovery() {
  g_mxt.present = false;
  g_millis = 0;
  touchReady = false;
  lastInitTry = 0;
  runLoops(5, 1000);
  CHECK(!touchReady, "chip absent but ready");
  g_mxt.present = true;
  g_mxt.setup(4095, 4095);
  runLoops(5, 1000);
  CHECK(touchReady, "must recover when chip appears");
}

static void test_reset_message_clears_fingers() {
  ble.onConnect(nullptr);
  g_mxt.touch(0, mxt::T9_DETECT | mxt::T9_PRESS, 1000, 1000);
  g_mxt.t6(mxt::T6_RESET);
  runLoops(2);
  CHECK(!fingers[0].down, "finger must be cleared on chip reset");
}

int main() {
  test_decode();
  test_init_and_table();
  test_move_and_tap_over_ble();
  test_two_finger_scroll_over_ble();
  test_not_connected_discards();
  test_missing_chip_then_recovery();
  test_reset_message_clears_fingers();
  printf(g_fail ? "system: %d FAILED\n" : "system: all passed\n", g_fail);
  return g_fail ? 1 : 0;
}
