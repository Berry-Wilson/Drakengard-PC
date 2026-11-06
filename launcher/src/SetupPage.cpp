#include "SetupPage.h"

#include <QDir>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QFontDatabase>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QMimeData>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QSettings>
#include <QVBoxLayout>

SetupPage::SetupPage(const QString &projectRoot, QWidget *parent)
    : QWidget(parent), m_projectRoot(projectRoot)
{
    setAcceptDrops(true);

    auto *title = new QLabel("Set up Drakengard");
    QFont titleFont = title->font();
    titleFont.setPointSize(titleFont.pointSize() + 6);
    titleFont.setBold(true);
    title->setFont(titleFont);

    auto *intro = new QLabel(
        "Choose a disc image (.iso) of your own Drakengard disc (USA, SLUS-20732). Setup extracts the game "
        "and builds it on this PC from your disc, so the game itself isn't part of this download. "
        "The first time takes 20 to 60 minutes and about 10 GB of disk space. "
        "You can stop and continue later, and the disc image isn't needed afterwards.");
    intro->setWordWrap(true);

    m_discPath = new QLineEdit;
    m_discPath->setPlaceholderText("Drop your .iso here or choose it");
    auto *browse = new QPushButton("Choose...");
    auto *discRow = new QHBoxLayout;
    discRow->addWidget(m_discPath, 1);
    discRow->addWidget(browse);

    m_discStatus = new QLabel;
    m_discStatus->setWordWrap(true);
    m_systemStatus = new QLabel;
    m_systemStatus->setWordWrap(true);

    auto *form = new QFormLayout;
    form->addRow("Disc image", discRow);
    form->addRow("", m_discStatus);
    form->addRow("This PC", m_systemStatus);

    m_start = new QPushButton("Set up the game");
    m_start->setDefault(true);
    m_start->setMinimumHeight(36);
    m_cancel = new QPushButton("Stop");
    m_cancel->setMinimumHeight(36);
    m_back = new QPushButton("Back");
    m_back->setMinimumHeight(36);
    auto *buttons = new QHBoxLayout;
    buttons->addWidget(m_start);
    buttons->addWidget(m_cancel);
    buttons->addWidget(m_back);
    buttons->addStretch(1);

    m_stepLabel = new QLabel;
    m_stepLabel->setWordWrap(true);
    m_stepLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_progress = new QProgressBar;
    m_progress->setRange(0, 100);
    m_progress->hide();

    m_log = new QPlainTextEdit;
    m_log->setReadOnly(true);
    m_log->setMaximumBlockCount(3000);
    m_log->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    m_log->setPlaceholderText("Setup's progress appears here.");

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(16, 16, 16, 16);
    layout->addWidget(title);
    layout->addWidget(intro);
    layout->addLayout(form);
    layout->addLayout(buttons);
    layout->addWidget(m_stepLabel);
    layout->addWidget(m_progress);
    layout->addWidget(m_log, 1);

    m_process = new QProcess(this);
    m_process->setProcessChannelMode(QProcess::MergedChannels);
    m_process->setWorkingDirectory(m_projectRoot);
    connect(m_process, &QProcess::readyReadStandardOutput, this, &SetupPage::readOutput);
    connect(m_process, &QProcess::finished, this, &SetupPage::onFinished);
    connect(m_process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error)
            {
                if (error != QProcess::FailedToStart)
                    return;
                showError("Setup couldn't start: " + m_process->errorString());
                updateControls();
            });

    connect(browse, &QPushButton::clicked, this, [this]
            {
                const QString path = QFileDialog::getOpenFileName(this, "Choose your Drakengard disc image", m_discPath->text(), "Disc images (*.iso);;All files (*)");
                if (!path.isEmpty())
                    setDisc(path);
            });
    // Only a changed path: leaving the box by clicking the start button must not disable that button.
    connect(m_discPath, &QLineEdit::editingFinished, this, [this]
            {
                if (m_discPath->text() != m_checkedDisc)
                    setDisc(m_discPath->text());
            });
    connect(m_start, &QPushButton::clicked, this, &SetupPage::start);
    connect(m_cancel, &QPushButton::clicked, this, &SetupPage::cancel);
    connect(m_back, &QPushButton::clicked, this, &SetupPage::backRequested);

    QSettings s(QSettings::IniFormat, QSettings::UserScope, "Drakengard", "Launcher");
    m_discPath->setText(s.value("setup/disc").toString());
}

QString SetupPage::pythonPath() const
{
    const QString bundled = QDir(m_projectRoot).filePath("python/python.exe");
    return QFileInfo::exists(bundled) ? bundled : QString("python");
}

QStringList SetupPage::scriptArguments(const QStringList &arguments) const
{
    return QStringList{"-u", QDir(m_projectRoot).filePath("tools/setup_game.py")} + arguments;
}

void SetupPage::runHelper(const QStringList &arguments, std::function<void(int, const QString &)> done)
{
    auto *helper = new QProcess(this);
    helper->setProcessChannelMode(QProcess::MergedChannels);
    helper->setWorkingDirectory(m_projectRoot);
    connect(helper, &QProcess::finished, this, [helper, done](int exitCode, QProcess::ExitStatus)
            {
                done(exitCode, QString::fromUtf8(helper->readAll()).trimmed());
                helper->deleteLater();
            });
    connect(helper, &QProcess::errorOccurred, this, [helper, done](QProcess::ProcessError error)
            {
                if (error != QProcess::FailedToStart)
                    return;
                done(1, "@error Setup's Python wasn't found: " + helper->errorString());
                helper->deleteLater();
            });
    helper->start(pythonPath(), scriptArguments(arguments));
}

void SetupPage::refresh(bool canGoBack)
{
    m_canGoBack = canGoBack;
    m_systemStatus->setText("Checking...");
    runHelper({"doctor"}, [this](int, const QString &output)
              {
                  const QJsonObject doctor = QJsonDocument::fromJson(output.toUtf8()).object();
                  if (doctor.isEmpty())
                  {
                      m_folderProblem = "Setup's own check failed:\n" + output;
                      m_systemStatus->setText(m_folderProblem);
                      updateControls();
                      return;
                  }
                  m_toolsName = doctor["tools"].toString();
                  m_folderProblem = doctor["folderProblem"].toString();
                  QString text = m_toolsName.isEmpty()
                                     ? QString("Microsoft's C++ Build Tools will be installed (about 6 GB, internet needed).")
                                     : QString("Build tools found: %1.").arg(m_toolsName);
                  text += QString(" %1 GB free here, about %2 GB needed.").arg(doctor["freeGb"].toInt()).arg(doctor["needGb"].toInt());
                  m_systemStatus->setText(m_folderProblem.isEmpty() ? text : m_folderProblem);
                  updateControls();
              });
    if (!m_discPath->text().isEmpty())
        setDisc(m_discPath->text());
    updateControls();
}

void SetupPage::setDisc(const QString &path)
{
    m_discPath->setText(QDir::toNativeSeparators(path));
    m_checkedDisc = m_discPath->text();
    m_discOk = false;
    m_discStatus->setText(path.isEmpty() ? QString() : QString("Checking the disc..."));
    updateControls();
    if (path.isEmpty())
        return;

    const int check = ++m_discCheck;
    runHelper({"check-disc", path}, [this, check](int exitCode, const QString &output)
              {
                  if (check != m_discCheck)
                      return;
                  m_discOk = exitCode == 0;
                  QString message = output;
                  message.remove("@error ");
                  message.replace("\\n", "\n");
                  m_discStatus->setText(m_discOk ? QString("Drakengard (USA) recognized.") : message);
                  updateControls();
              });
}

void SetupPage::updateControls()
{
    const bool running = isRunning();
    m_start->setEnabled(!running && m_discOk && m_folderProblem.isEmpty());
    m_cancel->setVisible(running);
    m_back->setVisible(m_canGoBack && !running);
    m_discPath->setEnabled(!running);
}

bool SetupPage::isRunning() const
{
    return m_process->state() != QProcess::NotRunning;
}

void SetupPage::start()
{
    QStringList arguments{"run", "--iso", m_discPath->text()};
    if (m_toolsName.isEmpty())
    {
        const auto answer = QMessageBox::question(
            this, "Install the build tools?",
            "Building the game needs Microsoft's C++ Build Tools, which are free. Setup will download and install "
            "them now: about 6 GB, and Windows will ask for permission.\n\nInstall them?");
        if (answer != QMessageBox::Yes)
            return;
        arguments << "--install-tools";
    }

    QSettings s(QSettings::IniFormat, QSettings::UserScope, "Drakengard", "Launcher");
    s.setValue("setup/disc", m_discPath->text());

    m_cancelled = false;
    m_lastError.clear();
    m_log->clear();
    m_stepLabel->setText("Starting...");
    m_progress->setValue(0);
    m_progress->show();
    m_start->setText("Set up the game");
    m_process->start(pythonPath(), scriptArguments(arguments));
    updateControls();
}

// The script's compilers are child processes, so the whole tree has to go.
void SetupPage::cancel()
{
    if (!isRunning())
        return;
    m_cancelled = true;
    m_stepLabel->setText("Stopping...");
    QProcess::execute("taskkill", {"/T", "/F", "/PID", QString::number(m_process->processId())});
}

void SetupPage::readOutput()
{
    while (m_process->canReadLine())
        handleLine(QString::fromUtf8(m_process->readLine()).trimmed());
}

void SetupPage::handleLine(const QString &line)
{
    if (line.startsWith("@step "))
    {
        const QStringList parts = line.split(' ');
        if (parts.size() >= 4)
        {
            m_stepLabel->setText(QString("Step %1 of %2: %3").arg(parts[1], parts[2], parts.mid(3).join(' ')));
            m_progress->setValue(0);
        }
    }
    else if (line.startsWith("@progress "))
        m_progress->setValue(line.mid(10).toInt());
    else if (line.startsWith("@error "))
        m_lastError = line.mid(7).replace("\\n", "\n");
    else if (!line.isEmpty())
        m_log->appendPlainText(line);
}

void SetupPage::onFinished(int exitCode, QProcess::ExitStatus status)
{
    readOutput();
    m_progress->hide();

    if (m_cancelled)
    {
        m_stepLabel->setText("Setup stopped. Press the button to continue where it left off.");
        m_start->setText("Continue setup");
    }
    else if (status == QProcess::NormalExit && exitCode == 0)
    {
        m_stepLabel->setText("Setup finished.");
        emit setupFinished();
    }
    else
    {
        showError(m_lastError.isEmpty() ? QString("Setup stopped unexpectedly (code %1).").arg(exitCode) : m_lastError);
    }
    updateControls();
}

void SetupPage::showError(const QString &message)
{
    m_progress->hide();
    m_stepLabel->setText("Setup didn't finish:\n" + message);
    m_start->setText("Try again");
}

void SetupPage::dragEnterEvent(QDragEnterEvent *event)
{
    if (!isRunning() && event->mimeData()->hasUrls())
        event->acceptProposedAction();
}

void SetupPage::dropEvent(QDropEvent *event)
{
    const QList<QUrl> urls = event->mimeData()->urls();
    if (!urls.isEmpty() && urls.first().isLocalFile())
        setDisc(urls.first().toLocalFile());
}
