// Заглушка BLE-библиотеки Arduino-ESP32: нужна только чтобы собрать и прогнать логику на ПК.
// Сигнатуры повторяют реальный API (BLEDevice.h, BLEHIDDevice.h, BLEServer.h, BLEUtils.h).
#pragma once
#include <stdint.h>
#include <stdlib.h>

#include <vector>

extern std::vector<std::vector<uint8_t>> g_ble_reports;

#define ESP_LE_AUTH_BOND 1

struct BLEUUID {};
struct BLEService {
  BLEUUID getUUID() { return {}; }
};
struct BLEServer;
struct BLEServerCallbacks {
  virtual ~BLEServerCallbacks() {}
  virtual void onConnect(BLEServer*) {}
  virtual void onDisconnect(BLEServer*) {}
};
struct BLECharacteristic {
  std::vector<uint8_t> value;
  void setValue(const char*) {}
  void setValue(uint8_t* d, size_t n) { value.assign(d, d + n); }
  void notify() {
    g_ble_reports.push_back(value);
  }
};
struct BLEAdvertising {
  void setAppearance(uint16_t) {}
  void addServiceUUID(BLEUUID) {}
  void start() {}
};
struct BLESecurity {
  void setAuthenticationMode(int) {}
};
struct BLEServer {
  BLEAdvertising adv;
  void setCallbacks(BLEServerCallbacks*) {}
  BLEAdvertising* getAdvertising() { return &adv; }
};
struct BLEHIDDevice {
  BLECharacteristic in, manu;
  BLEService svc;
  explicit BLEHIDDevice(BLEServer*) {}
  BLECharacteristic* inputReport(uint8_t) { return &in; }
  BLECharacteristic* manufacturer() { return &manu; }
  void pnp(uint8_t, uint16_t, uint16_t, uint16_t) {}
  void hidInfo(uint8_t, uint8_t) {}
  void reportMap(uint8_t*, uint16_t) {}
  void startServices() {}
  void setBatteryLevel(uint8_t) {}
  BLEService* hidService() { return &svc; }
};
struct BLEDevice {
  static void init(const char*) {}
  static BLEServer* createServer() { return new BLEServer(); }
};
