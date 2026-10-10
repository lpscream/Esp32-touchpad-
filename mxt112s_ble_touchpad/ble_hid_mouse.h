// BLE HID мышь (HOGP) на встроенной BLE-библиотеке Arduino-ESP32.
// ESP32-S3 не поддерживает классический Bluetooth, поэтому используется BLE HID:
// он работает в Windows 10/11, Android, macOS, iOS, ChromeOS и Linux.
#pragma once

#include <BLEDevice.h>
#include <BLEHIDDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>

#include "gestures.h"

constexpr uint8_t HID_REPORT_ID = 1;

// Отчёт: кнопки (3 бита) + X, Y, колесо, горизонтальная прокрутка (по int8)
static const uint8_t HID_REPORT_MAP[] = {
    0x05, 0x01,        // Usage Page (Generic Desktop)
    0x09, 0x02,        // Usage (Mouse)
    0xA1, 0x01,        // Collection (Application)
    0x85, HID_REPORT_ID,   //   Report ID
    0x09, 0x01,        //   Usage (Pointer)
    0xA1, 0x00,        //   Collection (Physical)
    0x05, 0x09,        //     Usage Page (Button)
    0x19, 0x01,        //     Usage Minimum (1)
    0x29, 0x03,        //     Usage Maximum (3)
    0x15, 0x00,        //     Logical Minimum (0)
    0x25, 0x01,        //     Logical Maximum (1)
    0x95, 0x03,        //     Report Count (3)
    0x75, 0x01,        //     Report Size (1)
    0x81, 0x02,        //     Input (Data, Variable, Absolute)
    0x95, 0x01,        //     Report Count (1)
    0x75, 0x05,        //     Report Size (5)
    0x81, 0x03,        //     Input (Constant) - выравнивание
    0x05, 0x01,        //     Usage Page (Generic Desktop)
    0x09, 0x30,        //     Usage (X)
    0x09, 0x31,        //     Usage (Y)
    0x09, 0x38,        //     Usage (Wheel)
    0x15, 0x81,        //     Logical Minimum (-127)
    0x25, 0x7F,        //     Logical Maximum (127)
    0x75, 0x08,        //     Report Size (8)
    0x95, 0x03,        //     Report Count (3)
    0x81, 0x06,        //     Input (Data, Variable, Relative)
    0x05, 0x0C,        //     Usage Page (Consumer)
    0x0A, 0x38, 0x02,  //     Usage (AC Pan)
    0x15, 0x81,        //     Logical Minimum (-127)
    0x25, 0x7F,        //     Logical Maximum (127)
    0x75, 0x08,        //     Report Size (8)
    0x95, 0x01,        //     Report Count (1)
    0x81, 0x06,        //     Input (Data, Variable, Relative)
    0xC0,              //   End Collection
    0xC0,              // End Collection
};

class BleHidMouse : public BLEServerCallbacks {
 public:
  void begin(const char* name, const char* manufacturer) {
    BLEDevice::init(name);

    // Сопряжение с сохранением ключей (bonding), без PIN-кода
    BLESecurity* security = new BLESecurity();
    security->setAuthenticationMode(ESP_LE_AUTH_BOND);

    server_ = BLEDevice::createServer();
    server_->setCallbacks(this);

    hid_ = new BLEHIDDevice(server_);
    input_ = hid_->inputReport(HID_REPORT_ID);

    hid_->manufacturer()->setValue((uint8_t*)manufacturer, strlen(manufacturer));
    hid_->pnp(0x02, 0xE502, 0xA111, 0x0210);
    hid_->hidInfo(0x00, 0x01);
    hid_->reportMap((uint8_t*)HID_REPORT_MAP, sizeof(HID_REPORT_MAP));
    hid_->startServices();
    hid_->setBatteryLevel(100);

    BLEAdvertising* adv = server_->getAdvertising();
    adv->setAppearance(0x03C2);  // HID Mouse
    adv->addServiceUUID(hid_->hidService()->getUUID());
    adv->start();
  }

  bool connected() const { return connected_; }

  void send(const tp::MouseReport& r) {
    if (!connected_ || !input_) return;
    uint8_t buf[5] = {r.buttons, (uint8_t)r.x, (uint8_t)r.y, (uint8_t)r.wheel, (uint8_t)r.pan};
    input_->setValue(buf, sizeof(buf));
    input_->notify();
  }

  void onConnect(BLEServer*) override { connected_ = true; }
  void onDisconnect(BLEServer*) override {
    connected_ = false;
    server_->getAdvertising()->start();  // снова доступен для подключения
  }

 private:
  BLEServer* server_ = nullptr;
  BLEHIDDevice* hid_ = nullptr;
  BLECharacteristic* input_ = nullptr;
  volatile bool connected_ = false;
};
