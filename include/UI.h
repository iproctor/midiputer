#ifndef UI_H
#define UI_H

#include <M5Cardputer.h>
#include "MidiTypes.h"
#include "Routing.h"
#include "MidiDevice.h"

// UI Colors
namespace Colors {
    constexpr uint16_t BACKGROUND     = 0x0000;  // Black
    constexpr uint16_t TEXT           = 0xFFFF;  // White
    constexpr uint16_t TEXT_DIM       = 0x7BEF;  // Gray
    constexpr uint16_t TEXT_DISABLED  = 0x4208;  // Dark gray
    constexpr uint16_t HIGHLIGHT      = 0x07FF;  // Cyan
    constexpr uint16_t HIGHLIGHT_BG   = 0x0410;  // Dark cyan
    constexpr uint16_t ACTIVE         = 0x07E0;  // Green
    constexpr uint16_t INACTIVE       = 0xF800;  // Red
    constexpr uint16_t WARNING        = 0xFD20;  // Orange
    constexpr uint16_t HEADER_BG      = 0x18E3;  // Dark blue
    constexpr uint16_t MIDI_FLASH     = 0xFFE0;  // Yellow
    constexpr uint16_t BORDER         = 0x4208;  // Dark gray
}

// Screen dimensions for Cardputer (240x135)
constexpr int16_t SCREEN_WIDTH = 240;
constexpr int16_t SCREEN_HEIGHT = 135;
constexpr int16_t HEADER_HEIGHT = 20;
constexpr int16_t FOOTER_HEIGHT = 16;
constexpr int16_t LINE_HEIGHT = 16;
constexpr int16_t CONTENT_HEIGHT = SCREEN_HEIGHT - HEADER_HEIGHT - FOOTER_HEIGHT;
constexpr int16_t MAX_VISIBLE_ITEMS = CONTENT_HEIGHT / LINE_HEIGHT;

// UI Screens
enum class Screen {
    ROUTING_LIST,    // Main screen - list of routings
    ROUTING_EDIT,    // Edit a routing
    DEVICE_SELECT,   // Select source or destination device
    CHANNEL_SELECT,  // Select channels to route
    SETTINGS,        // Settings screen
    CONFIRM_DELETE   // Confirm deletion dialog
};

// Edit mode for routing edit screen
enum class EditField {
    SOURCE,
    DESTINATION,
    CHANNELS,
    ENABLED
};

// UI Manager
class UIManager {
public:
    UIManager();
    ~UIManager();

    // Initialize display
    void begin();

    // Main update loop
    void update();

    // Screen management
    void setScreen(Screen screen);
    Screen getCurrentScreen() const { return _currentScreen; }

    // Force redraw
    void invalidate() { _needsRedraw = true; }

    // Notification display
    void showNotification(const String& message, uint16_t durationMs = 2000);

    // MIDI activity indicator
    void flashMidiActivity(uint8_t routingId);

    // Get/set selected routing for editing
    void setSelectedRouting(MidiRouting* routing) { _editingRouting = routing; }
    MidiRouting* getSelectedRouting() { return _editingRouting; }

    // Get currently editing field
    EditField getEditField() const { return _editField; }
    void setEditField(EditField field) { _editField = field; }

    // Device selection mode
    void startDeviceSelection(bool forSource);
    bool isSelectingSource() const { return _selectingSource; }

private:
    Screen _currentScreen;
    Screen _previousScreen;
    bool _needsRedraw;
    int _selectedIndex;
    int _scrollOffset;
    uint32_t _lastInputTime;
    bool _selectingSource;  // true = selecting source device, false = destination

    // Editing state
    MidiRouting* _editingRouting;
    EditField _editField;
    ChannelFilter _tempChannelFilter;

    // Notification
    String _notificationText;
    uint32_t _notificationEndTime;

    // MIDI activity flash
    uint8_t _flashRoutingId;
    uint32_t _flashEndTime;

    // Drawing methods
    void draw();
    void drawHeader();
    void drawFooter(const String& hint);
    void drawRoutingList();
    void drawRoutingEdit();
    void drawDeviceSelect();
    void drawChannelSelect();
    void drawSettings();
    void drawConfirmDelete();
    void drawNotification();

    // Draw a single routing line
    void drawRoutingLine(int y, const MidiRouting& routing, bool selected, bool flash);

    // Input handling
    void handleInput();
    void handleRoutingListInput(char key);
    void handleRoutingEditInput(char key);
    void handleDeviceSelectInput(char key);
    void handleChannelSelectInput(char key);
    void handleSettingsInput(char key);
    void handleConfirmDeleteInput(char key);

    // Helper to get key from keyboard
    char getKeyPress();

    // Get device display name (truncated if needed)
    String getDeviceDisplayName(const String& deviceId, int maxLen);

    // Scrolling helpers
    void ensureVisible(int index, int itemCount);
};

// Global UI manager instance
extern UIManager uiManager;

#endif // UI_H
