#pragma once

#include "Settings.h"

#include <QByteArray>
#include <QFile>
#include <QMainWindow>
#include <QProcess>
#include <QProcessEnvironment>
#include <QTimer>

#include <functional>
#include <utility>
#include <vector>

class QCheckBox;
class QComboBox;
class QFormLayout;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QPushButton;
class QStackedWidget;
class QTabWidget;
class ControlsTab;
class SetupPage;

// The launcher keeps no settings of its own. The game's options are lines in ps2x.ini beside the
// game, written as soon as one changes. The runner and game are found from the project folder.
class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    MainWindow();

protected:
    void closeEvent(QCloseEvent *event) override;

private:
    using Choices = std::vector<std::pair<QString, QString>>; // label, value; "" is the game's default

    QWidget *buildVideoTab();
    QWidget *buildPictureTab();
    QWidget *buildLogTab();

    // Controls tied to one ps2x.ini option. Each reloads with the file and writes it when changed.
    // The choice whose value is "" is the game's default and writes no line; defaultValue is the
    // same setting spelled out, which ps2x.ini may hold, and shows as that choice.
    QComboBox *optionCombo(const QString &name, const Choices &choices, const QString &tip, const QString &defaultValue = {});
    QCheckBox *optionCheck(const QString &text, const QString &name, bool defaultOn, const QString &tip);
    void store(const QString &name, const QString &value);

    void refreshWindowSizes();
    void openGameFolder();
    void loadOptions();
    QString gameFolder() const;
    QString controlsPath() const;
    void loadControls();
    void updateControls();
    void showSetup(bool canGoBack);

    void startGame();
    void stopGame();
    QProcessEnvironment buildEnvironment() const;
    QString runnerPath() const; // setup's build, else a developer's Visual Studio build
    QString validate() const;

    void readProcessOutput();
    void flushLog();
    void appendLog(const QString &text);
    void launcherLog(const QString &text);
    void onProcessFinished(int exitCode, QProcess::ExitStatus status);

    QString m_projectRoot;
    Ps2xIni m_ini;
    std::vector<std::function<void()>> m_loaders;
    bool m_loading = false;

    QStackedWidget *m_pages = nullptr;
    QTabWidget *m_tabs = nullptr;
    SetupPage *m_setup = nullptr;

    QString m_elfPath;

    QComboBox *m_aspect = nullptr;
    QComboBox *m_windowSize = nullptr;

    ControlsTab *m_controls = nullptr;
    QLineEdit *m_consoleLogPath = nullptr;

    QPushButton *m_play = nullptr;
    QPushButton *m_stop = nullptr;
    QLabel *m_status = nullptr;
    QPlainTextEdit *m_log = nullptr;

    QProcess *m_process = nullptr;
    QTimer m_killTimer;
    QTimer m_logTimer;
    QByteArray m_pendingLog;
    QFile m_consoleLog;
    bool m_stopRequested = false;
};
