#ifndef CARDPUTER_H
#define CARDPUTER_H

#include <M5GFX.h>
#include <lgfx/v1/panel/Panel_ST7789.hpp>
#include <lgfx/v1/platforms/esp32/Bus_SPI.hpp>
#include <lgfx/v1/platforms/esp32/Light_PWM.hpp>
#include <vector>

// Display class for M5Stack Cardputer (ST7789, 135x240)
class LGFX_Cardputer : public lgfx::LGFX_Device {
public:
    LGFX_Cardputer();
    void init();

private:
    lgfx::Bus_SPI     _bus;
    lgfx::Panel_ST7789 _panel;
    lgfx::Light_PWM   _light;
};

// Keyboard scanner for Cardputer GPIO matrix
class CardputerKeyboard {
public:
    struct KeysState {
        bool tab;
        bool fn;
        bool shift;
        bool ctrl;
        bool del;
        bool enter;
        bool space;
        std::vector<char> word;

        void reset() {
            tab = false;
            fn = false;
            shift = false;
            ctrl = false;
            del = false;
            enter = false;
            space = false;
            word.clear();
        }
    };

    CardputerKeyboard();

    void begin();
    void update();
    bool isChange();
    bool isPressed();
    KeysState& keysState();

private:
    KeysState _state;
    int _lastKeyCount;
    bool _changed;
};

extern LGFX_Cardputer display;
extern CardputerKeyboard keyboard;

#endif // CARDPUTER_H
