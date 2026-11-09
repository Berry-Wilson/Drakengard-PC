#include "ControlsTab.h"

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QFileInfo>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QVBoxLayout>

#include <cmath>

using namespace ps2x::input;

namespace
{
    constexpr int kPollMs = 16;
    constexpr int kCaptureTimeoutMs = 8000;
    const QString kWaitingKey = "Press a key...";
    const QString kWaitingButton = "Press a button...";

    // GLFW decides keys by scancode on Windows (win32_init.c, createKeyTables), so the same table
    // here makes a captured key exactly what the game will see. The 0x100 bit marks extended keys,
    // as in Qt's nativeScanCode().
    int glfwKeyFromScancode(quint32 scancode)
    {
        switch (scancode)
        {
        case 0x00B: return '0';
        case 0x002: return '1';
        case 0x003: return '2';
        case 0x004: return '3';
        case 0x005: return '4';
        case 0x006: return '5';
        case 0x007: return '6';
        case 0x008: return '7';
        case 0x009: return '8';
        case 0x00A: return '9';
        case 0x01E: return 'A';
        case 0x030: return 'B';
        case 0x02E: return 'C';
        case 0x020: return 'D';
        case 0x012: return 'E';
        case 0x021: return 'F';
        case 0x022: return 'G';
        case 0x023: return 'H';
        case 0x017: return 'I';
        case 0x024: return 'J';
        case 0x025: return 'K';
        case 0x026: return 'L';
        case 0x032: return 'M';
        case 0x031: return 'N';
        case 0x018: return 'O';
        case 0x019: return 'P';
        case 0x010: return 'Q';
        case 0x013: return 'R';
        case 0x01F: return 'S';
        case 0x014: return 'T';
        case 0x016: return 'U';
        case 0x02F: return 'V';
        case 0x011: return 'W';
        case 0x02D: return 'X';
        case 0x015: return 'Y';
        case 0x02C: return 'Z';
        case 0x028: return 39;  // Apostrophe
        case 0x02B: return 92;  // Backslash
        case 0x033: return 44;  // Comma
        case 0x00D: return 61;  // Equal
        case 0x029: return 96;  // Grave accent
        case 0x01A: return 91;  // Left bracket
        case 0x00C: return 45;  // Minus
        case 0x034: return 46;  // Period
        case 0x01B: return 93;  // Right bracket
        case 0x027: return 59;  // Semicolon
        case 0x035: return 47;  // Slash
        case 0x00E: return 259; // Backspace
        case 0x153: return 261; // Delete
        case 0x14F: return 269; // End
        case 0x01C: return 257; // Enter
        case 0x001: return 256; // Escape
        case 0x147: return 268; // Home
        case 0x152: return 260; // Insert
        case 0x15D: return 348; // Menu
        case 0x151: return 267; // Page down
        case 0x149: return 266; // Page up
        case 0x045: return 284; // Pause
        case 0x039: return 32;  // Space
        case 0x00F: return 258; // Tab
        case 0x03A: return 280; // Caps lock
        case 0x145: return 282; // Num lock
        case 0x046: return 281; // Scroll lock
        case 0x03B: return 290; // F1
        case 0x03C: return 291;
        case 0x03D: return 292;
        case 0x03E: return 293;
        case 0x03F: return 294;
        case 0x040: return 295;
        case 0x041: return 296;
        case 0x042: return 297;
        case 0x043: return 298;
        case 0x044: return 299; // F10
        case 0x057: return 300; // F11
        case 0x058: return 301; // F12
        case 0x038: return 342; // Left alt
        case 0x01D: return 341; // Left control
        case 0x02A: return 340; // Left shift
        case 0x15B: return 343; // Left super
        case 0x137: return 283; // Print screen
        case 0x138: return 346; // Right alt
        case 0x11D: return 345; // Right control
        case 0x036: return 344; // Right shift
        case 0x15C: return 347; // Right super
        case 0x150: return 264; // Down
        case 0x14B: return 263; // Left
        case 0x14D: return 262; // Right
        case 0x148: return 265; // Up
        case 0x052: return 320; // Keypad 0
        case 0x04F: return 321;
        case 0x050: return 322;
        case 0x051: return 323;
        case 0x04B: return 324;
        case 0x04C: return 325;
        case 0x04D: return 326;
        case 0x047: return 327;
        case 0x048: return 328;
        case 0x049: return 329; // Keypad 9
        case 0x04E: return 334; // Keypad add
        case 0x053: return 330; // Keypad decimal
        case 0x135: return 331; // Keypad divide
        case 0x11C: return 335; // Keypad enter
        case 0x059: return 336; // Keypad equal
        case 0x037: return 332; // Keypad multiply
        case 0x04A: return 333; // Keypad subtract
        default: return 0;
        }
    }

    // Used where there is no native scancode (not Windows): the keys that need no left/right or
    // keypad distinction.
    int glfwKeyFromQt(const QKeyEvent *event)
    {
        const int key = event->key();
        if ((key >= Qt::Key_A && key <= Qt::Key_Z) || (key >= Qt::Key_0 && key <= Qt::Key_9))
            return key; // Qt and GLFW both use ASCII here
        if (key >= Qt::Key_F1 && key <= Qt::Key_F25)
            return 290 + (key - Qt::Key_F1);
        switch (key)
        {
        case Qt::Key_Space: return 32;
        case Qt::Key_Return: return 257;
        case Qt::Key_Enter: return 335;
        case Qt::Key_Tab: return 258;
        case Qt::Key_Backspace: return 259;
        case Qt::Key_Up: return 265;
        case Qt::Key_Down: return 264;
        case Qt::Key_Left: return 263;
        case Qt::Key_Right: return 262;
        case Qt::Key_Shift: return 340;
        case Qt::Key_Control: return 341;
        case Qt::Key_Alt: return 342;
        default: return 0;
        }
    }

    const char *kStickNames[2] = {"Left stick", "Right stick"};
}

ControlsTab::ControlsTab(const QString &filePath, QWidget *parent)
    : QWidget(parent), m_path(filePath), m_bindings(Bindings::defaults())
{
    if (QFileInfo::exists(m_path))
        loadFile(QDir::toNativeSeparators(m_path).toStdString(), m_bindings);

    auto *grid = new QGridLayout;
    grid->setHorizontalSpacing(8);
    const char *headers[] = {"PS2 pad", "Key 1", "Key 2", "Controller 1", "Controller 2"};
    for (int c = 0; c < 5; ++c)
    {
        auto *header = new QLabel(QString("<b>%1</b>").arg(headers[c]));
        grid->addWidget(header, 0, c);
    }

    int row = 1;
    for (int action = 0; action < kActionCount; ++action)
    {
        if (action == kButtonActionCount)
        {
            auto *note = new QLabel("<b>Stick directions</b> (keys, or controller inputs, that push a PS2 stick fully)");
            note->setContentsMargins(0, 10, 0, 2);
            grid->addWidget(note, row++, 0, 1, 5);
        }
        grid->addWidget(new QLabel(actionLabel(static_cast<Action>(action))), row, 0);
        for (int column = 0; column < kKeyboardSlots + kControllerSlots; ++column)
        {
            const bool keyboard = column < kKeyboardSlots;
            auto *button = new QPushButton;
            button->setMinimumWidth(140);
            button->setContextMenuPolicy(Qt::CustomContextMenu);
            button->setToolTip(keyboard ? "Click, then press a key. Right-click to clear."
                                        : "Click, then press a controller button or move a stick or trigger. "
                                          "Right-click to clear.");
            const int slotIndex = static_cast<int>(m_slots.size());
            m_slots.push_back({button, action, keyboard, keyboard ? column : column - kKeyboardSlots});
            connect(button, &QPushButton::clicked, this, [this, slotIndex] {
                if (m_capturing == slotIndex)
                    stopCapture();
                else
                    startCapture(slotIndex);
            });
            connect(button, &QPushButton::customContextMenuRequested, this, [this, slotIndex] { clearSlot(slotIndex); });
            grid->addWidget(button, row, column + 1);
        }
        ++row;
    }

    auto *table = new QWidget;
    table->setLayout(grid);
    auto *scroll = new QScrollArea;
    scroll->setWidget(table);
    scroll->setWidgetResizable(true);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->setMinimumWidth(table->sizeHint().width() + scroll->verticalScrollBar()->sizeHint().width() + 4);

    auto *sticks = new QGroupBox("Controller sticks");
    auto *sticksLayout = new QGridLayout(sticks);
    for (int s = 0; s < 2; ++s)
    {
        m_stickSource[s] = new QComboBox;
        m_stickSource[s]->addItem("Controller left stick", static_cast<int>(StickSettings::Source::LeftStick));
        m_stickSource[s]->addItem("Controller right stick", static_cast<int>(StickSettings::Source::RightStick));
        m_stickSource[s]->addItem("Nothing", static_cast<int>(StickSettings::Source::None));
        m_invertX[s] = new QCheckBox("Invert left/right");
        m_invertY[s] = new QCheckBox("Invert up/down");
        sticksLayout->addWidget(new QLabel(QString("PS2 %1 reads").arg(QString(kStickNames[s]).toLower())), s, 0);
        sticksLayout->addWidget(m_stickSource[s], s, 1);
        sticksLayout->addWidget(m_invertX[s], s, 2);
        sticksLayout->addWidget(m_invertY[s], s, 3);
        sticksLayout->setColumnStretch(4, 1);

        auto apply = [this, s] {
            StickSettings &settings = s == 0 ? m_bindings.leftStick : m_bindings.rightStick;
            settings.source = static_cast<StickSettings::Source>(m_stickSource[s]->currentData().toInt());
            settings.invertX = m_invertX[s]->isChecked();
            settings.invertY = m_invertY[s]->isChecked();
            save();
        };
        connect(m_stickSource[s], &QComboBox::activated, this, apply);
        connect(m_invertX[s], &QCheckBox::clicked, this, apply);
        connect(m_invertY[s], &QCheckBox::clicked, this, apply);
    }

    auto *defaults = new QPushButton("Restore defaults");
    connect(defaults, &QPushButton::clicked, this, [this] {
        stopCapture();
        m_bindings = Bindings::defaults();
        save();
        refresh();
        setStatus("Restored the default controls.");
    });
    auto *rescan = new QPushButton("Find controllers");
    rescan->setToolTip("Look again for controllers connected since the launcher started.");
    connect(rescan, &QPushButton::clicked, this, [this] {
        stopCapture();
        m_pads.rescan();
        refreshGamepadLabel();
    });

    m_padLabel = new QLabel;
    m_status = new QLabel;
    m_status->setWordWrap(true);

    auto *bottom = new QHBoxLayout;
    bottom->addWidget(m_padLabel, 1);
    bottom->addWidget(rescan);
    bottom->addWidget(defaults);

    auto *layout = new QVBoxLayout(this);
    layout->addWidget(scroll, 1);
    layout->addWidget(sticks);
    layout->addLayout(bottom);
    layout->addWidget(m_status);

    m_pollTimer.setInterval(kPollMs);
    connect(&m_pollTimer, &QTimer::timeout, this, &ControlsTab::pollGamepads);

    refresh();
    refreshGamepadLabel();
    setStatus(QFileInfo::exists(m_path)
                  ? QString("Controls file: %1").arg(QDir::toNativeSeparators(m_path))
                  : QString("Using the default controls. Changes are saved to %1").arg(QDir::toNativeSeparators(m_path)));
}

ControlsTab::~ControlsTab()
{
    stopCapture();
}

Binding &ControlsTab::bindingFor(const Slot &slot)
{
    return slot.keyboard ? m_bindings.keyboard[slot.action][slot.index] : m_bindings.controller[slot.action][slot.index];
}

void ControlsTab::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    if (m_pads.names().empty())
    {
        m_pads.rescan();
        refreshGamepadLabel();
    }
}

void ControlsTab::startCapture(int slotIndex)
{
    stopCapture();
    const Slot &slot = m_slots[slotIndex];
    if (!slot.keyboard)
    {
        if (m_pads.names().empty())
            m_pads.rescan();
        refreshGamepadLabel();
        if (m_pads.names().empty())
        {
            setStatus("No controller found. Connect one, then click the slot again.");
            return;
        }
        m_baseline = m_pads.read();
    }
    m_capturing = slotIndex;
    slot.button->setText(slot.keyboard ? kWaitingKey : kWaitingButton);
    slot.button->setDown(true);
    qApp->installEventFilter(this);
    m_captureClock.start();
    m_pollTimer.start();
    setStatus(slot.keyboard ? "Press the key for " + QString(actionLabel(static_cast<Action>(slot.action))) +
                                  ". Escape cancels."
                            : "Press the controller input for " + QString(actionLabel(static_cast<Action>(slot.action))) +
                                  ". Escape cancels.");
}

void ControlsTab::stopCapture()
{
    if (m_capturing < 0)
        return;
    qApp->removeEventFilter(this);
    m_pollTimer.stop();
    m_slots[m_capturing].button->setDown(false);
    m_capturing = -1;
    refresh();
}

void ControlsTab::finishCapture(const Binding &binding)
{
    const Slot slot = m_slots[m_capturing];
    stopCapture();
    bindingFor(slot) = binding;
    save();
    refresh();
    QString text = QString("%1 is now %2.").arg(actionLabel(static_cast<Action>(slot.action)),
                                               QString::fromStdString(bindingLabel(binding)));
    const QString conflict = conflictNote(binding, slot.action, slot.keyboard);
    if (!conflict.isEmpty())
        text += " " + conflict;
    setStatus(text);
}

void ControlsTab::clearSlot(int slotIndex)
{
    stopCapture();
    bindingFor(m_slots[slotIndex]) = Binding{};
    save();
    refresh();
    setStatus(QString("Cleared a binding for %1.").arg(actionLabel(static_cast<Action>(m_slots[slotIndex].action))));
}

bool ControlsTab::eventFilter(QObject *watched, QEvent *event)
{
    if (m_capturing >= 0 && (event->type() == QEvent::KeyPress || event->type() == QEvent::ShortcutOverride))
    {
        auto *key = static_cast<QKeyEvent *>(event);
        if (event->type() == QEvent::ShortcutOverride || key->isAutoRepeat())
        {
            event->accept();
            return true;
        }
        if (key->key() == Qt::Key_Escape)
        {
            stopCapture();
            setStatus("Cancelled.");
            return true;
        }
        if (!m_slots[m_capturing].keyboard)
            return true; // waiting for the controller; swallow other keys
        int glfwKey = key->nativeScanCode() ? glfwKeyFromScancode(key->nativeScanCode()) : 0;
        if (!glfwKey)
            glfwKey = glfwKeyFromQt(key);
        if (!glfwKey)
        {
            setStatus("That key can't be used. Try another, or press Escape to cancel.");
            return true;
        }
        if (isReservedKey(glfwKey))
        {
            setStatus(QString("%1 is kept for the game itself (F1 debug panel, F5 super-sampling, F12 screenshot). "
                              "Try another key.")
                          .arg(QString::fromStdString(bindingLabel(Binding::key(glfwKey)))));
            return true;
        }
        finishCapture(Binding::key(glfwKey));
        return true;
    }
    return QWidget::eventFilter(watched, event);
}

std::optional<Binding> ControlsTab::detectGamepadInput(const std::vector<GamepadState> &now) const
{
    for (size_t p = 0; p < now.size(); ++p)
    {
        const GamepadState &pad = now[p];
        const GamepadState rest = p < m_baseline.size() ? m_baseline[p] : GamepadState{};
        for (int b = 0; b < kGamepadButtonCount; ++b)
        {
            if (pad.buttons[b] && !rest.buttons[b])
                return Binding::button(static_cast<GamepadButton>(b));
        }
        for (int a = 0; a < kGamepadAxisCount; ++a)
        {
            const bool trigger = a == static_cast<int>(GamepadAxis::LeftTrigger) ||
                                 a == static_cast<int>(GamepadAxis::RightTrigger);
            if (trigger)
            {
                if (pad.axes[a] > 0.5f && rest.axes[a] < 0.0f)
                    return Binding::axis(static_cast<GamepadAxis>(a), 1);
            }
            else if (std::fabs(pad.axes[a]) > 0.7f && std::fabs(rest.axes[a]) < 0.3f)
            {
                return Binding::axis(static_cast<GamepadAxis>(a), pad.axes[a] < 0.0f ? -1 : 1);
            }
        }
    }
    return std::nullopt;
}

void ControlsTab::pollGamepads()
{
    if (m_capturing < 0)
        return;
    if (m_captureClock.elapsed() > kCaptureTimeoutMs)
    {
        stopCapture();
        setStatus("Nothing was pressed, so the binding is unchanged.");
        return;
    }
    if (m_slots[m_capturing].keyboard)
        return;
    const std::vector<GamepadState> now = m_pads.read();
    if (const auto binding = detectGamepadInput(now))
    {
        finishCapture(*binding);
        return;
    }
    // Inputs held when the capture began count once they are released and pressed again.
    for (size_t p = 0; p < now.size() && p < m_baseline.size(); ++p)
    {
        for (int b = 0; b < kGamepadButtonCount; ++b)
            m_baseline[p].buttons[b] = m_baseline[p].buttons[b] && now[p].buttons[b];
        for (int a = 0; a < kGamepadAxisCount; ++a)
        {
            const bool trigger = a >= static_cast<int>(GamepadAxis::LeftTrigger);
            if (trigger ? now[p].axes[a] < 0.0f : std::fabs(now[p].axes[a]) < 0.3f)
                m_baseline[p].axes[a] = now[p].axes[a];
        }
    }
}

QString ControlsTab::conflictNote(const Binding &binding, int action, bool keyboard) const
{
    QStringList others;
    for (int a = 0; a < kActionCount; ++a)
    {
        if (a == action)
            continue;
        const auto check = [&](const auto &bindings) {
            for (const Binding &b : bindings)
            {
                if (b == binding)
                {
                    others << actionLabel(static_cast<Action>(a));
                    return;
                }
            }
        };
        if (keyboard)
            check(m_bindings.keyboard[a]);
        else
            check(m_bindings.controller[a]);
    }
    if (others.isEmpty())
        return {};
    return QString("It is also bound to %1, and will press both.").arg(others.join(", "));
}

void ControlsTab::refresh()
{
    for (size_t i = 0; i < m_slots.size(); ++i)
    {
        if (static_cast<int>(i) == m_capturing)
            continue;
        const Binding &binding = bindingFor(m_slots[i]);
        m_slots[i].button->setText(binding.isNone() ? QStringLiteral("—") : QString::fromStdString(bindingLabel(binding)));
    }
    const StickSettings *settings[2] = {&m_bindings.leftStick, &m_bindings.rightStick};
    for (int s = 0; s < 2; ++s)
    {
        const int index = m_stickSource[s]->findData(static_cast<int>(settings[s]->source));
        m_stickSource[s]->setCurrentIndex(index);
        m_invertX[s]->setChecked(settings[s]->invertX);
        m_invertY[s]->setChecked(settings[s]->invertY);
    }
}

void ControlsTab::refreshGamepadLabel()
{
    const std::vector<std::string> names = m_pads.names();
    if (!m_pads.ready())
        m_padLabel->setText("Controllers can't be read on this system.");
    else if (names.empty())
        m_padLabel->setText("No controller connected.");
    else
    {
        QStringList list;
        for (const std::string &name : names)
            list << QString::fromStdString(name);
        m_padLabel->setText("Controllers: " + list.join(", "));
    }
}

void ControlsTab::save()
{
    QDir().mkpath(QFileInfo(m_path).absolutePath());
    if (!saveFile(QDir::toNativeSeparators(m_path).toStdString(), m_bindings))
        setStatus(QString("Could not save %1.").arg(QDir::toNativeSeparators(m_path)));
}

void ControlsTab::setStatus(const QString &text)
{
    m_status->setText(text);
}
