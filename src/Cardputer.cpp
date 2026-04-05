#include "Cardputer.h"
#include "driver/gpio.h"
#include "esp_rom_sys.h"

// Global instances
LGFX_Cardputer display;
CardputerKeyboard keyboard;

//=============================================================================
// LGFX_Cardputer
//=============================================================================

LGFX_Cardputer::LGFX_Cardputer() {
    // Configure SPI bus
    {
        auto cfg = _bus.config();
        cfg.spi_host    = SPI3_HOST;
        cfg.freq_write  = 40000000;
        cfg.freq_read   = 16000000;
        cfg.spi_3wire   = true;
        cfg.use_lock    = true;
        cfg.dma_channel = SPI_DMA_CH_AUTO;
        cfg.pin_sclk    = 36;
        cfg.pin_mosi    = 35;
        cfg.pin_miso    = -1;
        cfg.pin_dc      = 34;
        _bus.config(cfg);
        _panel.setBus(&_bus);
    }

    // Configure panel
    {
        auto cfg = _panel.config();
        cfg.pin_cs        = 37;
        cfg.pin_rst       = 33;
        cfg.memory_width  = 240;
        cfg.memory_height = 320;
        cfg.panel_width   = 135;
        cfg.panel_height  = 240;
        cfg.offset_x      = 52;
        cfg.offset_y      = 40;
        cfg.invert        = true;
        cfg.readable      = false;
        cfg.rgb_order     = false;
        cfg.dlen_16bit    = false;
        cfg.bus_shared    = false;
        _panel.config(cfg);
    }

    // Configure backlight
    {
        auto cfg = _light.config();
        cfg.pin_bl      = 38;
        cfg.invert      = false;
        cfg.freq        = 256;
        cfg.pwm_channel = 7;
        _light.config(cfg);
        _panel.setLight(&_light);
    }

    setPanel(&_panel);
}

void LGFX_Cardputer::init() {
    lgfx::LGFX_Device::init();
    setRotation(1);
    setBrightness(128);
}

//=============================================================================
// CardputerKeyboard
//=============================================================================

// Key map: [row 0..3][col 0..13] — {normal, shifted}
struct KeyValue_t {
    char value_first;
    char value_second;
};

static const KeyValue_t _key_value_map[4][14] = {
    {{'`','~'},{'1','!'},{'2','@'},{'3','#'},{'4','$'},{'5','%'},{'6','^'},{'7','&'},{'8','*'},{'9','('},{'0',')'},  {'-','_'},{'=','+'},{'\b','\b'}},
    {{'\t','\t'},{'q','Q'},{'w','W'},{'e','E'},{'r','R'},{'t','T'},{'y','Y'},{'u','U'},{'i','I'},{'o','O'},{'p','P'},{'[','{'},  {']','}'},{'\\','|'}},
    {{'\xff','\xff'},{'\x81','\x81'},{'a','A'},{'s','S'},{'d','D'},{'f','F'},{'g','G'},{'h','H'},{'j','J'},{'k','K'},{'l','L'},{';',':'},{'\''  ,'"'},{'\n','\n'}},
    {{'\x80','\x80'},{'\x00','\x00'},{'\x82','\x82'},{'z','Z'},{'x','X'},{'c','C'},{'v','V'},{'b','B'},{'n','N'},{'m','M'},{',','<'},  {'.','>'},{'/','?'},{' ',' '}}
};

// X coordinate lookup for each row bit j=0..6:
// x1[j] used when i>3, x2[j] used when i<=3
static const uint8_t _x1[7] = {0, 2, 4, 6, 8, 10, 12};
static const uint8_t _x2[7] = {1, 3, 5, 7, 9, 11, 13};

// Output pins (3-bit column select)
static const gpio_num_t OUT_PINS[3] = {
    (gpio_num_t)8, (gpio_num_t)9, (gpio_num_t)11
};

// Input pins (7 rows, active-low)
static const gpio_num_t IN_PINS[7] = {
    (gpio_num_t)13, (gpio_num_t)15, (gpio_num_t)3,
    (gpio_num_t)4,  (gpio_num_t)5,  (gpio_num_t)6, (gpio_num_t)7
};

CardputerKeyboard::CardputerKeyboard()
    : _lastKeyCount(0), _changed(false) {
    _state.reset();
}

void CardputerKeyboard::begin() {
    // Configure output pins
    for (int i = 0; i < 3; i++) {
        gpio_config_t cfg = {};
        cfg.pin_bit_mask = (1ULL << OUT_PINS[i]);
        cfg.mode         = GPIO_MODE_OUTPUT;
        cfg.pull_up_en   = GPIO_PULLUP_DISABLE;
        cfg.pull_down_en = GPIO_PULLDOWN_DISABLE;
        cfg.intr_type    = GPIO_INTR_DISABLE;
        gpio_config(&cfg);
        gpio_set_level(OUT_PINS[i], 0);
    }

    // Configure input pins with pullup
    for (int j = 0; j < 7; j++) {
        gpio_config_t cfg = {};
        cfg.pin_bit_mask = (1ULL << IN_PINS[j]);
        cfg.mode         = GPIO_MODE_INPUT;
        cfg.pull_up_en   = GPIO_PULLUP_ENABLE;
        cfg.pull_down_en = GPIO_PULLDOWN_DISABLE;
        cfg.intr_type    = GPIO_INTR_DISABLE;
        gpio_config(&cfg);
    }
}

void CardputerKeyboard::update() {
    _state.reset();

    bool shiftDown = false;

    // Two-pass: first detect modifiers, then collect printable keys
    struct PressedKey { uint8_t x; uint8_t y; };
    PressedKey pressed[56];
    int pressedCount = 0;

    for (int i = 0; i < 8; i++) {
        gpio_set_level(OUT_PINS[0], i & 1);
        gpio_set_level(OUT_PINS[1], (i >> 1) & 1);
        gpio_set_level(OUT_PINS[2], (i >> 2) & 1);

        esp_rom_delay_us(10);

        for (int j = 0; j < 7; j++) {
            if (gpio_get_level(IN_PINS[j]) == 0) {
                uint8_t x = (i > 3) ? _x1[j] : _x2[j];
                uint8_t y_raw = (i > 3) ? (i - 4) : i;
                uint8_t y = 3 - y_raw;

                if (x < 14 && y < 4) {
                    if (pressedCount < 56) {
                        pressed[pressedCount++] = {x, y};
                    }
                }
            }
        }
    }

    // First pass: detect modifier keys
    for (int k = 0; k < pressedCount; k++) {
        uint8_t x = pressed[k].x;
        uint8_t y = pressed[k].y;
        char v = _key_value_map[y][x].value_first;

        if (v == '\xff') { _state.fn = true; }
        else if (v == '\x81') { _state.shift = true; shiftDown = true; }
        else if (v == '\x80') { _state.ctrl = true; }
        else if (v == '\x82') { /* alt — no state field */ }
    }

    // Second pass: collect non-modifier keys
    for (int k = 0; k < pressedCount; k++) {
        uint8_t x = pressed[k].x;
        uint8_t y = pressed[k].y;
        char v_first  = _key_value_map[y][x].value_first;
        char v_second = _key_value_map[y][x].value_second;

        // Skip modifier keys
        if (v_first == '\xff' || v_first == '\x81' || v_first == '\x80' || v_first == '\x82') {
            continue;
        }
        // Skip OPT (row3 col1 = {'\x00','\x00'})
        if (x == 1 && y == 3) {
            continue;
        }

        char ch = shiftDown ? v_second : v_first;

        if (ch == '\b') { _state.del = true; }
        else if (ch == '\n') { _state.enter = true; }
        else if (ch == '\t') { _state.tab = true; }
        else if (ch == ' ') { _state.space = true; _state.word.push_back(ch); }
        else if (ch >= 0x20 && ch < 0x7f) { _state.word.push_back(ch); }
    }

    int currentCount = pressedCount;
    _changed = (currentCount != _lastKeyCount);
    _lastKeyCount = currentCount;
}

bool CardputerKeyboard::isChange() {
    bool c = _changed;
    _changed = false;
    return c;
}

bool CardputerKeyboard::isPressed() {
    return _state.word.size() > 0 || _state.enter || _state.del || _state.tab;
}

CardputerKeyboard::KeysState& CardputerKeyboard::keysState() {
    return _state;
}
