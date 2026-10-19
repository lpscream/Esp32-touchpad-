// Минимальный драйвер maXTouch mXT112S (Object Protocol, I2C).
// Нужны только: чтение таблицы объектов, чтение сообщений T5, разбор T9.
#pragma once

#include <Arduino.h>
#include <Wire.h>

namespace mxt {

// Типы объектов (Protocol Guide)
constexpr uint8_t T5_MESSAGE = 5;
constexpr uint8_t T6_COMMAND = 6;
constexpr uint8_t T7_POWER = 7;
constexpr uint8_t T9_TOUCH = 9;

// Статус T9
constexpr uint8_t T9_DETECT = 0x80;
constexpr uint8_t T9_PRESS = 0x40;
constexpr uint8_t T9_RELEASE = 0x20;
constexpr uint8_t T9_MOVE = 0x10;
constexpr uint8_t T9_SUPPRESS = 0x02;

// Статус T6
constexpr uint8_t T6_RESET = 0x80;
constexpr uint8_t T6_CAL = 0x10;
constexpr uint8_t T6_CFGERR = 0x08;

constexpr uint8_t MAX_OBJECTS = 64;
constexpr uint8_t MAX_MSG = 16;

struct Info {
  uint8_t family, variant, version, build, xsize, ysize, objects;
};

struct Object {
  uint8_t type;
  uint16_t addr;
  uint8_t size;       // реальный размер (в таблице хранится size-1)
  uint8_t instances;  // реальное число экземпляров
  uint8_t reports;    // report ID на один экземпляр
  uint8_t firstReportId;
};

struct Touch {
  uint8_t index;  // номер касания внутри T9
  uint8_t status;
  uint16_t x, y;
};

class Device {
 public:
  void configure(TwoWire& wire, uint8_t addr, int chgPin, int resetPin);

  void hardReset();                // импульс на RESET и ожидание готовности
  bool init();                     // ищет чип (0x4A/0x4B), читает блок информации и таблицу объектов
  uint8_t address() const { return addr_; }
  bool changeAsserted() const { return digitalRead(chg_) == LOW; }

  // Читает одно сообщение T5. false - ошибка I2C. msg[0]==0xFF - сообщений нет.
  bool readMessage(uint8_t* msg);
  uint8_t messageSize() const { return msgSize_; }

  bool isTouchReport(uint8_t reportId) const { return inRange(t9_, reportId); }
  bool isCommandReport(uint8_t reportId) const { return inRange(t6_, reportId); }
  bool parseTouch(const uint8_t* msg, Touch& out) const;

  // Максимальные координаты из T9 (XRANGE/YRANGE), 0 если T9 не найден.
  uint16_t xRange() const { return xRange_; }
  uint16_t yRange() const { return yRange_; }

  const Info& info() const { return info_; }
  const Object* find(uint8_t type) const;
  bool hasTouchObject() const { return t9_ != nullptr; }

  bool readObject(uint8_t type, uint16_t offset, uint8_t* buf, uint8_t len);
  bool writeObject(uint8_t type, uint16_t offset, const uint8_t* buf, uint8_t len);

  // full = true: дополнительно печатает байты всех объектов (для сверки конфигурации)
  void dump(Print& out, bool full = false);
  bool applySafeDefaults();
  bool calibrate();
  bool saveToNvm();

  // Разбор T9 без обращения к шине (вынесено для тестов на ПК).
  static void decodeTouch(const uint8_t* msg, uint16_t xRange, uint16_t yRange, Touch& out);

 private:
  bool read(uint16_t addr, uint8_t* buf, uint8_t len);
  bool write(uint16_t addr, const uint8_t* buf, uint8_t len);
  bool inRange(const Object* o, uint8_t id) const {
    return o && id >= o->firstReportId && id < o->firstReportId + o->reports * o->instances;
  }

  TwoWire* wire_ = nullptr;
  uint8_t addr_ = 0x4A;
  uint8_t cfgAddr_ = 0;  // 0 = автоопределение
  bool initAt();
  int chg_ = -1, reset_ = -1;

  Info info_ = {};
  Object objects_[MAX_OBJECTS];
  uint8_t objectCount_ = 0;
  const Object *t5_ = nullptr, *t6_ = nullptr, *t9_ = nullptr;
  uint8_t msgSize_ = 0;
  uint16_t xRange_ = 0, yRange_ = 0;
};

}  // namespace mxt
