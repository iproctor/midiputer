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
    display.setTextColor(Colors::TEXT, Colors::BACKGROUND);

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

    // Draw notification overlay if active
    if (_notificationEndTime > 0) {
        if (millis() < _notificationEndTime) {
            drawNotification();
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
    display.fillScreen(Colors::BACKGROUND);
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
}

void UIManager::drawHeader() {
    display.fillRect(0, 0, SCREEN_WIDTH, HEADER_HEIGHT, Colors::HEADER_BG);
    display.setTextColor(Colors::TEXT, Colors::HEADER_BG);

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

    display.setCursor(4, 4);
    display.print(title);

    // Show device count on main screen
    if (_currentScreen == Screen::ROUTING_LIST) {
        String devInfo = String(deviceManager.getDeviceCount()) + " dev";
        int16_t x = SCREEN_WIDTH - (devInfo.length() * 6) - 4;
        display.setCursor(x, 4);
        display.print(devInfo);
    }

    display.setTextColor(Colors::TEXT, Colors::BACKGROUND);
}

void UIManager::drawFooter(const String& hint) {
    int16_t y = SCREEN_HEIGHT - FOOTER_HEIGHT;
    display.fillRect(0, y, SCREEN_WIDTH, FOOTER_HEIGHT, Colors::HEADER_BG);
    display.setTextColor(Colors::TEXT_DIM, Colors::HEADER_BG);
    display.setCursor(4, y + 3);
    display.print(hint);
    display.setTextColor(Colors::TEXT, Colors::BACKGROUND);
}

void UIManager::drawRoutingList() {
    int16_t y = HEADER_HEIGHT + 2;
    auto& routings = routingManager.getRoutings();

    if (routings.empty()) {
        display.setTextColor(Colors::TEXT_DIM, Colors::BACKGROUND);
        display.setCursor(10, y + 20);
        display.print("No routings configured");
        display.setCursor(10, y + 36);
        display.print("Press 'N' to add new");
        display.setTextColor(Colors::TEXT, Colors::BACKGROUND);
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
        display.setTextColor(Colors::HIGHLIGHT, Colors::BACKGROUND);
        display.setCursor(SCREEN_WIDTH - 12, HEADER_HEIGHT + 2);
        display.print("^");
    }
    if (_scrollOffset + MAX_VISIBLE_ITEMS < (int)routings.size()) {
        display.setTextColor(Colors::HIGHLIGHT, Colors::BACKGROUND);
        display.setCursor(SCREEN_WIDTH - 12, SCREEN_HEIGHT - FOOTER_HEIGHT - LINE_HEIGHT);
        display.print("v");
    }
    display.setTextColor(Colors::TEXT, Colors::BACKGROUND);
}

void UIManager::drawRoutingLine(int y, const MidiRouting& routing, bool selected, bool flash) {
    // Background
    uint16_t bgColor = Colors::BACKGROUND;
    if (flash) {
        bgColor = Colors::MIDI_FLASH;
    } else if (selected) {
        bgColor = Colors::HIGHLIGHT_BG;
    }
    display.fillRect(0, y, SCREEN_WIDTH, LINE_HEIGHT, bgColor);

    // Status indicator
    bool active = routing.isActive();
    uint16_t statusColor = active ? Colors::ACTIVE : Colors::TEXT_DISABLED;

    // If routing is disabled, show different color
    if (!routing.isEnabled()) {
        statusColor = Colors::INACTIVE;
    }

    // Draw status dot
    display.fillCircle(8, y + LINE_HEIGHT / 2, 3, statusColor);

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
    display.setTextColor(srcColor, bgColor);
    display.setCursor(16, y + 3);
    display.print(srcName);

    // Draw arrow
    display.setTextColor(Colors::TEXT_DIM, bgColor);
    display.setCursor(80, y + 3);
    display.print("->");

    // Draw destination name
    display.setTextColor(dstColor, bgColor);
    display.setCursor(96, y + 3);
    display.print(dstName);

    // Draw channel filter info
    display.setTextColor(Colors::TEXT_DIM, bgColor);
    display.setCursor(168, y + 3);
    display.print(routing.getChannelFilter().toString());

    // Draw last message if recent (within 2 seconds)
    const MidiMessage& lastMsg = routing.getLastMessage();
    if (lastMsg.timestamp > 0 && (millis() - lastMsg.timestamp) < 2000) {
        display.setTextColor(Colors::MIDI_FLASH, bgColor);
        display.setCursor(200, y + 3);
        // Show abbreviated message type
        switch (lastMsg.getType()) {
            case MidiMessageType::NOTE_ON:  display.print("N"); break;
            case MidiMessageType::NOTE_OFF: display.print("n"); break;
            case MidiMessageType::CONTROL_CHANGE: display.print("C"); break;
            case MidiMessageType::PROGRAM_CHANGE: display.print("P"); break;
            case MidiMessageType::PITCH_BEND: display.print("B"); break;
            default: display.print("*"); break;
        }
    }

    display.setTextColor(Colors::TEXT, Colors::BACKGROUND);
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
    display.setTextColor(srcSelected ? Colors::HIGHLIGHT : Colors::TEXT_DIM, Colors::BACKGROUND);
    display.setCursor(labelX, y);
    display.print("Source:");

    String srcName = getDeviceDisplayName(_editingRouting->getSourceDeviceId(), 18);
    display.setTextColor(srcSelected ? Colors::HIGHLIGHT : Colors::TEXT, Colors::BACKGROUND);
    display.setCursor(valueX, y);
    display.print(srcName);
    y += rowHeight;

    // Destination device
    bool dstSelected = (_editField == EditField::DESTINATION);
    display.setTextColor(dstSelected ? Colors::HIGHLIGHT : Colors::TEXT_DIM, Colors::BACKGROUND);
    display.setCursor(labelX, y);
    display.print("Dest:");

    String dstName = getDeviceDisplayName(_editingRouting->getDestDeviceId(), 18);
    display.setTextColor(dstSelected ? Colors::HIGHLIGHT : Colors::TEXT, Colors::BACKGROUND);
    display.setCursor(valueX, y);
    display.print(dstName);
    y += rowHeight;

    // Channel filter
    bool chSelected = (_editField == EditField::CHANNELS);
    display.setTextColor(chSelected ? Colors::HIGHLIGHT : Colors::TEXT_DIM, Colors::BACKGROUND);
    display.setCursor(labelX, y);
    display.print("Channels:");

    display.setTextColor(chSelected ? Colors::HIGHLIGHT : Colors::TEXT, Colors::BACKGROUND);
    display.setCursor(valueX, y);
    display.print(_editingRouting->getChannelFilter().toString());
    y += rowHeight;

    // Enabled toggle
    bool enSelected = (_editField == EditField::ENABLED);
    display.setTextColor(enSelected ? Colors::HIGHLIGHT : Colors::TEXT_DIM, Colors::BACKGROUND);
    display.setCursor(labelX, y);
    display.print("Enabled:");

    uint16_t enColor = _editingRouting->isEnabled() ? Colors::ACTIVE : Colors::INACTIVE;
    display.setTextColor(enSelected ? Colors::HIGHLIGHT : enColor, Colors::BACKGROUND);
    display.setCursor(valueX, y);
    display.print(_editingRouting->isEnabled() ? "Yes" : "No");

    display.setTextColor(Colors::TEXT, Colors::BACKGROUND);
}

void UIManager::drawDeviceSelect() {
    int16_t y = HEADER_HEIGHT + 2;
    auto& devices = deviceManager.getDevices();

    if (devices.empty()) {
        display.setTextColor(Colors::TEXT_DIM, Colors::BACKGROUND);
        display.setCursor(10, y + 20);
        display.print("No MIDI devices connected");
        display.setCursor(10, y + 36);
        display.print("Connect devices via USB hub");
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
        display.fillRect(0, y, SCREEN_WIDTH, LINE_HEIGHT, bgColor);

        // Connection status indicator
        uint16_t statusColor = device.isConnected() ? Colors::ACTIVE : Colors::INACTIVE;
        display.fillCircle(8, y + LINE_HEIGHT / 2, 3, statusColor);

        // Device name
        uint16_t textColor = device.isConnected() ? Colors::TEXT : Colors::TEXT_DISABLED;
        display.setTextColor(textColor, bgColor);
        display.setCursor(16, y + 3);
        display.print(device.getName().substring(0, 28));

        y += LINE_HEIGHT;
    }

    display.setTextColor(Colors::TEXT, Colors::BACKGROUND);
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

            display.fillRect(x - 2, cy - 1, colWidth - 4, rowHeight - 2, bgColor);

            if (selected) {
                display.drawRect(x - 2, cy - 1, colWidth - 4, rowHeight - 2, Colors::HIGHLIGHT);
            }

            display.setTextColor(textColor, bgColor);
            display.setCursor(x + 4, cy + 2);

            char buf[8];
            snprintf(buf, sizeof(buf), "Ch%02d", ch);
            display.print(buf);
        }
    }

    // Show current selection summary
    y = SCREEN_HEIGHT - FOOTER_HEIGHT - 20;
    display.setTextColor(Colors::TEXT_DIM, Colors::BACKGROUND);
    display.setCursor(10, y);
    display.print("Selected: ");
    display.setTextColor(Colors::TEXT, Colors::BACKGROUND);
    display.print(filter.toString());
}

void UIManager::drawSettings() {
    int16_t y = HEADER_HEIGHT + 6;

    display.setCursor(4, y);
    display.print("Routings: ");
    display.print(routingManager.getRoutingCount());
    y += 13;

    display.setCursor(4, y);
    display.print("MIDI devs: ");
    display.print(deviceManager.getDeviceCount());
    y += 13;

    display.setCursor(4, y);
    display.print("Msgs routed: ");
    display.print(routingManager.getTotalMessagesRouted());
    y += 13;

    // USB event log - shows all USB devices seen (including non-MIDI and hub)
    display.setTextColor(Colors::TEXT_DIM, Colors::BACKGROUND);
    display.setCursor(4, y);
    display.print("-- USB log --");
    y += 11;

    const auto& usbLog = deviceManager.getUsbLog();
    if (usbLog.empty()) {
        display.setCursor(4, y);
        display.print("(no USB events)");
    } else {
        for (const auto& entry : usbLog) {
            if (y >= SCREEN_HEIGHT - FOOTER_HEIGHT - 2) break;
            display.setCursor(4, y);
            display.print(entry.substring(0, 38));
            y += 11;
        }
    }

    display.setTextColor(Colors::TEXT, Colors::BACKGROUND);
}

void UIManager::drawConfirmDelete() {
    int16_t centerY = SCREEN_HEIGHT / 2;

    display.setTextColor(Colors::WARNING, Colors::BACKGROUND);
    display.setCursor(40, centerY - 16);
    display.print("Delete this routing?");

    if (_editingRouting) {
        display.setTextColor(Colors::TEXT_DIM, Colors::BACKGROUND);
        display.setCursor(40, centerY);

        String srcName = getDeviceDisplayName(_editingRouting->getSourceDeviceId(), 8);
        String dstName = getDeviceDisplayName(_editingRouting->getDestDeviceId(), 8);
        display.print(srcName + " -> " + dstName);
    }

    display.setTextColor(Colors::TEXT, Colors::BACKGROUND);
}

void UIManager::drawNotification() {
    const int16_t padding = 8;
    const int16_t height = 24;
    int16_t y = (SCREEN_HEIGHT - height) / 2;

    int16_t width = _notificationText.length() * 6 + padding * 2;
    int16_t x = (SCREEN_WIDTH - width) / 2;

    display.fillRoundRect(x - 2, y - 2, width + 4, height + 4, 4, Colors::BORDER);
    display.fillRoundRect(x, y, width, height, 4, Colors::HEADER_BG);

    display.setTextColor(Colors::TEXT, Colors::HEADER_BG);
    display.setCursor(x + padding, y + 6);
    display.print(_notificationText);

    display.setTextColor(Colors::TEXT, Colors::BACKGROUND);
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
