#pragma once

#include <QProcess>
#include <QString>
#include <QStringList>
#include <QWidget>

#include <functional>

class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QProgressBar;
class QPushButton;

// First-run setup: takes the player's disc image and runs tools/setup_game.py, which extracts the
// game and builds it on this PC.
class SetupPage : public QWidget
{
    Q_OBJECT

public:
    explicit SetupPage(const QString &projectRoot, QWidget *parent = nullptr);

    // Re-reads the build tools and disk space. Call when the page is shown.
    void refresh(bool canGoBack);
    bool isRunning() const;
    void cancel();

signals:
    void setupFinished();
    void backRequested();

protected:
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dropEvent(QDropEvent *event) override;

private:
    void setDisc(const QString &path);
    void updateControls();
    void start();
    void readOutput();
    void handleLine(const QString &line);
    void onFinished(int exitCode, QProcess::ExitStatus status);
    void showError(const QString &message);
    QString pythonPath() const;
    QStringList scriptArguments(const QStringList &arguments) const;
    void runHelper(const QStringList &arguments, std::function<void(int, const QString &)> done);

    QString m_projectRoot;
    QLineEdit *m_discPath = nullptr;
    QLabel *m_discStatus = nullptr;
    QLabel *m_systemStatus = nullptr;
    QLabel *m_stepLabel = nullptr;
    QProgressBar *m_progress = nullptr;
    QPlainTextEdit *m_log = nullptr;
    QPushButton *m_start = nullptr;
    QPushButton *m_cancel = nullptr;
    QPushButton *m_back = nullptr;
    QProcess *m_process = nullptr;

    QString m_checkedDisc;
    QString m_toolsName;
    QString m_folderProblem;
    QString m_lastError;
    bool m_discOk = false;
    bool m_cancelled = false;
    bool m_canGoBack = false;
    int m_discCheck = 0;
};
