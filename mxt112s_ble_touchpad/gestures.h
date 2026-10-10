// Преобразование касаний тачпада в отчёты HID-мыши.
//
// Файл не зависит от Arduino: время передаётся параметром, поэтому логику
// можно прогнать на ПК (см. tests/test_gestures.cpp).
//
// Жесты:
//   1 палец, движение      -> перемещение курсора (с ускорением)
//   тап 1 пальцем          -> левый клик
//   тап 2 пальцами         -> правый клик
//   тап 3 пальцами         -> средний клик
//   тап и сразу касание    -> перетаскивание (левая кнопка удерживается)
//   2 пальца, движение     -> прокрутка (вертикальная и горизонтальная)
#pragma once

#include <math.h>
#include <stdint.h>

namespace tp {

constexpr uint8_t MAX_FINGERS = 5;

constexpr uint8_t BTN_LEFT = 0x01;
constexpr uint8_t BTN_RIGHT = 0x02;
constexpr uint8_t BTN_MIDDLE = 0x04;

struct Finger {
  bool down;
  int16_t x;
  int16_t y;
};

struct MouseReport {
  uint8_t buttons;
  int8_t x;
  int8_t y;
  int8_t wheel;
  int8_t pan;
};

struct GestureConfig {
  float pointerPxPerSpan = 1800.0f;       // пикселей за проход по длинной стороне
  float accel = 0.5f;                      // прирост усиления на 1 px/мс
  float accelMax = 3.0f;                   // максимум усиления
  float scrollNotchesPerSpan = 30.0f;      // «щелчков» колеса за проход по длинной стороне
  bool naturalScroll = false;              // true: контент следует за пальцами
  bool horizontalScroll = true;
  bool tapToClick = true;
  bool tapDrag = true;
  uint16_t tapMaxMs = 250;                 // максимальная длительность тапа
  float tapMaxMoveFrac = 0.03f;            // максимальный сдвиг при тапе (доля длинной стороны)
  uint16_t dragWindowMs = 300;             // окно «тап -> касание» для перетаскивания
  uint8_t clickHoldMs = 20;                // сколько удерживается кнопка при тапе
};

class Gestures {
 public:
  explicit Gestures(const GestureConfig& cfg = GestureConfig()) : cfg_(cfg) {
    setRange(4095, 4095);
    reset();
  }

  // Максимальные значения координат (после поворота/отражения осей).
  void setRange(uint16_t xRange, uint16_t yRange) {
    float span = (float)(xRange > yRange ? xRange : yRange);
    if (span < 1.0f) span = 1.0f;
    unit_ = cfg_.pointerPxPerSpan / span;
    scrollUnit_ = cfg_.scrollNotchesPerSpan / span;
    tapMove_ = (int32_t)(cfg_.tapMaxMoveFrac * span);
    if (tapMove_ < 2) tapMove_ = 2;
    deadband_ = (int32_t)(span / 1500.0f);
    if (deadband_ < 1) deadband_ = 1;
  }

  void reset() {
    prevCount_ = 0;
    for (uint8_t i = 0; i < MAX_FINGERS; i++) {
      prevDown_[i] = false;
      haveStart_[i] = false;
    }
    accX_ = accY_ = accWheel_ = accPan_ = 0;
    buttons_ = 0;
    pulseMask_ = 0;
    dirty_ = false;
    dragging_ = false;
    lastTapSingle_ = false;
    suppressPointer_ = false;
    lastUpdate_ = 0;
  }

  // Вызывать после каждой порции сообщений от тачпада.
  void update(uint32_t now, const Finger* f, uint8_t n) {
    if (n > MAX_FINGERS) n = MAX_FINGERS;
    uint8_t count = 0;
    for (uint8_t i = 0; i < n; i++)
      if (f[i].down) count++;

    uint32_t dt = now - lastUpdate_;
    if (dt < 1) dt = 1;
    if (dt > 50) dt = 50;
    lastUpdate_ = now;

    if (count > 0 && prevCount_ == 0) beginSession(now, count);

    if (count > 0) {
      if (count > maxCount_) maxCount_ = count;
      trackDisplacement(f, n);
    }

    if (count != prevCount_) {
      // число пальцев изменилось: пересчитываем базу, чтобы не было скачка
      if (count == 1 && prevCount_ >= 2) suppressPointer_ = true;
      rebase(f, n);
    } else if (count == 1) {
      if (!suppressPointer_) movePointer(f, n, dt);
    } else if (count == 2) {
      scroll(f, n);
    }

    if (count == 0 && prevCount_ > 0) endSession(now);
    prevCount_ = count;
  }

  // Формирует следующий отчёт; false, если отправлять нечего.
  bool poll(uint32_t now, MouseReport& out) {
    if (pulseMask_ && (int32_t)(now - releaseAt_) >= 0) {
      buttons_ &= (uint8_t)~pulseMask_;
      pulseMask_ = 0;
      dirty_ = true;
    }
    int ix = take(accX_), iy = take(accY_), iw = take(accWheel_), ip = take(accPan_);
    if (!ix && !iy && !iw && !ip && !dirty_) return false;
    out.buttons = buttons_;
    out.x = (int8_t)ix;
    out.y = (int8_t)iy;
    out.wheel = (int8_t)iw;
    out.pan = (int8_t)ip;
    dirty_ = false;
    return true;
  }

  bool hasPending() const {
    return dirty_ || pulseMask_ || fabsf(accX_) >= 1 || fabsf(accY_) >= 1 ||
           fabsf(accWheel_) >= 1 || fabsf(accPan_) >= 1;
  }

 private:
  static int take(float& acc) {
    int v = (int)acc;  // усечение к нулю: остаток остаётся в аккумуляторе
    if (v > 127) v = 127;
    if (v < -127) v = -127;
    acc -= (float)v;
    return v;
  }

  void beginSession(uint32_t now, uint8_t count) {
    sessionStart_ = now;
    maxCount_ = 0;
    maxDisp_ = 0;
    suppressPointer_ = false;
    for (uint8_t i = 0; i < MAX_FINGERS; i++) haveStart_[i] = false;
    if (cfg_.tapDrag && count == 1 && lastTapSingle_ &&
        (now - lastTapEnd_) <= cfg_.dragWindowMs) {
      dragging_ = true;
      pulseMask_ = 0;
      buttons_ |= BTN_LEFT;
      dirty_ = true;
    }
    lastTapSingle_ = false;
  }

  void trackDisplacement(const Finger* f, uint8_t n) {
    for (uint8_t i = 0; i < n; i++) {
      if (!f[i].down) {
        haveStart_[i] = false;
        continue;
      }
      if (!haveStart_[i]) {
        haveStart_[i] = true;
        startX_[i] = f[i].x;
        startY_[i] = f[i].y;
      } else {
        int32_t d = abs32(f[i].x - startX_[i]) + abs32(f[i].y - startY_[i]);
        if (d > maxDisp_) maxDisp_ = d;
      }
    }
  }

  void endSession(uint32_t now) {
    if (dragging_) {
      buttons_ &= (uint8_t)~BTN_LEFT;
      dirty_ = true;
      dragging_ = false;
      return;
    }
    if (!cfg_.tapToClick) return;
    if ((now - sessionStart_) > cfg_.tapMaxMs || maxDisp_ > tapMove_) return;
    uint8_t b = maxCount_ == 1 ? BTN_LEFT : (maxCount_ == 2 ? BTN_RIGHT : BTN_MIDDLE);
    buttons_ |= b;
    pulseMask_ |= b;
    releaseAt_ = now + cfg_.clickHoldMs;
    dirty_ = true;
    if (maxCount_ == 1) {
      lastTapSingle_ = true;
      lastTapEnd_ = now;
    }
  }

  void rebase(const Finger* f, uint8_t n) {
    for (uint8_t i = 0; i < MAX_FINGERS; i++) {
      prevDown_[i] = (i < n) && f[i].down;
      if (prevDown_[i]) {
        prevX_[i] = f[i].x;
        prevY_[i] = f[i].y;
      }
    }
  }

  void movePointer(const Finger* f, uint8_t n, uint32_t dt) {
    for (uint8_t i = 0; i < n; i++) {
      if (!f[i].down) continue;
      if (!prevDown_[i]) {
        rebase(f, n);
        return;
      }
      int32_t dx = f[i].x - prevX_[i];
      int32_t dy = f[i].y - prevY_[i];
      if (abs32(dx) < deadband_ && abs32(dy) < deadband_) return;  // копим мелкое дрожание
      float px = (float)dx * unit_, py = (float)dy * unit_;
      float v = sqrtf(px * px + py * py) / (float)dt;  // px/мс
      float gain = 1.0f + cfg_.accel * v;
      if (gain > cfg_.accelMax) gain = cfg_.accelMax;
      accX_ += px * gain;
      accY_ += py * gain;
      prevX_[i] = f[i].x;
      prevY_[i] = f[i].y;
      return;
    }
  }

  void scroll(const Finger* f, uint8_t n) {
    uint8_t idx[2];
    uint8_t k = 0;
    for (uint8_t i = 0; i < n && k < 2; i++)
      if (f[i].down) idx[k++] = i;
    if (k < 2 || !prevDown_[idx[0]] || !prevDown_[idx[1]]) {
      rebase(f, n);
      return;
    }
    float dx = ((f[idx[0]].x - prevX_[idx[0]]) + (f[idx[1]].x - prevX_[idx[1]])) * 0.5f;
    float dy = ((f[idx[0]].y - prevY_[idx[0]]) + (f[idx[1]].y - prevY_[idx[1]])) * 0.5f;
    if (fabsf(dx) < (float)deadband_ && fabsf(dy) < (float)deadband_) return;
    // подавляем «слабую» ось, чтобы вертикальная прокрутка не уходила вбок
    if (fabsf(dx) < fabsf(dy) * 0.5f) dx = 0;
    if (fabsf(dy) < fabsf(dx) * 0.5f) dy = 0;
    float sign = cfg_.naturalScroll ? 1.0f : -1.0f;
    accWheel_ += sign * dy * scrollUnit_;
    if (cfg_.horizontalScroll) accPan_ += -sign * dx * scrollUnit_;
    for (uint8_t j = 0; j < 2; j++) {
      prevX_[idx[j]] = f[idx[j]].x;
      prevY_[idx[j]] = f[idx[j]].y;
    }
  }

  static int32_t abs32(int32_t v) { return v < 0 ? -v : v; }

  GestureConfig cfg_;
  float unit_ = 1, scrollUnit_ = 1;
  int32_t tapMove_ = 2, deadband_ = 1;

  uint8_t prevCount_ = 0;
  bool prevDown_[MAX_FINGERS];
  int16_t prevX_[MAX_FINGERS] = {0}, prevY_[MAX_FINGERS] = {0};
  bool haveStart_[MAX_FINGERS];
  int16_t startX_[MAX_FINGERS] = {0}, startY_[MAX_FINGERS] = {0};

  uint32_t lastUpdate_ = 0, sessionStart_ = 0, lastTapEnd_ = 0, releaseAt_ = 0;
  uint8_t maxCount_ = 0;
  int32_t maxDisp_ = 0;
  bool dragging_ = false, lastTapSingle_ = false, suppressPointer_ = false;

  float accX_ = 0, accY_ = 0, accWheel_ = 0, accPan_ = 0;
  uint8_t buttons_ = 0, pulseMask_ = 0;
  bool dirty_ = false;
};

}  // namespace tp
