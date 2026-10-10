#include "mxt112s.h"

namespace mxt {

namespace {
constexpr uint16_t INFO_BLOCK_SIZE = 7;
constexpr uint16_t OBJECT_ENTRY_SIZE = 6;
constexpr uint8_t I2C_CHUNK = 32;

// Смещения полей внутри объектов
constexpr uint8_t T6_RESET_OFS = 0;
constexpr uint8_t T6_BACKUPNV_OFS = 1;
constexpr uint8_t T6_CALIBRATE_OFS = 2;
constexpr uint8_t T9_CTRL_OFS = 0;
constexpr uint8_t T9_XSIZE_OFS = 3;
constexpr uint8_t T9_YSIZE_OFS = 4;
constexpr uint8_t T9_ORIENT_OFS = 9;
constexpr uint8_t T9_NUMTOUCH_OFS = 14;
constexpr uint8_t T9_XRANGE_OFS = 18;  // LSB, MSB
constexpr uint8_t T9_YRANGE_OFS = 20;  // LSB, MSB
}  // namespace

void Device::configure(TwoWire& wire, uint8_t addr, int chgPin, int resetPin) {
  wire_ = &wire;
  addr_ = addr;
  chg_ = chgPin;
  reset_ = resetPin;
  pinMode(chg_, INPUT_PULLUP);  // основная подтяжка - внешний резистор 10 кОм
}

void Device::hardReset() {
  if (reset_ < 0) return;
  pinMode(reset_, OUTPUT);
  digitalWrite(reset_, LOW);  // импульс должен быть >= 90 нс
  delay(5);
  digitalWrite(reset_, HIGH);
  // ~20 мс до готовности и ещё ~100 мс линия CHG работает как вход
  delay(150);
}

bool Device::read(uint16_t addr, uint8_t* buf, uint8_t len) {
  while (len) {
    uint8_t n = len > I2C_CHUNK ? I2C_CHUNK : len;
    wire_->beginTransmission(addr_);
    wire_->write((uint8_t)(addr & 0xFF));
    wire_->write((uint8_t)(addr >> 8));
    if (wire_->endTransmission() != 0) return false;
    if (wire_->requestFrom((uint8_t)addr_, (uint8_t)n) != n) return false;
    for (uint8_t i = 0; i < n; i++) buf[i] = (uint8_t)wire_->read();
    buf += n;
    addr += n;
    len -= n;
  }
  return true;
}

bool Device::write(uint16_t addr, const uint8_t* buf, uint8_t len) {
  if (len > I2C_CHUNK - 2) return false;
  wire_->beginTransmission(addr_);
  wire_->write((uint8_t)(addr & 0xFF));
  wire_->write((uint8_t)(addr >> 8));
  for (uint8_t i = 0; i < len; i++) wire_->write(buf[i]);
  return wire_->endTransmission() == 0;
}

bool Device::init() {
  t5_ = t6_ = t9_ = nullptr;
  objectCount_ = 0;
  msgSize_ = 0;
  xRange_ = yRange_ = 0;

  uint8_t hdr[INFO_BLOCK_SIZE];
  if (!read(0x0000, hdr, sizeof(hdr))) return false;
  info_ = {hdr[0], hdr[1], hdr[2], hdr[3], hdr[4], hdr[5], hdr[6]};
  if (info_.objects == 0 || info_.objects > MAX_OBJECTS) return false;

  uint8_t reportId = 1;  // нумерация report ID идёт по таблице, начиная с 1
  for (uint8_t i = 0; i < info_.objects; i++) {
    uint8_t e[OBJECT_ENTRY_SIZE];
    if (!read(INFO_BLOCK_SIZE + i * OBJECT_ENTRY_SIZE, e, sizeof(e))) return false;
    Object& o = objects_[i];
    o.type = e[0];
    o.addr = (uint16_t)e[1] | ((uint16_t)e[2] << 8);
    o.size = e[3] + 1;
    o.instances = e[4] + 1;
    o.reports = e[5];
    o.firstReportId = reportId;
    reportId += o.reports * o.instances;
  }
  objectCount_ = info_.objects;

  t5_ = find(T5_MESSAGE);
  t6_ = find(T6_COMMAND);
  t9_ = find(T9_TOUCH);
  if (!t5_ || !t6_) return false;

  // CRC в сообщениях не включён, поэтому последний байт T5 не читаем
  msgSize_ = t5_->size - 1;
  if (msgSize_ > MAX_MSG || msgSize_ < 2) return false;

  if (t9_) {
    uint8_t r[4];
    if (readObject(T9_TOUCH, T9_XRANGE_OFS, r, 4)) {
      xRange_ = r[0] | (r[1] << 8);
      yRange_ = r[2] | (r[3] << 8);
    }
  }
  return true;
}

const Object* Device::find(uint8_t type) const {
  for (uint8_t i = 0; i < objectCount_; i++)
    if (objects_[i].type == type) return &objects_[i];
  return nullptr;
}

bool Device::readMessage(uint8_t* msg) {
  if (!t5_) return false;
  return read(t5_->addr, msg, msgSize_);
}

bool Device::readObject(uint8_t type, uint16_t offset, uint8_t* buf, uint8_t len) {
  const Object* o = find(type);
  if (!o || offset + len > o->size) return false;
  return read(o->addr + offset, buf, len);
}

bool Device::writeObject(uint8_t type, uint16_t offset, const uint8_t* buf, uint8_t len) {
  const Object* o = find(type);
  if (!o || offset + len > o->size) return false;
  return write(o->addr + offset, buf, len);
}

void Device::decodeTouch(const uint8_t* msg, uint16_t xRange, uint16_t yRange, Touch& out) {
  out.status = msg[1];
  uint16_t x = ((uint16_t)msg[2] << 4) | ((msg[4] >> 4) & 0x0F);
  uint16_t y = ((uint16_t)msg[3] << 4) | (msg[4] & 0x0F);
  // Если диапазон меньше 1024, чип отдаёт 10-битные координаты в 12-битной раскладке
  if (xRange && xRange < 1024) x >>= 2;
  if (yRange && yRange < 1024) y >>= 2;
  out.x = x;
  out.y = y;
}

bool Device::parseTouch(const uint8_t* msg, Touch& out) const {
  if (!t9_ || !isTouchReport(msg[0])) return false;
  out.index = msg[0] - t9_->firstReportId;
  decodeTouch(msg, xRange_, yRange_, out);
  return true;
}

bool Device::applySafeDefaults() {
  // T7: период опроса в простое/активном режиме (мс) и таймаут перехода в простой (x200 мс).
  // Нули означают «глубокий сон» - чип не будет сканировать сенсор.
  const uint8_t power[3] = {32, 10, 50};
  bool ok = writeObject(T7_POWER, 0, power, sizeof(power));
  // T9 CTRL: бит0 ENABLE, бит1 RPTEN, бит7 SCANEN
  const uint8_t ctrl = 0x83;
  ok &= writeObject(T9_TOUCH, T9_CTRL_OFS, &ctrl, 1);
  return ok;
}

bool Device::calibrate() {
  const uint8_t one = 1;
  return writeObject(T6_COMMAND, T6_CALIBRATE_OFS, &one, 1);
}

bool Device::saveToNvm() {
  const uint8_t magic = 0x55;
  return writeObject(T6_COMMAND, T6_BACKUPNV_OFS, &magic, 1);
}

void Device::dump(Print& out, bool full) {
  out.printf("mXT: family=0x%02X variant=0x%02X version=0x%02X build=0x%02X matrix=%ux%u objects=%u\n",
             info_.family, info_.variant, info_.version, info_.build, info_.xsize, info_.ysize,
             info_.objects);
  for (uint8_t i = 0; i < objectCount_; i++) {
    const Object& o = objects_[i];
    out.printf("  T%-3u addr=0x%04X size=%-3u inst=%u reports=%u firstId=%u\n", o.type, o.addr,
               o.size, o.instances, o.reports, o.firstReportId);
  }
  uint8_t b[3];
  if (readObject(T7_POWER, 0, b, 3))
    out.printf("T7 power: idle=%u ms active=%u ms idle-timeout=%u x200ms%s\n", b[0], b[1], b[2],
               (b[0] == 0 || b[1] == 0) ? "  <-- 0 = deep sleep!" : "");
  if (t9_) {
    uint8_t ctrl = 0, xs = 0, ys = 0, ori = 0, num = 0;
    readObject(T9_TOUCH, T9_CTRL_OFS, &ctrl, 1);
    readObject(T9_TOUCH, T9_XSIZE_OFS, &xs, 1);
    readObject(T9_TOUCH, T9_YSIZE_OFS, &ys, 1);
    readObject(T9_TOUCH, T9_ORIENT_OFS, &ori, 1);
    readObject(T9_TOUCH, T9_NUMTOUCH_OFS, &num, 1);
    out.printf("T9 touch: ctrl=0x%02X%s size=%u,%u orient=0x%02X numtouch=%u range=%ux%u\n", ctrl,
               (ctrl & 1) ? "" : "  <-- ENABLE bit is off!", xs, ys, ori, num, xRange_, yRange_);
    if (xs == 0 || ys == 0 || xRange_ == 0 || yRange_ == 0 || num == 0)
      out.println("!!! T9 не сконфигурирован (размер/диапазон/numtouch = 0): в энергонезависимой памяти "
                  "чипа, вероятно, нет конфигурации. Без неё касания не определяются.");
    if (full) {
      for (uint8_t i = 0; i < objectCount_; i++) {
        const Object& o = objects_[i];
        out.printf("  T%u:", o.type);
        for (uint8_t k = 0; k < o.size; k++) {
          uint8_t v;
          if (!read(o.addr + k, &v, 1)) break;
          out.printf(" %02X", v);
        }
        out.println();
      }
    }
  } else {
    out.println("T9 not found - this firmware is not supported (T100?)");
  }
}

}  // namespace mxt
