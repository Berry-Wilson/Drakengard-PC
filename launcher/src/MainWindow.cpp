#include "MainWindow.h"

#include "ControlsTab.h"
#include "SetupPage.h"

#include <QApplication>
#include <QCheckBox>
#include <QCloseEvent>
#include <QComboBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFontDatabase>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollBar>
#include <QSignalBlocker>
#include <QStackedWidget>
#include <QTabWidget>
#include <QVBoxLayout>

namespace
{
    // Setup's build first, then a developer's Visual Studio build.
    const QStringList kRunnerRelative = {"external/PS2Recomp/out/build-game/ps2xRuntime/ps2EntryRunner.exe",
                                         "external/PS2Recomp/out/build/ps2xRuntime/Release/ps2EntryRunner.exe"};
    const QString kElfRelative = "iso_extracted/SLUS_207.32";
    constexpr qsizetype kMaxPendingLog = 256 * 1024;
    constexpr int kStopGraceMs = 3000;

    QString defaultPath(const QString &root, const QString &relative)
    {
        return root.isEmpty() ? QString() : QDir(root).filePath(relative);
    }

    QWidget *pathRow(QLineEdit *edit, QPushButton *browse)
    {
        auto *row = new QWidget;
        auto *layout = new QHBoxLayout(row);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->addWidget(edit);
        layout->addWidget(browse);
        return row;
    }

    QFormLayout *formTab(QWidget *tab, const QString &intro = {})
    {
        auto *layout = new QVBoxLayout(tab);
        if (!intro.isEmpty())
        {
            auto *label = new QLabel(intro);
            label->setWordWrap(true);
            layout->addWidget(label);
        }
        auto *form = new QFormLayout;
        layout->addLayout(form);
        layout->addStretch(1);
        return form;
    }

    void selectData(QComboBox *combo, const QVariant &data)
    {
        const int index = combo->findData(data);
        if (index >= 0)
            combo->setCurrentIndex(index);
    }
}

MainWindow::MainWindow()
{
    m_projectRoot = findProjectRoot();
    setWindowTitle("Drakengard Launcher");

    m_play = new QPushButton("Play");
    m_play->setDefault(true);
    m_play->setMinimumHeight(36);
    m_stop = new QPushButton("Stop");
    m_stop->setMinimumHeight(36);
    m_status = new QLabel("Not running");

    m_tabs = new QTabWidget;
    m_tabs->addTab(new QWidget, "Controls"); // replaced by loadControls() once the game folder is known
    m_tabs->addTab(buildVideoTab(), "Video");
    m_tabs->addTab(buildPictureTab(), "Picture");
    m_tabs->addTab(buildLogTab(), "Log");

    auto *buttons = new QHBoxLayout;
    buttons->addWidget(m_play);
    buttons->addWidget(m_stop);
    buttons->addWidget(m_status, 1);

    auto *main = new QWidget;
    auto *mainLayout = new QVBoxLayout(main);
    mainLayout->addWidget(m_tabs);
    mainLayout->addLayout(buttons);

    m_pages = new QStackedWidget;
    m_pages->addWidget(main);
    setCentralWidget(m_pages);
    resize(1080, 940); // room for the Controls tab's controller diagram

    m_process = new QProcess(this);
    m_process->setProcessChannelMode(QProcess::MergedChannels);
    connect(m_process, &QProcess::readyReadStandardOutput, this, &MainWindow::readProcessOutput);
    connect(m_process, &QProcess::finished, this, &MainWindow::onProcessFinished);
    connect(m_process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error)
            {
                if (error != QProcess::FailedToStart)
                    return;
                launcherLog("Failed to start: " + m_process->errorString());
                m_status->setText("Failed to start");
                m_consoleLog.close();
                updateControls();
            });

    m_killTimer.setSingleShot(true);
    connect(&m_killTimer, &QTimer::timeout, m_process, &QProcess::kill);

    // The log is drawn in batches so a chatty game can't stall the window.
    m_logTimer.setInterval(100);
    connect(&m_logTimer, &QTimer::timeout, this, &MainWindow::flushLog);

    connect(m_play, &QPushButton::clicked, this, &MainWindow::startGame);
    connect(m_stop, &QPushButton::clicked, this, &MainWindow::stopGame);

    m_elfPath = defaultPath(m_projectRoot, kElfRelative);
    m_consoleLogPath->setText(defaultPath(m_projectRoot, "launch_console.log"));
    openGameFolder();
    updateControls();

    // Setup needs its script next to the launcher. A checkout with a built runner never opens it by itself.
    if (!m_projectRoot.isEmpty() && QFileInfo::exists(QDir(m_projectRoot).filePath("tools/setup_game.py")))
    {
        m_setup = new SetupPage(m_projectRoot);
        m_pages->addWidget(m_setup);
        connect(m_setup, &SetupPage::setupFinished, this, [this]
                {
                    openGameFolder();
                    m_pages->setCurrentIndex(0);
                    m_status->setText("Ready to play");
                });
        connect(m_setup, &SetupPage::backRequested, this, [this] { m_pages->setCurrentIndex(0); });
        if (!QFileInfo(runnerPath()).isFile() || !QFileInfo(m_elfPath).isFile())
            showSetup(false);
    }
}

void MainWindow::showSetup(bool canGoBack)
{
    m_setup->refresh(canGoBack);
    m_pages->setCurrentWidget(m_setup);
}

// ---- Option controls ------------------------------------------------------------------------

void MainWindow::store(const QString &name, const QString &value)
{
    if (m_loading)
        return;
    if (!m_ini.set(name, value))
        m_status->setText("Could not write " + QDir::toNativeSeparators(m_ini.path()));
}

QComboBox *MainWindow::optionCombo(const QString &name, const Choices &choices, const QString &tip, const QString &defaultValue)
{
    auto *combo = new QComboBox;
    for (const auto &[label, value] : choices)
        combo->addItem(label, value);
    combo->setToolTip(tip);
    const int fixed = combo->count();
    m_loaders.push_back([this, combo, name, fixed, defaultValue]
                        {
                            while (combo->count() > fixed)
                                combo->removeItem(fixed);
                            QString value = m_ini.value(name);
                            if (!defaultValue.isEmpty() && value == defaultValue)
                                value.clear();
                            if (combo->findData(value) < 0)
                                combo->addItem(value + " (set in ps2x.ini)", value);
                            selectData(combo, value);
                        });
    connect(combo, &QComboBox::currentIndexChanged, this, [this, combo, name] { store(name, combo->currentData().toString()); });
    return combo;
}

// 0 and 1 options. Only the state that differs from the game's default is written.
QCheckBox *MainWindow::optionCheck(const QString &text, const QString &name, bool defaultOn, const QString &tip)
{
    auto *check = new QCheckBox(text);
    check->setToolTip(tip);
    m_loaders.push_back([this, check, name, defaultOn]
                        {
                            const QString value = m_ini.value(name);
                            check->setChecked(value.isEmpty() ? defaultOn : value != "0");
                        });
    connect(check, &QCheckBox::toggled, this, [this, name, defaultOn](bool on) { store(name, on == defaultOn ? QString() : QString(on ? "1" : "0")); });
    return check;
}

// ---- Tabs -----------------------------------------------------------------------------------

QWidget *MainWindow::buildVideoTab()
{
    auto *tab = new QWidget;
    QFormLayout *form = formTab(tab);

    m_aspect = optionCombo("PS2X_ASPECT", {{"4:3", ""}, {"16:9", "16:9"}},
                           "Widescreen. The 3D view shows more at the sides; menus stay a centred 4:3 layout.");
    form->addRow("Aspect Ratio", m_aspect);

    m_windowSize = new QComboBox;
    m_windowSize->setToolTip("The game window's size when it opens. It can still be resized.");
    form->addRow("Window Size", m_windowSize);
    connect(m_aspect, &QComboBox::currentIndexChanged, this, [this] { refreshWindowSizes(); });
    m_loaders.push_back([this] { refreshWindowSizes(); });
    connect(m_windowSize, &QComboBox::currentIndexChanged, this, [this] { store("PS2X_WINDOW_SIZE", m_windowSize->currentData().toString()); });

    QCheckBox *fullScreen = optionCheck("On", "PS2X_FULLSCREEN", false,
                                        "Starts covering the main monitor at its current resolution. F11 switches between "
                                        "full screen and a window of the size above while playing.");
    form->addRow("Full Screen", fullScreen);

    Choices scales;
    for (int scale = 1; scale <= 8; ++scale)
        scales.push_back({QString("%1x").arg(scale), scale == 2 ? QString() : QString::number(scale)}); // 2x is the game's default
    form->addRow("Internal Resolution", optionCombo("PS2X_PCSX2_UPSCALE", scales,
                                                    "The resolution the game is drawn at, as a multiple of the PS2's. F5 steps through them while playing."));


    form->addRow("Fog Distance", optionCombo("PS2X_DRAW_DISTANCE",
                                              {{"Original", ""}, {"2x (experimental)", "2"}, {"3x (experimental)", "3"}, {"5x (experimental)", "5"}},
                                              "Pushes the fog back in ground missions. Objects can appear suddenly and the level's edge may show."));

    form->addRow("Sharpening", optionCombo("PS2X_PCSX2_SHARPNESS", {{"Off", ""}, {"Normal", "1"}, {"Strong", "2"}},
                                           "Sharpening and anisotropic filtering."));

    form->addRow("Renderer", optionCombo("PS2X_PCSX2_RENDERER",
                                         {{"Automatic", ""}, {"Direct3D 11", "dx11"}, {"Direct3D 12", "dx12"}, {"Vulkan", "vulkan"},
                                          {"OpenGL", "opengl"}, {"Software (slow, most accurate)", "software"}},
                                         "Sets graphics API"));

    form->addRow("Detail", optionCheck("Full-Height Picture", "PS2X_PROGRESSIVE", true,
                                       "Shows the game's full 480-line frame instead of its 224-line copy for TVs. The biggest gain in detail."));
    return tab;
}

QWidget *MainWindow::buildPictureTab()
{
    auto *tab = new QWidget;
    QFormLayout *form = formTab(tab);

    form->addRow("Blending Accuracy", optionCombo("PS2X_PCSX2_BLENDING",
                                                  {{"Minimum", "0"}, {"Basic", "1"}, {"Medium (Default)", ""}, {"High", "3"}, {"Full", "4"}, {"Maximum", "5"}},
                                                  "Higher is more accurate and slower.", "2"));
    form->addRow("Texture Filtering", optionCombo("PS2X_PCSX2_TEXTURE_FILTER",
                                                  {{"Nearest", "0"}, {"Bilinear, always", "1"}, {"Set by Game (Default)", ""},
                                                   {"Bilinear, except sprites", "3"}},
                                                  "", "2"));
    // PCSX2's own default, automatic, filters as the PS2 asks.
    form->addRow("Trilinear Filtering", optionCombo("PS2X_PCSX2_TRILINEAR",
                                                    {{"Off", "0"}, {"Set by Game (Default)", ""}, {"Forced", "2"}}, "", "1"));
    form->addRow("Dithering", optionCombo("PS2X_PCSX2_DITHERING",
                                          {{"Off", "0"}, {"Scaled", "1"}, {"Unscaled (Default)", ""}, {"Forced 32-bit", "3"}}, "", "2"));

    form->addRow("Half-Pixel Offset", optionCombo("PS2X_PCSX2_HALF_PIXEL",
                                                  {{"Off", "0"}, {"Normal (vertex)", "1"}, {"Special (texture) (Default)", ""},
                                                   {"Special (texture, aggressive)", "3"}, {"Align to native", "4"},
                                                   {"Align to native, with texture offset", "5"}},
                                                  "Upscaling fix.", "2"));
    form->addRow("Round Sprite", optionCombo("PS2X_PCSX2_ROUND_SPRITE", {{"Off (Default)", ""}, {"Half", "1"}, {"Full", "2"}}, "Upscaling fix.", "0"));
    form->addRow("Native Scaling", optionCombo("PS2X_PCSX2_NATIVE_SCALING",
                                               {{"Off (Default)", ""}, {"Normal", "1"}, {"Aggressive", "2"}, {"Normal (upscaled)", "3"},
                                                {"Aggressive (upscaled)", "4"}},
                                               "Upscaling fix.", "0"));
    form->addRow("Align Sprite", optionCheck("On", "PS2X_PCSX2_ALIGN_SPRITE", false, "Upscaling fix."));
    form->addRow("Merge Sprite", optionCheck("On", "PS2X_PCSX2_MERGE_SPRITE", true, "Upscaling fix."));
    return tab;
}

QWidget *MainWindow::buildLogTab()
{
    auto *tab = new QWidget;
    auto *layout = new QVBoxLayout(tab);

    m_consoleLogPath = new QLineEdit;
    m_consoleLogPath->setToolTip("Copy of the game output, rewritten on each start. Leave empty for no file. "
                                 "A change lasts until the launcher closes.");
    auto *browse = new QPushButton("Browse...");
    connect(browse, &QPushButton::clicked, this, [this]
            {
                const QString path = QFileDialog::getSaveFileName(this, "Select console log", m_consoleLogPath->text());
                if (!path.isEmpty())
                    m_consoleLogPath->setText(path);
            });
    auto *form = new QFormLayout;
    form->addRow("Console Log", pathRow(m_consoleLogPath, browse));
    layout->addLayout(form);

    m_log = new QPlainTextEdit;
    m_log->setReadOnly(true);
    m_log->setMaximumBlockCount(5000);
    m_log->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    m_log->setPlaceholderText("Game output appears here.");
    layout->addWidget(m_log, 1);

    return tab;
}

// Each aspect offers its own sizes, none of them the game's own 640x448 or 796x448, so one is
// always written. A size typed into ps2x.ini stays; otherwise switching aspect picks the first size.
void MainWindow::refreshWindowSizes()
{
    const QString saved = m_ini.value("PS2X_WINDOW_SIZE");
    const QString current = m_loading ? saved : m_windowSize->currentData().toString();
    const bool wide = !m_aspect->currentData().toString().isEmpty();

    static const std::vector<std::pair<int, int>> fourThree = {{1024, 768}, {1280, 960}, {1440, 1080}, {1600, 1200}, {1920, 1440},
                                                               {2048, 1536}, {2880, 2160}, {3200, 2400}, {3840, 2880}, {4096, 3072}};
    static const std::vector<std::pair<int, int>> sixteenNine = {{960, 540}, {1280, 720}, {1366, 768}, {1600, 900}, {1920, 1080},
                                                                 {2048, 1152}, {2560, 1440}, {3200, 1800}, {3840, 2160}, {4096, 2304}};
    Choices sizes;
    for (const auto &[w, h] : wide ? sixteenNine : fourThree)
        sizes.push_back({QString("%1 × %2").arg(w).arg(h), QString("%1x%2").arg(w).arg(h)});

    const QString custom = " (set in ps2x.ini)";
    const bool typed = m_loading ? !saved.isEmpty() : m_windowSize->currentText().endsWith(custom);
    {
        const QSignalBlocker blocker(m_windowSize); // rebuilding the list mustn't write each step
        m_windowSize->clear();
        for (const auto &[label, value] : sizes)
            m_windowSize->addItem(label, value);
        if (typed && m_windowSize->findData(current) < 0)
            m_windowSize->addItem(current + custom, current);
        m_windowSize->setCurrentIndex(qMax(0, m_windowSize->findData(current)));
    }

    // Written even while loading, so a 4:3 window without a size gets the one shown.
    const QString chosen = m_windowSize->currentData().toString();
    if (chosen != saved && !m_ini.path().isEmpty() && !m_ini.set("PS2X_WINDOW_SIZE", chosen))
        m_status->setText("Could not write " + QDir::toNativeSeparators(m_ini.path()));
}

// ---- Game folder ----------------------------------------------------------------------------

QString MainWindow::gameFolder() const
{
    return m_elfPath.isEmpty() ? QString() : QFileInfo(m_elfPath).absolutePath();
}

void MainWindow::openGameFolder()
{
    const QString folder = gameFolder();
    m_ini.open(folder.isEmpty() ? QString() : QDir(folder).filePath("ps2x.ini"));
    loadOptions();
    loadControls();
}

void MainWindow::loadOptions()
{
    m_loading = true;
    for (const auto &load : m_loaders)
        load();
    m_loading = false;
}

// The game reads its controls from the working folder, which the launcher sets to the game's folder.
QString MainWindow::controlsPath() const
{
    const QString named = m_ini.value("PS2X_CONTROLS");
    const QString folder = gameFolder();
    if (named.isEmpty())
        return folder.isEmpty() ? QString() : QDir(folder).filePath("controls.ini");
    return QDir::cleanPath(QDir(folder).absoluteFilePath(named));
}

void MainWindow::loadControls()
{
    const QString path = controlsPath();
    if (m_controls && m_controls->filePath() == path)
        return;

    const int index = m_controls ? m_tabs->indexOf(m_controls) : 0;
    auto *controls = new ControlsTab(path);
    QWidget *previous = m_tabs->widget(index);
    const bool showing = m_tabs->currentIndex() == index;
    m_tabs->removeTab(index);
    m_tabs->insertTab(index, controls, "Controls");
    if (showing)
        m_tabs->setCurrentIndex(index);
    previous->deleteLater();
    m_controls = controls;
}

void MainWindow::updateControls()
{
    const bool running = m_process->state() != QProcess::NotRunning;
    m_play->setEnabled(!running);
    m_stop->setEnabled(running);
}

// ---- Running the game -----------------------------------------------------------------------

QString MainWindow::runnerPath() const
{
    for (const QString &relative : kRunnerRelative)
    {
        const QString path = defaultPath(m_projectRoot, relative);
        if (QFileInfo(path).isFile())
            return path;
    }
    return defaultPath(m_projectRoot, kRunnerRelative.first());
}

QString MainWindow::validate() const
{
    const QString runner = runnerPath();
    if (!QFileInfo(runner).isFile())
        return "The runner was not found:\n" + QDir::toNativeSeparators(runner) + "\n\nRun setup, or build the ps2EntryRunner target.";
    if (!QFileInfo(m_elfPath).isFile())
        return "The game ELF was not found:\n" + QDir::toNativeSeparators(m_elfPath) + "\n\nRun setup to extract the disc.";
    return {};
}

// The game reads ps2x.ini itself. The environment loses any PS2X_ variables the launcher
// inherited, since those would win over ps2x.ini.
QProcessEnvironment MainWindow::buildEnvironment() const
{
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    const QStringList inherited = env.keys();
    for (const QString &key : inherited)
    {
        if (key.startsWith("PS2X_", Qt::CaseInsensitive))
            env.remove(key);
    }
    return env;
}

void MainWindow::startGame()
{
    if (const QString problem = validate(); !problem.isEmpty())
    {
        QMessageBox::warning(this, "Cannot start the game", problem);
        return;
    }

    const QProcessEnvironment env = buildEnvironment();
    const QFileInfo elf(m_elfPath);

    m_log->clear();
    m_pendingLog.clear();
    m_consoleLog.close();
    if (!m_consoleLogPath->text().isEmpty())
    {
        m_consoleLog.setFileName(m_consoleLogPath->text());
        if (!m_consoleLog.open(QIODevice::WriteOnly | QIODevice::Truncate))
            launcherLog("Cannot write console log: " + m_consoleLog.errorString());
    }

    const QString runner = runnerPath();
    launcherLog(QDir::toNativeSeparators(runner) + " " + QDir::toNativeSeparators(elf.absoluteFilePath()));

    m_stopRequested = false;
    m_process->setProgram(runner);
    m_process->setArguments({QDir::toNativeSeparators(elf.absoluteFilePath())});
    m_process->setWorkingDirectory(elf.absolutePath());
    m_process->setProcessEnvironment(env);
    m_process->start();

    if (m_process->waitForStarted(5000))
    {
        m_status->setText(QString("Running (PID %1)").arg(m_process->processId()));
        m_logTimer.start();
    }
    updateControls();
}

// Asks the game window to close, so the runtime can shut down, then force-kills it after a grace period.
void MainWindow::stopGame()
{
    if (m_process->state() == QProcess::NotRunning)
        return;
    m_stopRequested = true;
    m_status->setText("Stopping...");
    m_stop->setEnabled(false);
    m_process->terminate();
    m_killTimer.start(kStopGraceMs);
}

void MainWindow::readProcessOutput()
{
    const QByteArray data = m_process->readAllStandardOutput();
    if (m_consoleLog.isOpen())
        m_consoleLog.write(data);
    m_pendingLog += data;
    if (m_pendingLog.size() > kMaxPendingLog)
        m_pendingLog.remove(0, m_pendingLog.size() - kMaxPendingLog);
}

void MainWindow::flushLog()
{
    const qsizetype end = m_pendingLog.lastIndexOf('\n');
    if (end < 0)
        return;
    QString text = QString::fromLocal8Bit(m_pendingLog.left(end));
    text.remove('\r');
    m_pendingLog.remove(0, end + 1);
    appendLog(text);
}

void MainWindow::appendLog(const QString &text)
{
    QScrollBar *bar = m_log->verticalScrollBar();
    const bool atBottom = bar->value() == bar->maximum();
    const int position = bar->value();
    m_log->appendPlainText(text);
    bar->setValue(atBottom ? bar->maximum() : position);
}

void MainWindow::launcherLog(const QString &text)
{
    const QString line = "[launcher] " + text;
    appendLog(line);
    if (m_consoleLog.isOpen())
        m_consoleLog.write(line.toLocal8Bit() + '\n');
}

void MainWindow::onProcessFinished(int exitCode, QProcess::ExitStatus status)
{
    m_killTimer.stop();
    m_logTimer.stop();
    readProcessOutput();
    if (!m_pendingLog.isEmpty() && !m_pendingLog.endsWith('\n'))
    {
        m_pendingLog += '\n';
        if (m_consoleLog.isOpen())
            m_consoleLog.write("\n");
    }
    flushLog();

    QString message;
    if (m_stopRequested)
        message = "Stopped";
    else if (status == QProcess::CrashExit)
        message = QString("Crashed (0x%1)").arg(static_cast<uint>(exitCode), 8, 16, QChar('0'));
    else
        message = QString("Exited with code %1").arg(exitCode);
    m_status->setText(message);
    launcherLog(message);
    m_consoleLog.close();

    updateControls();
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    // Text fields write ps2x.ini when they lose focus; make sure the one being edited does.
    if (QWidget *focused = QApplication::focusWidget())
        focused->clearFocus();

    if (m_setup && m_setup->isRunning())
    {
        const auto answer = QMessageBox::question(this, "Setup is running", "Stop setup and close the launcher? It continues where it left off next time.");
        if (answer != QMessageBox::Yes)
        {
            event->ignore();
            return;
        }
        m_setup->cancel();
    }
    if (m_process->state() != QProcess::NotRunning)
    {
        const auto answer = QMessageBox::question(this, "Game is running", "Stop the game and close the launcher?");
        if (answer != QMessageBox::Yes)
        {
            event->ignore();
            return;
        }
        stopGame();
        if (!m_process->waitForFinished(kStopGraceMs))
        {
            m_process->kill();
            m_process->waitForFinished(kStopGraceMs);
        }
    }
    event->accept();
}
