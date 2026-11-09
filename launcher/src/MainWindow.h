#pragma once

#include <QByteArray>
#include <QFile>
#include <QMainWindow>
#include <QProcess>
#include <QProcessEnvironment>
#include <QTimer>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QGroupBox;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QPushButton;
class QSpinBox;
class QStackedWidget;
class QTableWidget;
class ControlsTab;
class SetupPage;

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    MainWindow();

protected:
    void closeEvent(QCloseEvent *event) override;

private:
    QWidget *buildGameTab();
    QWidget *buildVideoTab();
    QWidget *buildDeveloperTab();
    QWidget *buildEnvironmentTab();
    QWidget *buildLogTab();
    void refreshWindowSizes();

    void loadSettings();
    void saveSettings() const;
    void refreshRecordings(const QString &select = {});
    void updateControls();
    void showSetup(bool canGoBack);

    void startGame();
    void stopGame();
    QProcessEnvironment buildEnvironment(const QString &recordFile) const;
    QString validate() const;

    void readProcessOutput();
    void flushLog();
    void appendLog(const QString &text);
    void launcherLog(const QString &text);
    void onProcessFinished(int exitCode, QProcess::ExitStatus status);

    QString m_projectRoot;
    QStackedWidget *m_pages = nullptr;
    SetupPage *m_setup = nullptr;
    QPushButton *m_runSetup = nullptr;

    QLineEdit *m_runnerPath = nullptr;
    QLineEdit *m_elfPath = nullptr;
    QLineEdit *m_recordingsDir = nullptr;
    QLineEdit *m_consoleLogPath = nullptr;

    QCheckBox *m_vif1Thread = nullptr;
    QComboBox *m_gsBackend = nullptr;
    QComboBox *m_aspect = nullptr;
    QComboBox *m_windowSize = nullptr;
    QComboBox *m_drawDistance = nullptr;
    QComboBox *m_inputMode = nullptr;
    QComboBox *m_replayFile = nullptr;
    QPushButton *m_replayBrowse = nullptr;

    QComboBox *m_vu1Mode = nullptr;
    QComboBox *m_vu0Mode = nullptr;
    QDoubleSpinBox *m_vuVerifyAfter = nullptr;
    QCheckBox *m_vif1Stats = nullptr;
    QCheckBox *m_vuStats = nullptr;
    QCheckBox *m_vifVerify = nullptr;
    QGroupBox *m_dumpFrames = nullptr;
    QLineEdit *m_dumpDir = nullptr;
    QSpinBox *m_dumpEvery = nullptr;
    QDoubleSpinBox *m_dumpAfter = nullptr;
    QSpinBox *m_dumpCount = nullptr;

    QTableWidget *m_extraEnv = nullptr;
    ControlsTab *m_controls = nullptr;

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
