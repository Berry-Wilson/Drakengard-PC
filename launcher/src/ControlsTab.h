#pragma once

#include "GamepadReader.h"
#include "runtime/input_bindings.h"

#include <QElapsedTimer>
#include <QString>
#include <QTimer>
#include <QWidget>

#include <optional>
#include <vector>

class QCheckBox;
class QComboBox;
class QLabel;
class QPushButton;

// Edits the controls file the game reads (PS2X_CONTROLS). Click a slot, then press a key or a
// controller button to bind it; right-click a slot to clear it. Changes are saved at once.
class ControlsTab : public QWidget
{
    Q_OBJECT

public:
    explicit ControlsTab(const QString &filePath, QWidget *parent = nullptr);
    ~ControlsTab() override;

    QString filePath() const { return m_path; }

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;
    void showEvent(QShowEvent *event) override;

private:
    struct Slot
    {
        QPushButton *button;
        int action;
        bool keyboard;
        int index;
    };

    ps2x::input::Binding &bindingFor(const Slot &slot);
    void startCapture(int slotIndex);
    void stopCapture();
    void finishCapture(const ps2x::input::Binding &binding);
    void clearSlot(int slotIndex);
    void pollGamepads();
    void refresh();
    void refreshGamepadLabel();
    void save();
    void setStatus(const QString &text);
    QString conflictNote(const ps2x::input::Binding &binding, int action, bool keyboard) const;
    std::optional<ps2x::input::Binding> detectGamepadInput(const std::vector<ps2x::input::GamepadState> &now) const;

    QString m_path;
    ps2x::input::Bindings m_bindings;
    std::vector<Slot> m_slots;
    int m_capturing = -1;
    std::vector<ps2x::input::GamepadState> m_baseline;
    GamepadReader m_pads;
    QTimer m_pollTimer;
    QElapsedTimer m_captureClock;

    QComboBox *m_stickSource[2] = {};
    QCheckBox *m_invertX[2] = {};
    QCheckBox *m_invertY[2] = {};
    QLabel *m_padLabel = nullptr;
    QLabel *m_status = nullptr;
};
