#include "UI.h"
#include "Routing.h"
#include "Storage.h"

// Global instance
UIManager uiManager;

//=============================================================================
// UIManager Implementation
//=============================================================================

UIManager::UIManager()
    : _currentScreen(Screen::ROUTING_LIST),
      _previousScreen(Screen::ROUTING_LIST),
      _needsRedraw(true),
      _selectedIndex(0),
      _scrollOffset(0),
      _lastInputTime(0),
      _selectingSource(true),
      _editingRouting(nullptr),
      _editField(EditField::SOURCE),
      _notificationEndTime(0),
      _flashRoutingId(0),
      _flashEndTime(0) {
}

UIManager::~UIManager() {
}

void UIManager::begin() {
    // Initialize display
    display.fillScreen(Colors::BACKGROUND);
    display.setTextSize(1);
    _sprite.setTextColor(Colors::TEXT, Colors::BACKGROUND);

    // Create sprite for double-buffered, flicker-free rendering
    _sprite.createSprite(SCREEN_WIDTH, SCREEN_HEIGHT);
    _sprite.setTextSize(1);
    _sprite.setTextColor(Colors::TEXT, Colors::BACKGROUND);

    _needsRedraw = true;
}

void UIManager::update() {
    // Handle input
    handleInput();

    // Clear flash if expired
    if (_flashEndTime > 0 && millis() > _flashEndTime) {
        _flashEndTime = 0;
        _flashRoutingId = 0;
        _needsRedraw = true;
    }

    // Draw if needed
    if (_needsRedraw) {
        draw();
        _needsRedraw = false;
    }

    // Draw notification overlay on top of sprite, then push
    if (_notificationEndTime > 0) {
        if (millis() < _notificationEndTime) {
            drawNotification();
            _sprite.pushSprite(&display, 0, 0);
        } else {
            _notificationEndTime = 0;
            _needsRedraw = true;
        }
    }
}

void UIManager::setScreen(Screen screen) {
    _previousScreen = _currentScreen;
    _currentScreen = screen;
    _selectedIndex = 0;
    _scrollOffset = 0;
    _needsRedraw = true;
}

void UIManager::showNotification(const String& message, uint16_t durationMs) {
    _notificationText = message;
    _notificationEndTime = millis() + durationMs;
    _needsRedraw = true;
}

void UIManager::flashMidiActivity(uint8_t routingId) {
    _flashRoutingId = routingId;
    _flashEndTime = millis() + 100;  // Flash for 100ms
    _needsRedraw = true;
}

void UIManager::startDeviceSelection(bool forSource) {
    _selectingSource = forSource;
    setScreen(Screen::DEVICE_SELECT);
}

//=============================================================================
// Drawing Methods
//=============================================================================

void UIManager::draw() {
    _sprite.fillScreen(Colors::BACKGROUND);
    drawHeader();

    switch (_currentScreen) {
        case Screen::ROUTING_LIST:
            drawRoutingList();
            drawFooter("N:New D:Del E:Edit");
            break;
        case Screen::ROUTING_EDIT:
            drawRoutingEdit();
            drawFooter("Enter:Select Esc:Back");
            break;
        case Screen::DEVICE_SELECT:
            drawDeviceSelect();
            drawFooter("Enter:Select Esc:Cancel");
            break;
        case Screen::CHANNEL_SELECT:
            drawChannelSelect();
            drawFooter("Space:Toggle A:All Esc:Done");
            break;
        case Screen::SETTINGS:
            drawSettings();
            drawFooter("Esc:Back");
            break;
        case Screen::CONFIRM_DELETE:
            drawConfirmDelete();
            drawFooter("Y:Delete N:Cancel");
            break;
    }

    // Push completed frame to display in one shot — no flicker
    _sprite.pushSprite(&display, 0, 0);
}

void UIManager::drawHeader() {
    _sprite.fillRect(0, 0, SCREEN_WIDTH, HEADER_HEIGHT, Colors::HEADER_BG);
    _sprite.setTextColor(Colors::TEXT, Colors::HEADER_BG);

    String title;
    switch (_currentScreen) {
        case Screen::ROUTING_LIST:
            title = "MIDI Router";
            break;
        case Screen::ROUTING_EDIT:
            title = "Edit Routing";
            break;
        case Screen::DEVICE_SELECT:
            title = _selectingSource ? "Select Source" : "Select Dest";
            break;
        case Screen::CHANNEL_SELECT:
            title = "Channel Filter";
            break;
        case Screen::SETTINGS:
            title = "Settings";
            break;
        case Screen::CONFIRM_DELETE:
            title = "Confirm Delete";
            break;
    }

    _sprite.setCursor(4, 4);
    _sprite.print(title);

    // Show device count on main screen
    if (_currentScreen == Screen::ROUTING_LIST) {
        String devInfo = String(deviceManager.getDeviceCount()) + " dev";
        int16_t x = SCREEN_WIDTH - (devInfo.length() * 6) - 4;
        _sprite.setCursor(x, 4);
        _sprite.print(devInfo);
    }

    _sprite.setTextColor(Colors::TEXT, Colors::BACKGROUND);
}

void UIManager::drawFooter(const String& hint) {
    int16_t y = SCREEN_HEIGHT - FOOTER_HEIGHT;
    _sprite.fillRect(0, y, SCREEN_WIDTH, FOOTER_HEIGHT, Colors::HEADER_BG);
    _sprite.setTextColor(Colors::TEXT_DIM, Colors::HEADER_BG);
    _sprite.setCursor(4, y + 3);
    _sprite.print(hint);
    _sprite.setTextColor(Colors::TEXT, Colors::BACKGROUND);
}

void UIManager::drawRoutingList() {
    int16_t y = HEADER_HEIGHT + 2;
    auto& routings = routingManager.getRoutings();

    if (routings.empty()) {
        _sprite.setTextColor(Colors::TEXT_DIM, Colors::BACKGROUND);
        _sprite.setCursor(10, y + 20);
        _sprite.print("No routings configured");
        _sprite.setCursor(10, y + 36);
        _sprite.print("Press 'N' to add new");
        _sprite.setTextColor(Colors::TEXT, Colors::BACKGROUND);
        return;
    }

    // Ensure selected index is valid
    if (_selectedIndex >= (int)routings.size()) {
        _selectedIndex = routings.size() - 1;
    }

    // Calculate scroll position
    ensureVisible(_selectedIndex, routings.size());

    // Draw visible routings
    for (size_t i = _scrollOffset; i < routings.size() && (int)(i - _scrollOffset) < MAX_VISIBLE_ITEMS; i++) {
        bool selected = ((int)i == _selectedIndex);
        bool flash = (_flashRoutingId == routings[i].getId() && _flashEndTime > 0);
        drawRoutingLine(y, routings[i], selected, flash);
        y += LINE_HEIGHT;
    }

    // Draw scroll indicators if needed
    if (_scrollOffset > 0) {
        _sprite.setTextColor(Colors::HIGHLIGHT, Colors::BACKGROUND);
        _sprite.setCursor(SCREEN_WIDTH - 12, HEADER_HEIGHT + 2);
        _sprite.print("^");
    }
    if (_scrollOffset + MAX_VISIBLE_ITEMS < (int)routings.size()) {
        _sprite.setTextColor(Colors::HIGHLIGHT, Colors::BACKGROUND);
        _sprite.setCursor(SCREEN_WIDTH - 12, SCREEN_HEIGHT - FOOTER_HEIGHT - LINE_HEIGHT);
        _sprite.print("v");
    }
    _sprite.setTextColor(Colors::TEXT, Colors::BACKGROUND);
}

void UIManager::drawRoutingLine(int y, const MidiRouting& routing, bool selected, bool flash) {
    // Background
    uint16_t bgColor = Colors::BACKGROUND;
    if (flash) {
        bgColor = Colors::MIDI_FLASH;
    } else if (selected) {
        bgColor = Colors::HIGHLIGHT_BG;
    }
    _sprite.fillRect(0, y, SCREEN_WIDTH, LINE_HEIGHT, bgColor);

    // Status indicator
    bool active = routing.isActive();
    uint16_t statusColor = active ? Colors::ACTIVE : Colors::TEXT_DISABLED;

    // If routing is disabled, show different color
    if (!routing.isEnabled()) {
        statusColor = Colors::INACTIVE;
    }

    // Draw status dot
    _sprite.fillCircle(8, y + LINE_HEIGHT / 2, 3, statusColor);

    // Get device names
    String srcName = getDeviceDisplayName(routing.getSourceDeviceId(), 10);
    String dstName = getDeviceDisplayName(routing.getDestDeviceId(), 10);

    // Determine text color based on device connection state
    uint16_t srcColor = Colors::TEXT;
    uint16_t dstColor = Colors::TEXT;

    MidiDevice* srcDev = deviceManager.findDevice(routing.getSourceDeviceId());
    MidiDevice* dstDev = deviceManager.findDevice(routing.getDestDeviceId());

    if (!srcDev || !srcDev->isConnected()) {
        srcColor = Colors::TEXT_DISABLED;
    }
    if (!dstDev || !dstDev->isConnected()) {
        dstColor = Colors::TEXT_DISABLED;
    }

    // Draw source name
    _sprite.setTextColor(srcColor, bgColor);
    _sprite.setCursor(16, y + 3);
    _sprite.print(srcName);

    // Draw arrow
    _sprite.setTextColor(Colors::TEXT_DIM, bgColor);
    _sprite.setCursor(80, y + 3);
    _sprite.print("->");

    // Draw destination name
    _sprite.setTextColor(dstColor, bgColor);
    _sprite.setCursor(96, y + 3);
    _sprite.print(dstName);

    // Draw channel filter info
    _sprite.setTextColor(Colors::TEXT_DIM, bgColor);
    _sprite.setCursor(168, y + 3);
    _sprite.print(routing.getChannelFilter().toString());

    // Draw last message if recent (within 2 seconds)
    const MidiMessage& lastMsg = routing.getLastMessage();
    if (lastMsg.timestamp > 0 && (millis() - lastMsg.timestamp) < 2000) {
        _sprite.setTextColor(Colors::MIDI_FLASH, bgColor);
        _sprite.setCursor(200, y + 3);
        // Show abbreviated message type
        switch (lastMsg.getType()) {
            case MidiMessageType::NOTE_ON:  _sprite.print("N"); break;
            case MidiMessageType::NOTE_OFF: _sprite.print("n"); break;
            case MidiMessageType::CONTROL_CHANGE: _sprite.print("C"); break;
            case MidiMessageType::PROGRAM_CHANGE: _sprite.print("P"); break;
            case MidiMessageType::PITCH_BEND: _sprite.print("B"); break;
            default: _sprite.print("*"); break;
        }
    }

    _sprite.setTextColor(Colors::TEXT, Colors::BACKGROUND);
}

void UIManager::drawRoutingEdit() {
    if (!_editingRouting) {
        setScreen(Screen::ROUTING_LIST);
        return;
    }

    int16_t y = HEADER_HEIGHT + 8;
    const int16_t labelX = 8;
    const int16_t valueX = 80;
    const int16_t rowHeight = 20;

    // Source device
    bool srcSelected = (_editField == EditField::SOURCE);
    _sprite.setTextColor(srcSelected ? Colors::HIGHLIGHT : Colors::TEXT_DIM, Colors::BACKGROUND);
    _sprite.setCursor(labelX, y);
    _sprite.print("Source:");

    String srcName = getDeviceDisplayName(_editingRouting->getSourceDeviceId(), 18);
    _sprite.setTextColor(srcSelected ? Colors::HIGHLIGHT : Colors::TEXT, Colors::BACKGROUND);
    _sprite.setCursor(valueX, y);
    _sprite.print(srcName);
    y += rowHeight;

    // Destination device
    bool dstSelected = (_editField == EditField::DESTINATION);
    _sprite.setTextColor(dstSelected ? Colors::HIGHLIGHT : Colors::TEXT_DIM, Colors::BACKGROUND);
    _sprite.setCursor(labelX, y);
    _sprite.print("Dest:");

    String dstName = getDeviceDisplayName(_editingRouting->getDestDeviceId(), 18);
    _sprite.setTextColor(dstSelected ? Colors::HIGHLIGHT : Colors::TEXT, Colors::BACKGROUND);
    _sprite.setCursor(valueX, y);
    _sprite.print(dstName);
    y += rowHeight;

    // Channel filter
    bool chSelected = (_editField == EditField::CHANNELS);
    _sprite.setTextColor(chSelected ? Colors::HIGHLIGHT : Colors::TEXT_DIM, Colors::BACKGROUND);
    _sprite.setCursor(labelX, y);
    _sprite.print("Channels:");

    _sprite.setTextColor(chSelected ? Colors::HIGHLIGHT : Colors::TEXT, Colors::BACKGROUND);
    _sprite.setCursor(valueX, y);
    _sprite.print(_editingRouting->getChannelFilter().toString());
    y += rowHeight;

    // Enabled toggle
    bool enSelected = (_editField == EditField::ENABLED);
    _sprite.setTextColor(enSelected ? Colors::HIGHLIGHT : Colors::TEXT_DIM, Colors::BACKGROUND);
    _sprite.setCursor(labelX, y);
    _sprite.print("Enabled:");

    uint16_t enColor = _editingRouting->isEnabled() ? Colors::ACTIVE : Colors::INACTIVE;
    _sprite.setTextColor(enSelected ? Colors::HIGHLIGHT : enColor, Colors::BACKGROUND);
    _sprite.setCursor(valueX, y);
    _sprite.print(_editingRouting->isEnabled() ? "Yes" : "No");

    _sprite.setTextColor(Colors::TEXT, Colors::BACKGROUND);
}

void UIManager::drawDeviceSelect() {
    int16_t y = HEADER_HEIGHT + 2;
    auto& devices = deviceManager.getDevices();

    if (devices.empty()) {
        _sprite.setTextColor(Colors::TEXT_DIM, Colors::BACKGROUND);
        _sprite.setCursor(10, y + 20);
        _sprite.print("No MIDI devices connected");
        _sprite.setCursor(10, y + 36);
        _sprite.print("Connect devices via USB hub");
        return;
    }

    // Ensure selected index is valid
    if (_selectedIndex >= (int)devices.size()) {
        _selectedIndex = devices.size() - 1;
    }

    ensureVisible(_selectedIndex, devices.size());

    // Draw devices
    for (size_t i = _scrollOffset; i < devices.size() && (int)(i - _scrollOffset) < MAX_VISIBLE_ITEMS; i++) {
        bool selected = ((int)i == _selectedIndex);
        const MidiDevice& device = devices[i];

        uint16_t bgColor = selected ? Colors::HIGHLIGHT_BG : Colors::BACKGROUND;
        _sprite.fillRect(0, y, SCREEN_WIDTH, LINE_HEIGHT, bgColor);

        // Connection status indicator
        uint16_t statusColor = device.isConnected() ? Colors::ACTIVE : Colors::INACTIVE;
        _sprite.fillCircle(8, y + LINE_HEIGHT / 2, 3, statusColor);

        // Device name
        uint16_t textColor = device.isConnected() ? Colors::TEXT : Colors::TEXT_DISABLED;
        _sprite.setTextColor(textColor, bgColor);
        _sprite.setCursor(16, y + 3);
        _sprite.print(device.getName().substring(0, 28));

        y += LINE_HEIGHT;
    }

    _sprite.setTextColor(Colors::TEXT, Colors::BACKGROUND);
}

void UIManager::drawChannelSelect() {
    int16_t y = HEADER_HEIGHT + 4;
    const int16_t colWidth = 40;
    const int16_t rowHeight = 16;
    const int16_t startX = 20;

    if (!_editingRouting) {
        setScreen(Screen::ROUTING_EDIT);
        return;
    }

    ChannelFilter& filter = _editingRouting->getChannelFilter();

    // Draw 4x4 grid of channels
    for (int row = 0; row < 4; row++) {
        for (int col = 0; col < 4; col++) {
            int ch = row * 4 + col + 1;
            int idx = row * 4 + col;
            bool selected = (idx == _selectedIndex);
            bool enabled = filter.isChannelEnabled(ch);

            int16_t x = startX + col * colWidth;
            int16_t cy = y + row * rowHeight;

            uint16_t bgColor = selected ? Colors::HIGHLIGHT_BG : Colors::BACKGROUND;
            uint16_t textColor = enabled ? Colors::ACTIVE : Colors::TEXT_DISABLED;

            _sprite.fillRect(x - 2, cy - 1, colWidth - 4, rowHeight - 2, bgColor);

            if (selected) {
                _sprite.drawRect(x - 2, cy - 1, colWidth - 4, rowHeight - 2, Colors::HIGHLIGHT);
            }

            _sprite.setTextColor(textColor, bgColor);
            _sprite.setCursor(x + 4, cy + 2);

            char buf[8];
            snprintf(buf, sizeof(buf), "Ch%02d", ch);
            _sprite.print(buf);
        }
    }

    // Show current selection summary
    y = SCREEN_HEIGHT - FOOTER_HEIGHT - 20;
    _sprite.setTextColor(Colors::TEXT_DIM, Colors::BACKGROUND);
    _sprite.setCursor(10, y);
    _sprite.print("Selected: ");
    _sprite.setTextColor(Colors::TEXT, Colors::BACKGROUND);
    _sprite.print(filter.toString());
}

void UIManager::drawSettings() {
    int16_t y = HEADER_HEIGHT + 6;

    _sprite.setCursor(4, y);
    _sprite.print("Routings: ");
    _sprite.print(routingManager.getRoutingCount());
    y += 13;

    _sprite.setCursor(4, y);
    _sprite.print("MIDI devs: ");
    _sprite.print(deviceManager.getDeviceCount());
    y += 13;

    _sprite.setCursor(4, y);
    _sprite.print("Msgs routed: ");
    _sprite.print(routingManager.getTotalMessagesRouted());
    y += 13;

    // USB event log - shows all USB devices seen (including non-MIDI and hub)
    _sprite.setTextColor(Colors::TEXT_DIM, Colors::BACKGROUND);
    _sprite.setCursor(4, y);
    _sprite.print("-- USB log --");
    y += 11;

    const auto& usbLog = deviceManager.getUsbLog();
    if (usbLog.empty()) {
        _sprite.setCursor(4, y);
        _sprite.print("(no USB events)");
    } else {
        for (const auto& entry : usbLog) {
            if (y >= SCREEN_HEIGHT - FOOTER_HEIGHT - 2) break;
            _sprite.setCursor(4, y);
            _sprite.print(entry.substring(0, 38));
            y += 11;
        }
    }

    _sprite.setTextColor(Colors::TEXT, Colors::BACKGROUND);
}

void UIManager::drawConfirmDelete() {
    int16_t centerY = SCREEN_HEIGHT / 2;

    _sprite.setTextColor(Colors::WARNING, Colors::BACKGROUND);
    _sprite.setCursor(40, centerY - 16);
    _sprite.print("Delete this routing?");

    if (_editingRouting) {
        _sprite.setTextColor(Colors::TEXT_DIM, Colors::BACKGROUND);
        _sprite.setCursor(40, centerY);

        String srcName = getDeviceDisplayName(_editingRouting->getSourceDeviceId(), 8);
        String dstName = getDeviceDisplayName(_editingRouting->getDestDeviceId(), 8);
        _sprite.print(srcName + " -> " + dstName);
    }

    _sprite.setTextColor(Colors::TEXT, Colors::BACKGROUND);
}

void UIManager::drawNotification() {
    const int16_t padding = 8;
    const int16_t height = 24;
    int16_t y = (SCREEN_HEIGHT - height) / 2;

    int16_t width = _notificationText.length() * 6 + padding * 2;
    int16_t x = (SCREEN_WIDTH - width) / 2;

    _sprite.fillRoundRect(x - 2, y - 2, width + 4, height + 4, 4, Colors::BORDER);
    _sprite.fillRoundRect(x, y, width, height, 4, Colors::HEADER_BG);

    _sprite.setTextColor(Colors::TEXT, Colors::HEADER_BG);
    _sprite.setCursor(x + padding, y + 6);
    _sprite.print(_notificationText);

    _sprite.setTextColor(Colors::TEXT, Colors::BACKGROUND);
}

//=============================================================================
// Input Handling
//=============================================================================

void UIManager::handleInput() {
    keyboard.update();

    if (!keyboard.isChange()) return;
    if (!keyboard.isPressed()) return;

    char key = getKeyPress();
    if (key == 0) return;

    _lastInputTime = millis();

    switch (_currentScreen) {
        case Screen::ROUTING_LIST:
            handleRoutingListInput(key);
            break;
        case Screen::ROUTING_EDIT:
            handleRoutingEditInput(key);
            break;
        case Screen::DEVICE_SELECT:
            handleDeviceSelectInput(key);
            break;
        case Screen::CHANNEL_SELECT:
            handleChannelSelectInput(key);
            break;
        case Screen::SETTINGS:
            handleSettingsInput(key);
            break;
        case Screen::CONFIRM_DELETE:
            handleConfirmDeleteInput(key);
            break;
    }
}

char UIManager::getKeyPress() {
    auto& state = keyboard.keysState();

    // Check for special keys
    if (state.del) return '\b';
    if (state.enter) return '\n';
    if (state.fn) return 0;  // Ignore fn by itself

    // Check word buffer for regular keys
    if (state.word.size() > 0) {
        return state.word[0];
    }

    return 0;
}

void UIManager::handleRoutingListInput(char key) {
    auto& routings = routingManager.getRoutings();

    switch (key) {
        case 'w':
        case 'W':
            // Up
            if (_selectedIndex > 0) {
                _selectedIndex--;
                _needsRedraw = true;
            }
            break;

        case 's':
        case 'S':
            // Down
            if (_selectedIndex < (int)routings.size() - 1) {
                _selectedIndex++;
                _needsRedraw = true;
            }
            break;

        case 'n':
        case 'N':
            // New routing
            if (deviceManager.getDeviceCount() >= 2) {
                // Create a new routing and go to edit screen
                MidiRouting* newRouting = routingManager.addRouting("", "");
                if (newRouting) {
                    _editingRouting = newRouting;
                    _editField = EditField::SOURCE;
                    startDeviceSelection(true);
                }
            } else {
                showNotification("Need 2+ devices", 2000);
            }
            break;

        case 'd':
        case 'D':
            // Delete
            if (!routings.empty() && _selectedIndex < (int)routings.size()) {
                _editingRouting = &routings[_selectedIndex];
                setScreen(Screen::CONFIRM_DELETE);
            }
            break;

        case 'e':
        case 'E':
        case '\n':
            // Edit
            if (!routings.empty() && _selectedIndex < (int)routings.size()) {
                _editingRouting = &routings[_selectedIndex];
                _editField = EditField::SOURCE;
                setScreen(Screen::ROUTING_EDIT);
            }
            break;

        case 'c':
        case 'C':
            // Settings (config)
            setScreen(Screen::SETTINGS);
            break;

        default:
            break;
    }
}

void UIManager::handleRoutingEditInput(char key) {
    if (!_editingRouting) {
        setScreen(Screen::ROUTING_LIST);
        return;
    }

    switch (key) {
        case 'w':
        case 'W':
            // Move up in field selection
            if (_editField == EditField::DESTINATION) _editField = EditField::SOURCE;
            else if (_editField == EditField::CHANNELS) _editField = EditField::DESTINATION;
            else if (_editField == EditField::ENABLED) _editField = EditField::CHANNELS;
            _needsRedraw = true;
            break;

        case 's':
        case 'S':
            // Move down in field selection
            if (_editField == EditField::SOURCE) _editField = EditField::DESTINATION;
            else if (_editField == EditField::DESTINATION) _editField = EditField::CHANNELS;
            else if (_editField == EditField::CHANNELS) _editField = EditField::ENABLED;
            _needsRedraw = true;
            break;

        case '\n':
            // Edit selected field
            switch (_editField) {
                case EditField::SOURCE:
                    startDeviceSelection(true);
                    break;
                case EditField::DESTINATION:
                    startDeviceSelection(false);
                    break;
                case EditField::CHANNELS:
                    _selectedIndex = 0;
                    setScreen(Screen::CHANNEL_SELECT);
                    break;
                case EditField::ENABLED:
                    _editingRouting->setEnabled(!_editingRouting->isEnabled());
                    routingManager.saveRoutings();
                    _needsRedraw = true;
                    break;
            }
            break;

        case '\b':  // Backspace/Escape
        case 'q':
        case 'Q':
            // Back to list
            _editingRouting = nullptr;
            setScreen(Screen::ROUTING_LIST);
            break;

        default:
            break;
    }
}

void UIManager::handleDeviceSelectInput(char key) {
    auto& devices = deviceManager.getDevices();

    switch (key) {
        case 'w':
        case 'W':
            if (_selectedIndex > 0) {
                _selectedIndex--;
                _needsRedraw = true;
            }
            break;

        case 's':
        case 'S':
            if (_selectedIndex < (int)devices.size() - 1) {
                _selectedIndex++;
                _needsRedraw = true;
            }
            break;

        case '\n':
            // Select device
            if (_selectedIndex < (int)devices.size() && _editingRouting) {
                String uniqueId = devices[_selectedIndex].getUniqueId();
                if (_selectingSource) {
                    _editingRouting->setSourceDeviceId(uniqueId);
                } else {
                    _editingRouting->setDestDeviceId(uniqueId);
                }
                routingManager.saveRoutings();

                // Go back to edit screen
                _selectedIndex = 0;
                setScreen(Screen::ROUTING_EDIT);
            }
            break;

        case '\b':
        case 'q':
        case 'Q':
            // Cancel - go back
            _selectedIndex = 0;
            setScreen(Screen::ROUTING_EDIT);
            break;

        default:
            break;
    }
}

void UIManager::handleChannelSelectInput(char key) {
    if (!_editingRouting) {
        setScreen(Screen::ROUTING_EDIT);
        return;
    }

    ChannelFilter& filter = _editingRouting->getChannelFilter();

    switch (key) {
        case 'w':
        case 'W':
            // Up (move up one row)
            if (_selectedIndex >= 4) {
                _selectedIndex -= 4;
                _needsRedraw = true;
            }
            break;

        case 's':
        case 'S':
            // Down (move down one row)
            if (_selectedIndex < 12) {
                _selectedIndex += 4;
                _needsRedraw = true;
            }
            break;

        case 'a':
        case 'A':
            // Toggle all channels
            if (filter.isAllChannels()) {
                filter.setNone();
            } else {
                filter.setAll();
            }
            routingManager.saveRoutings();
            _needsRedraw = true;
            break;

        case 'd':
        case 'D':
            // Right
            if (_selectedIndex < 15) {
                _selectedIndex++;
                _needsRedraw = true;
            }
            break;

        case ' ':
        case '\n':
            // Toggle selected channel
            {
                int ch = _selectedIndex + 1;
                filter.setChannel(ch, !filter.isChannelEnabled(ch));
                routingManager.saveRoutings();
                _needsRedraw = true;
            }
            break;

        case '\b':
        case 'q':
        case 'Q':
            // Done - go back to edit
            _selectedIndex = 0;
            setScreen(Screen::ROUTING_EDIT);
            break;

        default:
            // Check for left movement
            if (key == ',' || key == '<') {
                if (_selectedIndex > 0) {
                    _selectedIndex--;
                    _needsRedraw = true;
                }
            }
            break;
    }
}

void UIManager::handleSettingsInput(char key) {
    switch (key) {
        case '\b':
        case 'q':
        case 'Q':
            setScreen(Screen::ROUTING_LIST);
            break;

        default:
            break;
    }
}

void UIManager::handleConfirmDeleteInput(char key) {
    switch (key) {
        case 'y':
        case 'Y':
            // Confirm delete
            if (_editingRouting) {
                routingManager.removeRouting(_editingRouting->getId());
                _editingRouting = nullptr;
                showNotification("Routing deleted", 1500);
            }
            setScreen(Screen::ROUTING_LIST);
            break;

        case 'n':
        case 'N':
        case '\b':
        case 'q':
        case 'Q':
            // Cancel
            _editingRouting = nullptr;
            setScreen(Screen::ROUTING_LIST);
            break;

        default:
            break;
    }
}

//=============================================================================
// Helper Methods
//=============================================================================

String UIManager::getDeviceDisplayName(const String& deviceId, int maxLen) {
    if (deviceId.isEmpty()) {
        return "<none>";
    }

    MidiDevice* device = deviceManager.findDevice(deviceId);
    String name;

    if (device) {
        name = device->getName();
    } else {
        // Device not connected - extract name from unique ID
        // Format is VID:PID:Name
        int lastColon = deviceId.lastIndexOf(':');
        if (lastColon > 0) {
            name = deviceId.substring(lastColon + 1);
        } else {
            name = deviceId;
        }
    }

    // Truncate if needed
    if ((int)name.length() > maxLen) {
        name = name.substring(0, maxLen - 2) + "..";
    }

    return name;
}

void UIManager::ensureVisible(int index, int itemCount) {
    if (itemCount <= MAX_VISIBLE_ITEMS) {
        _scrollOffset = 0;
        return;
    }

    if (index < _scrollOffset) {
        _scrollOffset = index;
    } else if (index >= _scrollOffset + MAX_VISIBLE_ITEMS) {
        _scrollOffset = index - MAX_VISIBLE_ITEMS + 1;
    }

    // Clamp scroll offset
    if (_scrollOffset < 0) _scrollOffset = 0;
    if (_scrollOffset > itemCount - MAX_VISIBLE_ITEMS) {
        _scrollOffset = itemCount - MAX_VISIBLE_ITEMS;
    }
}
