// Bluetooth-тачпад: Atmel/Microchip mXT112S (I2C) + ESP32-S3 (BLE HID мышь).
//
//   тачпад --I2C--> ESP32-S3 --BLE HID--> ПК / телефон / планшет
//
// Плата в Arduino IDE: "ESP32S3 Dev Module", USB CDC On Boot: Enabled (для Serial).
// Схема подключения и описание жестов - в README.md.

#include <Wire.h>

#include "ble_hid_mouse.h"
#include "config.h"
#include "gestures.h"
#include "mxt112s.h"

static mxt::Device touch;
static tp::Gestures gestures;
static BleHidMouse ble;

static tp::Finger fingers[tp::MAX_FINGERS];
static uint16_t rangeX = 4095, rangeY = 4095;  // диапазон после поворота/отражения осей

static bool touchReady = false;
static uint8_t i2cErrors = 0;
static uint32_t lastInitTry = 0, lastSend = 0;

#if DEBUG_SERIAL
#define LOG(...) Serial.printf(__VA_ARGS__)
#else
#define LOG(...) \
  do {           \
  } while (0)
#endif

static void scanI2c() {
  LOG("Сканирование I2C:");
  uint8_t found = 0;
  for (uint8_t a = 1; a < 127; a++) {
    Wire.beginTransmission(a);
    if (Wire.endTransmission() == 0) {
      LOG(" 0x%02X", a);
      found++;
    }
  }
  LOG(found ? "\n" : " устройств нет (проверьте питание, SDA/SCL и подтяжки)\n");
}

// Координаты чипа -> логические (с учётом TP_SWAP_XY / TP_INVERT_*)
static void toLogical(uint16_t rx, uint16_t ry, int16_t& x, int16_t& y) {
  int32_t lx = rx, ly = ry;
#if TP_SWAP_XY || TP_INVERT_X || TP_INVERT_Y
  int32_t mx = touch.xRange() ? touch.xRange() : 4095;
  int32_t my = touch.yRange() ? touch.yRange() : 4095;
#endif
#if TP_SWAP_XY
  int32_t t = lx; lx = ly; ly = t;
  t = mx; mx = my; my = t;
#endif
#if TP_INVERT_X
  lx = mx - lx;
#endif
#if TP_INVERT_Y
  ly = my - ly;
#endif
  x = (int16_t)lx;
  y = (int16_t)ly;
}

static void applyRanges() {
  uint16_t xr = touch.xRange() ? touch.xRange() : 4095;
  uint16_t yr = touch.yRange() ? touch.yRange() : 4095;
#if TP_SWAP_XY
  rangeX = yr;
  rangeY = xr;
#else
  rangeX = xr;
  rangeY = yr;
#endif
  gestures.setRange(rangeX, rangeY);
}

static bool initTouchpad() {
  touch.hardReset();
  if (!touch.init()) return false;

#if DEBUG_SERIAL
  touch.dump(Serial);
#endif
  if (!touch.hasTouchObject()) return false;

#if MXT_APPLY_SAFE_DEFAULTS
  LOG("Применяю T7/T9 по умолчанию (RAM)\n");
  touch.applySafeDefaults();
  touch.calibrate();
#if MXT_SAVE_TO_NVM
  touch.saveToNvm();
  delay(100);
#endif
#endif

  // Забираем стартовые сообщения (сброс, калибровка), иначе CHG останется низким
  uint8_t msg[mxt::MAX_MSG];
  for (uint8_t i = 0; i < 32 && touch.changeAsserted(); i++) {
    if (!touch.readMessage(msg)) return false;
  }

  applyRanges();
  for (auto& f : fingers) f.down = false;
  gestures.reset();
  return true;
}

// Обработка всех ожидающих сообщений. true - были данные о касаниях.
static bool pumpTouchMessages() {
  uint8_t msg[mxt::MAX_MSG];
  bool touched = false;

  // CHG - уровневое прерывание: читаем, пока линия низкая (не более 24 сообщений за проход)
  for (uint8_t n = 0; n < 24 && touch.changeAsserted(); n++) {
    if (!touch.readMessage(msg)) {
      if (++i2cErrors > 10) touchReady = false;  // переинициализация в loop()
      return touched;
    }
    i2cErrors = 0;
    if (msg[0] == 0xFF) break;  // «пустое» сообщение

    mxt::Touch t;
    if (touch.parseTouch(msg, t)) {
      if (t.index >= tp::MAX_FINGERS) continue;
      tp::Finger& f = fingers[t.index];
      if (t.status & (mxt::T9_RELEASE | mxt::T9_SUPPRESS)) {
        f.down = false;
      } else if (t.status & mxt::T9_DETECT) {
        f.down = true;
        toLogical(t.x, t.y, f.x, f.y);
#if DEBUG_TOUCH
        Serial.printf("finger %u: raw=(%u,%u) logical=(%d,%d) status=0x%02X\n", t.index, t.x, t.y,
                      f.x, f.y, t.status);
#endif
      }
      touched = true;
    } else if (touch.isCommandReport(msg[0])) {
      LOG("T6 status=0x%02X%s%s%s\n", msg[1], (msg[1] & mxt::T6_RESET) ? " RESET" : "",
          (msg[1] & mxt::T6_CAL) ? " CAL" : "", (msg[1] & mxt::T6_CFGERR) ? " CFGERR" : "");
      if (msg[1] & (mxt::T6_RESET | mxt::T6_CAL)) {
        for (auto& f : fingers) f.down = false;  // после сброса/калибровки касаний нет
        touched = true;
      }
    }
  }
  return touched;
}

void setup() {
#if DEBUG_SERIAL
  Serial.begin(115200);
  delay(300);
  Serial.println("\nmXT112S BLE touchpad");
#endif

  Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL);
  Wire.setClock(I2C_FREQ_HZ);
  Wire.setTimeOut(20);

  tp::GestureConfig cfg;
  cfg.pointerPxPerSpan = POINTER_PX_PER_SPAN;
  cfg.accel = POINTER_ACCEL;
  cfg.accelMax = POINTER_ACCEL_MAX;
  cfg.scrollNotchesPerSpan = SCROLL_NOTCHES_PER_SPAN;
  cfg.naturalScroll = SCROLL_NATURAL;
  cfg.horizontalScroll = SCROLL_HORIZONTAL;
  cfg.tapToClick = TAP_TO_CLICK;
  cfg.tapDrag = TAP_DRAG;
  cfg.tapMaxMs = TAP_MAX_MS;
  cfg.tapMaxMoveFrac = TAP_MAX_MOVE_FRAC;
  cfg.dragWindowMs = DRAG_WINDOW_MS;
  gestures = tp::Gestures(cfg);

  touch.configure(Wire, MXT_I2C_ADDR, PIN_TP_CHG, PIN_TP_RESET);
  touchReady = initTouchpad();
  if (!touchReady) {
    LOG("Тачпад не отвечает по адресу 0x%02X\n", MXT_I2C_ADDR);
    scanI2c();
  }
  lastInitTry = millis();

  ble.begin(BLE_DEVICE_NAME, BLE_MANUFACTURER);
  LOG("BLE запущен, имя: %s\n", BLE_DEVICE_NAME);
}

void loop() {
  uint32_t now = millis();

  if (!touchReady) {
    if (now - lastInitTry >= 2000) {  // повторная попытка каждые 2 с
      lastInitTry = now;
      i2cErrors = 0;
      touchReady = initTouchpad();
      LOG(touchReady ? "Тачпад подключён\n" : "Тачпад не отвечает\n");
    }
  } else if (pumpTouchMessages()) {
    gestures.update(now, fingers, tp::MAX_FINGERS);
  }

  tp::MouseReport report;
  if (!ble.connected()) {
    gestures.poll(now, report);  // без подключения просто сбрасываем накопленное
  } else if (now - lastSend >= BLE_SEND_INTERVAL_MS && gestures.poll(now, report)) {
    ble.send(report);
    lastSend = now;
  }

  delay(1);
}
