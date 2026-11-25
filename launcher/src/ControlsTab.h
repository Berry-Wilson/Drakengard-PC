#pragma once

#include "GamepadReader.h"
#include "runtime/input_bindings.h"

#include <QElapsedTimer>
#include <QString>
#include <QTimer>
#include <QWidget>

#include <optional>
#include <vector>

class BindingBox;
class ControllerDiagram;
class QLabel;
class QPushButton;

// Edits the controls file the game reads (PS2X_CONTROLS). A picture of the controller has a box for
// each PS2 input: click one, then press a key or a controller input to bind it; right-click to
// clear. Changes are saved at once.
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
    void startCapture(int action);
    void stopCapture();
    void finishCapture(const ps2x::input::Binding &binding, bool keyboard);
    void clearBindings(int action, bool keys, bool controller);
    // Bind all: every action in turn, each waiting for the next key or controller input.
    void startBindAll();
    void captureNext();
    void endBindAll(const QString &message = {});
    void showClearMenu(int action, const QPoint &globalPos);
    void pollGamepads();
    void refresh();
    void refreshGamepadLabel();
    void save();
    void setStatus(const QString &text);
    QString conflictNote(const ps2x::input::Binding &binding, int action, bool keyboard) const;
    std::optional<ps2x::input::Binding> detectGamepadInput(const std::vector<ps2x::input::GamepadState> &now) const;

    QString m_path;
    ps2x::input::Bindings m_bindings;
    ControllerDiagram *m_diagram = nullptr;
    std::vector<BindingBox *> m_boxes; // by action
    QString m_names[ps2x::input::kActionCount]; // as the boxes call them
    int m_capturing = -1;
    std::vector<ps2x::input::GamepadState> m_baseline;
    GamepadReader m_pads;
    QTimer m_pollTimer;
    QElapsedTimer m_captureClock;

    bool m_bindAll = false;
    int m_bindAllNext = 0;
    QString m_lastResult;
    QTimer m_nextTimer;
    QTimer m_statusTimer;
    QPushButton *m_bindAllButton = nullptr;
    QPushButton *m_skipButton = nullptr;

    QLabel *m_padLabel = nullptr;
    QLabel *m_status = nullptr;
};
