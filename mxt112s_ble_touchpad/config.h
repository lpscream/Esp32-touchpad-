// Настройки проекта: пины, ориентация, чувствительность, жесты.
#pragma once

// ---------------------------------------------------------------- Пины ESP32-S3
// Не используйте GPIO0/3/45/46 (strapping), 19/20 (USB), 26..37 (флеш/PSRAM).
#define PIN_I2C_SDA 8
#define PIN_I2C_SCL 9
#define PIN_TP_CHG 4     // выход CHG тачпада (активный низкий, открытый сток)
#define PIN_TP_RESET 5   // вход RESET тачпада; -1, если не подключён

#define I2C_FREQ_HZ 400000UL   // максимум для mXT112S - 400 кГц
#define MXT_I2C_ADDR 0x4A      // даташит, п. 5.2.1

// ---------------------------------------------------------------- Bluetooth
#define BLE_DEVICE_NAME "ESP32 Touchpad"
#define BLE_MANUFACTURER "DIY"
#define BLE_SEND_INTERVAL_MS 8   // не чаще ~125 отчётов/с

// ---------------------------------------------------------------- Ориентация
// Если курсор идёт «не туда», меняйте эти флаги (0/1).
#define TP_SWAP_XY 0
#define TP_INVERT_X 0
#define TP_INVERT_Y 0

// ---------------------------------------------------------------- Чувствительность
#define POINTER_PX_PER_SPAN 1800.0f   // пикселей за проход по длинной стороне тачпада
#define POINTER_ACCEL 0.5f            // ускорение: 0 - выключено
#define POINTER_ACCEL_MAX 3.0f
#define SCROLL_NOTCHES_PER_SPAN 30.0f
#define SCROLL_NATURAL 0              // 1 - «как на macOS/смартфоне»
#define SCROLL_HORIZONTAL 1

// ---------------------------------------------------------------- Жесты
#define TAP_TO_CLICK 1
#define TAP_DRAG 1
#define TAP_MAX_MS 250
#define TAP_MAX_MOVE_FRAC 0.03f
#define DRAG_WINDOW_MS 300

// ---------------------------------------------------------------- Конфигурация чипа
// Тачпад-модуль обычно уже прошит рабочей конфигурацией (хранится в NVM чипа).
// Включайте только если чип «молчит»: в Serial будет видно T7 = 0 или T9 CTRL
// без бита ENABLE. Значения записываются в RAM и пропадают при снятии питания,
// поэтому применяются при каждом старте.
#define MXT_APPLY_SAFE_DEFAULTS 0
#define MXT_SAVE_TO_NVM 0   // 1 - сохранить во флеш чипа (ресурс 10 000 записей!)

// ---------------------------------------------------------------- Отладка
#define DEBUG_SERIAL 1      // сообщения о старте и ошибках
#define DEBUG_TOUCH 0       // 1 - печатать координаты пальцев (для настройки осей)
