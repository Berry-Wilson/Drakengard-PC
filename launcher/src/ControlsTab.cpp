#include "ControlsTab.h"

#include "ControllerDiagram.h"

#include <QApplication>
#include <QDir>
#include <QFileInfo>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QMenu>
#include <QMessageBox>
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
    constexpr int kBindAllPauseMs = 350;
    constexpr int kStatusMs = 5000;

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

    // Each box's line color: the face buttons match the picture, the rest go by group.
    QColor lineColor(Action action)
    {
        switch (action)
        {
        case Action::Triangle: return QColor(0x22c55e);
        case Action::Square: return QColor(0xec4899);
        case Action::Circle: return QColor(0xef4444);
        case Action::Cross: return QColor(0x3b82f6);
        case Action::Up:
        case Action::Down:
        case Action::Left:
        case Action::Right: return QColor(0xf59e0b);
        case Action::L1:
        case Action::L2:
        case Action::R1:
        case Action::R2: return QColor(0xa78bfa);
        case Action::Start:
        case Action::Select: return QColor(0x2dd4bf);
        default: return QColor(0xd1d5db); // sticks and L3/R3
        }
    }
}

ControlsTab::ControlsTab(const QString &filePath, QWidget *parent)
    : QWidget(parent), m_path(filePath), m_bindings(Bindings::defaults())
{
    if (QFileInfo::exists(m_path))
        loadFile(QDir::toNativeSeparators(m_path).toStdString(), m_bindings);

    m_diagram = new ControllerDiagram;
    constexpr double W = 176, H = 56, Left = 10, Right = ControllerDiagram::kWidth - 10 - W;
    // Stick directions sit in a framed group per stick, under the picture.
    constexpr double SW = 150, GroupTop = 474, StickTop = GroupTop + 22, LeftStickX = 160, RightStickX = 512;
    const QPointF noLine(-1, -1);
    // Layout-space box, and the pixel in the controller picture its line points to.
    struct Place
    {
        Action action;
        const char *title;
        QRectF rect;
        QPointF anchor;
    };
    const Place places[] = {
        {Action::L2, "L2", {Left, 10, W, H}, {88, 9}},
        {Action::L1, "L1", {Left, 74, W, H}, {112, 11}},
        {Action::Up, "D-Pad Up", {Left, 150, W, H}, {99, 95}},
        {Action::Left, "D-Pad Left", {Left, 212, W, H}, {77, 116}},
        {Action::Right, "D-Pad Right", {Left, 274, W, H}, {121, 116}},
        {Action::Down, "D-Pad Down", {Left, 336, W, H}, {99, 138}},
        {Action::L3, "L3 (Press Left Stick)", {Left, 404, W, H}, {140, 205}},
        {Action::R2, "R2", {Right, 10, W, H}, {372, 9}},
        {Action::R1, "R1", {Right, 74, W, H}, {348, 11}},
        {Action::Triangle, "Triangle", {Right, 150, W, H}, {360, 84}},
        {Action::Square, "Square", {Right, 212, W, H}, {327, 116}},
        {Action::Circle, "Circle", {Right, 274, W, H}, {393, 116}},
        {Action::Cross, "Cross", {Right, 336, W, H}, {360, 149}},
        {Action::R3, "R3 (Press Right Stick)", {Right, 404, W, H}, {318, 205}},
        {Action::Select, "Select", {300, 10, W, H}, {196, 119}},
        {Action::Start, "Start", {504, 10, W, H}, {263, 120}},
        {Action::LeftStickUp, "Up", {LeftStickX + 8, StickTop, SW, H}, noLine},
        {Action::LeftStickDown, "Down", {LeftStickX + 8 + SW + 6, StickTop, SW, H}, noLine},
        {Action::LeftStickLeft, "Left", {LeftStickX + 8, StickTop + H + 6, SW, H}, noLine},
        {Action::LeftStickRight, "Right", {LeftStickX + 8 + SW + 6, StickTop + H + 6, SW, H}, noLine},
        {Action::RightStickUp, "Up", {RightStickX + 8, StickTop, SW, H}, noLine},
        {Action::RightStickDown, "Down", {RightStickX + 8 + SW + 6, StickTop, SW, H}, noLine},
        {Action::RightStickLeft, "Left", {RightStickX + 8, StickTop + H + 6, SW, H}, noLine},
        {Action::RightStickRight, "Right", {RightStickX + 8 + SW + 6, StickTop + H + 6, SW, H}, noLine},
    };
    const double groupWidth = 2 * SW + 6 + 16, groupHeight = ControllerDiagram::kHeight - GroupTop - 2;
    m_diagram->addGroup("Left Stick", {LeftStickX, GroupTop, groupWidth, groupHeight}, {163, 191}, lineColor(Action::LeftStickUp));
    m_diagram->addGroup("Right Stick", {RightStickX, GroupTop, groupWidth, groupHeight}, {295, 191}, lineColor(Action::RightStickUp));
    m_boxes.assign(kActionCount, nullptr);
    for (const Place &place : places)
    {
        const int action = static_cast<int>(place.action);
        auto *box = new BindingBox(place.title);
        // Stick boxes are titled by direction inside their group; messages name the stick too.
        const bool leftStick = place.action >= Action::LeftStickUp && place.action <= Action::LeftStickRight;
        const bool rightStick = place.action >= Action::RightStickUp;
        m_names[action] = (leftStick ? "Left Stick " : rightStick ? "Right Stick " : "") + QString(place.title);
        m_boxes[action] = box;
        m_diagram->addBox(box, place.rect, place.anchor, lineColor(place.action));
        connect(box, &BindingBox::clicked, this, [this, action] {
            endBindAll();
            if (m_capturing == action)
            {
                stopCapture();
                setStatus("Cancelled.");
            }
            else
                startCapture(action);
        });
        connect(box, &BindingBox::contextMenuRequested, this, [this, action](const QPoint &pos) {
            endBindAll();
            showClearMenu(action, pos);
        });
    }

    auto *scroll = new QScrollArea;
    scroll->setWidget(m_diagram);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);

    auto *defaults = new QPushButton("Restore defaults");
    connect(defaults, &QPushButton::clicked, this, [this] {
        endBindAll();
        stopCapture();
        m_bindings = Bindings::defaults();
        save();
        refresh();
        setStatus("Restored the default controls.");
    });
    auto *clearAll = new QPushButton("Unbind All");
    clearAll->setToolTip("Remove every key and controller binding.");
    // Dark red, since it throws away every binding; the confirmation's button matches.
    static const QString kDangerStyle = "QPushButton { background: #7f1d1d; color: white; border: 1px solid #991b1b;"
                                        " border-radius: 4px; padding: 3px 12px; }"
                                        "QPushButton:hover { background: #991b1b; border-color: #b91c1c; }"
                                        "QPushButton:pressed { background: #651515; }";
    clearAll->setStyleSheet(kDangerStyle);
    connect(clearAll, &QPushButton::clicked, this, [this] {
        endBindAll();
        stopCapture();
        QMessageBox confirm(QMessageBox::Warning, "Unbind All", "Are you sure?", QMessageBox::NoButton, this);
        QPushButton *unbind = confirm.addButton("Unbind All", QMessageBox::DestructiveRole);
        unbind->setStyleSheet(kDangerStyle);
        confirm.addButton(QMessageBox::Cancel);
        confirm.setDefaultButton(QMessageBox::Cancel);
        confirm.exec();
        if (confirm.clickedButton() != unbind)
            return;
        for (int action = 0; action < kActionCount; ++action)
        {
            m_bindings.keyboard[action] = {};
            m_bindings.controller[action] = {};
        }
        save();
        refresh();
        setStatus("Cleared every key and controller binding.");
    });
    auto *rescan = new QPushButton("Find controllers");
    rescan->setToolTip("Look again for controllers connected since the launcher started.");
    connect(rescan, &QPushButton::clicked, this, [this] {
        endBindAll();
        stopCapture();
        m_pads.rescan();
        refreshGamepadLabel();
    });

    m_bindAllButton = new QPushButton("Bind all");
    m_bindAllButton->setToolTip("Go through every PS2 button and stick direction in turn, binding each to the next key "
                                "or controller input you press.");
    connect(m_bindAllButton, &QPushButton::clicked, this, [this] {
        if (m_bindAll)
            endBindAll("Stopped binding all buttons.");
        else
            startBindAll();
    });
    m_skipButton = new QPushButton("Skip");
    m_skipButton->setToolTip("Leave this one as it is and go to the next.");
    m_skipButton->setVisible(false);
    connect(m_skipButton, &QPushButton::clicked, this, [this] {
        m_lastResult = QString("Skipped %1.").arg(m_names[m_bindAllNext]);
        ++m_bindAllNext;
        captureNext();
    });
    m_nextTimer.setSingleShot(true);
    connect(&m_nextTimer, &QTimer::timeout, this, &ControlsTab::captureNext);

    m_padLabel = new QLabel;
    m_status = new QLabel;
    m_status->setWordWrap(true);
    // Room for two lines, so Bind all's two-line messages don't move the buttons.
    m_status->setMinimumHeight(m_status->fontMetrics().lineSpacing() * 2 + 2);
    m_status->setAlignment(Qt::AlignLeft | Qt::AlignTop);

    auto *bottom = new QHBoxLayout;
    // Unbind All sits under Bind all.
    auto *bindButtons = new QVBoxLayout;
    bindButtons->addWidget(m_bindAllButton);
    bindButtons->addWidget(clearAll);
    bottom->addLayout(bindButtons);
    // Skip and the controller list line up with Bind all, on the top row.
    m_padLabel->setMinimumHeight(m_bindAllButton->sizeHint().height());
    bottom->addWidget(m_skipButton, 0, Qt::AlignTop);
    bottom->addWidget(m_padLabel, 1, Qt::AlignTop);
    // Restore defaults sits under Find controllers.
    auto *resetButtons = new QVBoxLayout;
    resetButtons->addWidget(rescan);
    resetButtons->addWidget(defaults);
    bottom->addLayout(resetButtons);

    auto *layout = new QVBoxLayout(this);
    layout->addWidget(scroll, 1);
    layout->addLayout(bottom);
    layout->addWidget(m_status);

    m_statusTimer.setSingleShot(true);
    connect(&m_statusTimer, &QTimer::timeout, this, [this] {
        if (m_capturing < 0)
            m_status->clear();
    });

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

void ControlsTab::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    if (m_pads.names().empty())
    {
        m_pads.rescan();
        refreshGamepadLabel();
    }
}

// A box listens for a key and for a controller input at the same time; whichever comes first is
// bound. Controllers are read only if one is connected.
void ControlsTab::startCapture(int action)
{
    stopCapture();
    if (m_pads.names().empty())
        m_pads.rescan();
    refreshGamepadLabel();
    m_baseline = m_pads.read();
    m_capturing = action;
    m_boxes[action]->setWaiting(true);
    qApp->installEventFilter(this);
    m_captureClock.start();
    m_pollTimer.start();
    const QString name = m_names[action];
    setStatus(m_pads.names().empty() ? "Press the key for " + name + ". No controller is connected. Escape cancels."
                                     : "Press a key or a controller input for " + name + ". Escape cancels.");
}

void ControlsTab::stopCapture()
{
    if (m_capturing < 0)
        return;
    qApp->removeEventFilter(this);
    m_pollTimer.stop();
    m_boxes[m_capturing]->setWaiting(false);
    m_capturing = -1;
    refresh();
}

// The new input replaces the box's keys (or controller inputs); the other kind is kept.
void ControlsTab::finishCapture(const Binding &binding, bool keyboard)
{
    const int action = m_capturing;
    stopCapture();
    auto &inputs = keyboard ? m_bindings.keyboard[action] : m_bindings.controller[action];
    for (Binding &slot : inputs)
        slot = Binding{};
    inputs[0] = binding;
    save();
    refresh();
    QString text = QString("%1 is now %2.").arg(m_names[action],
                                               QString::fromStdString(bindingLabel(binding)));
    const QString conflict = conflictNote(binding, action, keyboard);
    if (!conflict.isEmpty())
        text += " " + conflict;
    setStatus(text);

    if (m_bindAll)
    {
        // A short pause shows the new binding before the next box lights up.
        m_lastResult = text;
        ++m_bindAllNext;
        m_nextTimer.start(kBindAllPauseMs);
    }
}

void ControlsTab::startBindAll()
{
    m_bindAll = true;
    m_bindAllNext = 0;
    m_lastResult.clear();
    m_bindAllButton->setText("Stop");
    m_skipButton->setVisible(true);
    captureNext();
}

void ControlsTab::captureNext()
{
    if (!m_bindAll)
        return;
    if (m_bindAllNext >= kActionCount)
    {
        endBindAll(m_lastResult + (m_lastResult.isEmpty() ? "" : " ") + "Every button has been gone through.");
        return;
    }
    startCapture(m_bindAllNext);
    const QString prompt = QString("<b>Bind all (%1 of %2):</b> press a key or a controller input for <b>%3</b>. "
                                   "Skip leaves it as it is; Escape or Stop ends.")
                               .arg(m_bindAllNext + 1)
                               .arg(kActionCount)
                               .arg(m_names[m_bindAllNext].toHtmlEscaped());
    setStatus(m_lastResult.isEmpty() ? prompt : m_lastResult.toHtmlEscaped() + "<br>" + prompt);
}

void ControlsTab::endBindAll(const QString &message)
{
    if (!m_bindAll)
        return;
    m_bindAll = false;
    m_nextTimer.stop();
    m_bindAllButton->setText("Bind all");
    m_skipButton->setVisible(false);
    stopCapture();
    if (!message.isEmpty())
        setStatus(message);
}

void ControlsTab::clearBindings(int action, bool keys, bool controller)
{
    stopCapture();
    if (keys)
        m_bindings.keyboard[action] = {};
    if (controller)
        m_bindings.controller[action] = {};
    save();
    refresh();
    setStatus(QString("Cleared %1 for %2.")
                  .arg(keys && controller ? "the bindings" : keys ? "the keys" : "the controller inputs",
                       m_names[action]));
}

void ControlsTab::showClearMenu(int action, const QPoint &globalPos)
{
    stopCapture();
    QMenu menu(this);
    menu.addAction("Clear key", this, [this, action] { clearBindings(action, true, false); });
    menu.addAction("Clear controller input", this, [this, action] { clearBindings(action, false, true); });
    menu.addAction("Clear both", this, [this, action] { clearBindings(action, true, true); });
    menu.exec(globalPos);
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
            if (m_bindAll)
                endBindAll("Stopped binding all buttons.");
            else
            {
                stopCapture();
                setStatus("Cancelled.");
            }
            return true;
        }
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
            setStatus(QString("%1 is kept for the game itself (F1 statistics, F3 speed counter, F5 internal resolution, "
                              "F11 full screen, F12 screenshot). Try another key.")
                          .arg(QString::fromStdString(bindingLabel(Binding::key(glfwKey)))));
            return true;
        }
        finishCapture(Binding::key(glfwKey), true);
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
    if (!m_bindAll && m_captureClock.elapsed() > kCaptureTimeoutMs)
    {
        stopCapture();
        setStatus("Nothing was pressed, so the binding is unchanged.");
        return;
    }
    const std::vector<GamepadState> now = m_pads.read();
    if (const auto binding = detectGamepadInput(now))
    {
        finishCapture(*binding, false);
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
                    others << m_names[a];
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
    const auto describe = [](const auto &inputs) {
        QStringList names;
        for (const Binding &binding : inputs)
        {
            if (!binding.isNone())
                names << QString::fromStdString(bindingLabel(binding));
        }
        return names.isEmpty() ? QStringLiteral("—") : names.join(" / ");
    };
    for (int action = 0; action < kActionCount; ++action)
    {
        if (action != m_capturing && m_boxes[action])
            m_boxes[action]->setBindings(describe(m_bindings.keyboard[action]), describe(m_bindings.controller[action]));
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

// Messages clear themselves after a few seconds. A prompt waiting for input stays.
void ControlsTab::setStatus(const QString &text)
{
    m_status->setText(text);
    m_statusTimer.start(kStatusMs);
}
