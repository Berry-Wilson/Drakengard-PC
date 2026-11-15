#include "MainWindow.h"

#include "ControlsTab.h"
#include "SetupPage.h"

#include <QApplication>
#include <QCheckBox>
#include <QCloseEvent>
#include <QComboBox>
#include <QDateTime>
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
#include <QRegularExpression>
#include <QScrollBar>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QStackedWidget>
#include <QTabWidget>
#include <QVBoxLayout>

namespace
{
    const QString kRunnerRelative = "external/PS2Recomp/out/build/ps2xRuntime/Release/ps2EntryRunner.exe";
    const QString kElfRelative = "iso_extracted/SLUS_207.32";
    constexpr qsizetype kMaxPendingLog = 256 * 1024;
    constexpr int kStopGraceMs = 3000;

    enum InputMode
    {
        InputOff,
        InputRecord,
        InputReplay
    };

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

    QString withName(const QString &tip, const QString &name)
    {
        return tip + (tip.isEmpty() ? "" : " ") + "Sets " + name + " in ps2x.ini.";
    }
}

MainWindow::MainWindow()
{
    m_projectRoot = findProjectRoot();
    m_recordingsDir = defaultPath(m_projectRoot.isEmpty() ? QCoreApplication::applicationDirPath() : m_projectRoot, "input_recordings");
    setWindowTitle("Drakengard Launcher");

    m_play = new QPushButton("Play");
    m_play->setDefault(true);
    m_play->setMinimumHeight(36);
    m_stop = new QPushButton("Stop");
    m_stop->setMinimumHeight(36);
    m_status = new QLabel("Not running");

    m_tabs = new QTabWidget;
    m_tabs->addTab(buildGameTab(), "Game");
    m_tabs->addTab(new QWidget, "Controls"); // replaced by loadControls() once the game folder is known
    m_tabs->addTab(buildVideoTab(), "Video");
    m_tabs->addTab(buildPictureTab(), "Picture");
    m_tabs->addTab(buildPathsTab(), "Paths");
    m_tabs->addTab(buildScriptedTab(), "Scripted");
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
    resize(860, 720);

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
    connect(m_inputMode, &QComboBox::currentIndexChanged, this, [this] { updateControls(); });

    m_runnerPath->setText(defaultPath(m_projectRoot, kRunnerRelative));
    m_elfPath->setText(defaultPath(m_projectRoot, kElfRelative));
    m_consoleLogPath->setText(defaultPath(m_projectRoot, "launch_console.log"));
    refreshRecordings();
    openGameFolder();
    updateControls();

    // Setup needs its script next to the launcher. A checkout with a built runner never opens it by itself.
    if (!m_projectRoot.isEmpty() && QFileInfo::exists(QDir(m_projectRoot).filePath("tools/setup_game.py")))
    {
        m_setup = new SetupPage(m_projectRoot);
        m_pages->addWidget(m_setup);
        connect(m_setup, &SetupPage::setupFinished, this, [this]
                {
                    if (!QFileInfo(m_runnerPath->text()).isFile())
                        m_runnerPath->setText(defaultPath(m_projectRoot, kRunnerRelative));
                    if (!QFileInfo(m_elfPath->text()).isFile())
                        m_elfPath->setText(defaultPath(m_projectRoot, kElfRelative));
                    openGameFolder();
                    m_pages->setCurrentIndex(0);
                    m_status->setText("Ready to play");
                });
        connect(m_setup, &SetupPage::backRequested, this, [this] { m_pages->setCurrentIndex(0); });
        connect(m_runSetup, &QPushButton::clicked, this, [this] { showSetup(true); });
        if (!QFileInfo(m_runnerPath->text()).isFile() || !QFileInfo(m_elfPath->text()).isFile())
            showSetup(false);
    }
    else
    {
        m_runSetup->hide();
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
    if (name == "PS2X_CONTROLS")
        loadControls();
}

QComboBox *MainWindow::optionCombo(const QString &name, const Choices &choices, const QString &tip)
{
    auto *combo = new QComboBox;
    for (const auto &[label, value] : choices)
        combo->addItem(label, value);
    combo->setToolTip(withName(tip, name));
    const int fixed = combo->count();
    m_loaders.push_back([this, combo, name, fixed]
                        {
                            while (combo->count() > fixed)
                                combo->removeItem(fixed);
                            const QString value = m_ini.value(name);
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
    check->setToolTip(withName(tip, name));
    m_loaders.push_back([this, check, name, defaultOn]
                        {
                            const QString value = m_ini.value(name);
                            check->setChecked(value.isEmpty() ? defaultOn : value != "0");
                        });
    connect(check, &QCheckBox::toggled, this, [this, name, defaultOn](bool on) { store(name, on == defaultOn ? QString() : QString(on ? "1" : "0")); });
    return check;
}

QLineEdit *MainWindow::optionText(const QString &name, const QString &placeholder, const QString &tip)
{
    auto *edit = new QLineEdit;
    edit->setPlaceholderText(placeholder);
    edit->setToolTip(withName(tip, name));
    m_loaders.push_back([this, edit, name] { edit->setText(m_ini.value(name)); });
    connect(edit, &QLineEdit::editingFinished, this, [this, edit, name]
            {
                if (edit->text().trimmed() != m_ini.value(name))
                    store(name, edit->text().trimmed());
            });
    return edit;
}

QWidget *MainWindow::optionPath(const QString &name, bool folder, const QString &placeholder, const QString &tip, QLineEdit **out)
{
    QLineEdit *edit = optionText(name, placeholder, tip);
    auto *browse = new QPushButton("Browse...");
    connect(browse, &QPushButton::clicked, this, [this, edit, name, folder]
            {
                const QString start = edit->text().isEmpty() ? gameFolder() : edit->text();
                const QString path = folder ? QFileDialog::getExistingDirectory(this, "Select folder", start)
                                            : QFileDialog::getOpenFileName(this, "Select file", start);
                if (path.isEmpty())
                    return;
                edit->setText(QDir::toNativeSeparators(path));
                store(name, edit->text());
            });
    if (out)
        *out = edit;
    return pathRow(edit, browse);
}

QSpinBox *MainWindow::optionCount(const QString &name, const QString &noneText, const QString &tip)
{
    auto *spin = new QSpinBox;
    spin->setRange(0, 1000000000);
    spin->setSpecialValueText(noneText);
    spin->setToolTip(withName(tip, name));
    m_loaders.push_back([this, spin, name] { spin->setValue(m_ini.value(name).toInt()); });
    connect(spin, &QSpinBox::valueChanged, this, [this, name](int value) { store(name, value > 0 ? QString::number(value) : QString()); });
    return spin;
}

// ---- Tabs -----------------------------------------------------------------------------------

QWidget *MainWindow::buildGameTab()
{
    auto *tab = new QWidget;
    QFormLayout *form = formTab(tab);

    const auto addSessionPath = [&](const QString &label, QLineEdit *&edit, const QString &tip, const QString &caption, const QString &filter)
    {
        edit = new QLineEdit;
        edit->setToolTip(tip);
        auto *button = new QPushButton("Browse...");
        QLineEdit *target = edit;
        connect(button, &QPushButton::clicked, this, [this, target, caption, filter]
                {
                    const QString path = QFileDialog::getOpenFileName(this, caption, target->text(), filter);
                    if (path.isEmpty())
                        return;
                    target->setText(path);
                    if (target == m_elfPath)
                        openGameFolder();
                });
        form->addRow(label, pathRow(edit, button));
    };
    addSessionPath("Runner", m_runnerPath, "The recompiled game, ps2EntryRunner.exe. Found next to the launcher; a change lasts until the launcher closes.",
                   "Select runner", "Programs (*.exe)");
    addSessionPath("Game ELF", m_elfPath, "SLUS_207.32 in the extracted disc. Its folder is the game's folder, which holds ps2x.ini, "
                                          "controls.ini and the mc0 memory card. A change lasts until the launcher closes.",
                   "Select game ELF", "All files (*)");
    connect(m_elfPath, &QLineEdit::editingFinished, this, [this] { openGameFolder(); });

    form->addRow("Controls file", optionPath("PS2X_CONTROLS", false, "controls.ini in the game's folder",
                                             "The key and controller bindings that the Controls tab edits.", &m_controlsPath));
    form->addRow("Saves", optionPath("PS2X_MEMCARD_DIR", true, "mc0 in the game's folder", "Where memory card saves are kept."));
    form->addRow("Performance", optionCheck("VIF1/GIF worker thread", "PS2X_VIF1_THREAD", true,
                                            "Clearing it keeps graphics data handling on the game's thread, which is slower; for comparing."));

    m_inputMode = new QComboBox;
    m_inputMode->addItem("Off");
    m_inputMode->addItem("Record to a new timestamped file");
    m_inputMode->addItem("Replay a recording");
    m_inputMode->setToolTip("Records the pad to input_recordings, or plays a recording back and then hands control back. "
                            "Lasts until the launcher closes. Sets PS2X_INPUT_RECORD or PS2X_INPUT_REPLAY for that run.");
    form->addRow("Pad input", m_inputMode);

    m_replayFile = new QComboBox;
    m_replayBrowse = new QPushButton("Browse...");
    connect(m_replayBrowse, &QPushButton::clicked, this, [this]
            {
                const QString path = QFileDialog::getOpenFileName(this, "Select recording", m_recordingsDir, "Recordings (*.txt);;All files (*)");
                if (!path.isEmpty())
                    refreshRecordings(path);
            });
    auto *replayRow = new QWidget;
    auto *replayLayout = new QHBoxLayout(replayRow);
    replayLayout->setContentsMargins(0, 0, 0, 0);
    replayLayout->addWidget(m_replayFile, 1);
    replayLayout->addWidget(m_replayBrowse);
    form->addRow("Recording", replayRow);

    m_runSetup = new QPushButton("Run setup again...");
    m_runSetup->setToolTip("Extracts the disc and builds the game again, for example after an update.");
    form->addRow("", m_runSetup);

    m_iniLabel = new QLabel;
    m_iniLabel->setWordWrap(true);
    m_iniLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    form->addRow("", m_iniLabel);

    return tab;
}

QWidget *MainWindow::buildVideoTab()
{
    auto *tab = new QWidget;
    QFormLayout *form = formTab(tab);

    m_aspect = optionCombo("PS2X_ASPECT", {{"4:3 (original)", ""}, {"16:9 (experimental)", "16:9"}},
                           "Widescreen. The 3D view shows more at the sides; menus stay a centred 4:3 layout.");
    form->addRow("Aspect ratio", m_aspect);

    m_windowSize = new QComboBox;
    m_windowSize->setToolTip(withName("The game window's size when it opens. It can still be resized.", "PS2X_WINDOW_SIZE"));
    form->addRow("Window size", m_windowSize);
    refreshWindowSizes();
    connect(m_aspect, &QComboBox::currentIndexChanged, this, [this] { refreshWindowSizes(); });
    m_loaders.push_back([this]
                        {
                            const QString value = m_ini.value("PS2X_WINDOW_SIZE");
                            if (m_windowSize->findData(value) < 0)
                                m_windowSize->addItem(value + " (set in ps2x.ini)", value);
                            selectData(m_windowSize, value);
                        });
    connect(m_windowSize, &QComboBox::currentIndexChanged, this, [this] { store("PS2X_WINDOW_SIZE", m_windowSize->currentData().toString()); });

    Choices scales;
    for (int scale = 1; scale <= 8; ++scale)
        scales.push_back({QString("%1x (%2 x %3)%4").arg(scale).arg(640 * scale).arg(448 * scale).arg(scale == 2 ? ", default" : ""),
                          scale == 2 ? QString() : QString::number(scale)});
    form->addRow("Internal resolution", optionCombo("PS2X_PCSX2_UPSCALE", scales,
                                                    "The resolution the game is drawn at, as a multiple of the PS2's. F5 steps through them while playing."));

    form->addRow("Detail", optionCheck("Full-height picture", "PS2X_PROGRESSIVE", false,
                                       "Shows the game's full 480-line frame instead of its 224-line copy for TVs. The biggest gain in detail."));

    form->addRow("Draw distance", optionCombo("PS2X_DRAW_DISTANCE",
                                              {{"Original", ""}, {"2x (experimental)", "2"}, {"3x (experimental)", "3"}, {"5x (experimental)", "5"}},
                                              "Pushes the fog back in ground missions. Objects can appear suddenly and the level's edge may show."));

    form->addRow("Sharpening", optionCombo("PS2X_PCSX2_SHARPNESS", {{"Off", ""}, {"Normal", "1"}, {"Strong", "2"}},
                                           "Sharpening and anisotropic filtering."));

    form->addRow("Renderer", optionCombo("PS2X_PCSX2_RENDERER",
                                         {{"Automatic", ""}, {"Direct3D 11", "dx11"}, {"Direct3D 12", "dx12"}, {"Vulkan", "vulkan"},
                                          {"OpenGL", "opengl"}, {"Software (slow, most accurate)", "software"}},
                                         "The graphics API PCSX2's renderer uses."));

    form->addRow("Movies", optionCombo("PS2X_PCSX2_MOVIE_RENDERER",
                                       {{"Software renderer (default)", ""}, {"Same renderer as the game", "hardware"}},
                                       "Movies are drawn with PCSX2's software renderer, as its GPU renderers garble them."));
    return tab;
}

QWidget *MainWindow::buildPictureTab()
{
    auto *tab = new QWidget;
    QFormLayout *form = formTab(tab, "PCSX2's picture settings. Default leaves each to PCSX2 and its game database. "
                                     "The upscaling fixes only matter above 1x internal resolution.");

    form->addRow("Blending accuracy", optionCombo("PS2X_PCSX2_BLENDING",
                                                  {{"Default (Medium, from the game database)", ""}, {"Minimum", "0"}, {"Basic", "1"}, {"Medium", "2"},
                                                   {"High", "3"}, {"Full", "4"}, {"Maximum", "5"}},
                                                  "Higher is more accurate and slower."));
    form->addRow("Texture filtering", optionCombo("PS2X_PCSX2_TEXTURE_FILTER",
                                                  {{"Default (as the PS2 asks)", ""}, {"Nearest", "0"}, {"Bilinear, always", "1"},
                                                   {"As the PS2 asks", "2"}, {"Bilinear, except sprites", "3"}},
                                                  ""));
    form->addRow("Trilinear filtering", optionCombo("PS2X_PCSX2_TRILINEAR",
                                                    {{"Default (PCSX2's)", ""}, {"Off", "0"}, {"As the PS2 asks", "1"}, {"Forced", "2"}}, ""));
    form->addRow("Mipmapping", optionCheck("As the game sets it up", "PS2X_PCSX2_MIPMAP", true, "Drakengard uses none."));
    form->addRow("Dithering", optionCombo("PS2X_PCSX2_DITHERING",
                                          {{"Default (unscaled)", ""}, {"Off", "0"}, {"Scaled", "1"}, {"Unscaled", "2"}, {"Forced 32-bit", "3"}}, ""));

    form->addRow("Half-pixel offset", optionCombo("PS2X_PCSX2_HALF_PIXEL",
                                                  {{"Default (Special, from the game database)", ""}, {"Off", "0"}, {"Normal (vertex)", "1"},
                                                   {"Special (texture)", "2"}, {"Special (texture, aggressive)", "3"}, {"Align to native", "4"},
                                                   {"Align to native, with texture offset", "5"}},
                                                  "Upscaling fix."));
    form->addRow("Round sprite", optionCombo("PS2X_PCSX2_ROUND_SPRITE", {{"Off (default)", ""}, {"Half", "1"}, {"Full", "2"}}, "Upscaling fix."));
    form->addRow("Native scaling", optionCombo("PS2X_PCSX2_NATIVE_SCALING",
                                               {{"Off (default)", ""}, {"Normal", "1"}, {"Aggressive", "2"}, {"Normal (upscaled)", "3"},
                                                {"Aggressive (upscaled)", "4"}},
                                               "Upscaling fix."));
    form->addRow("Align sprite", optionCheck("On", "PS2X_PCSX2_ALIGN_SPRITE", false, "Upscaling fix."));
    form->addRow("Merge sprite", optionCheck("On (from the game database)", "PS2X_PCSX2_MERGE_SPRITE", true, "Upscaling fix."));
    return tab;
}

QWidget *MainWindow::buildPathsTab()
{
    auto *tab = new QWidget;
    QFormLayout *form = formTab(tab, "Where the renderer and its files are. Leave a field empty for the usual place.");

    form->addRow("Renderer DLL", optionPath("PS2X_PCSX2_GS_DLL", false, "pcsx2-gs\\ps2x-gs.dll beside the runner", ""));
    form->addRow("PCSX2 resources", optionPath("PS2X_PCSX2_RESOURCES", true, "resources beside the DLL", "PCSX2's resources folder."));
    form->addRow("PCSX2 log", optionCheck("Write PCSX2's log", "PS2X_PCSX2_LOG", false, "To gs-data\\logs\\ps2x-gs.log beside the DLL."));
    form->addRow("Game serial", optionText("PS2X_GAME_SERIAL", "From the game file's name (SLUS-20732)",
                                           "The serial PCSX2 looks up for its per-game fixes."));
    return tab;
}

QWidget *MainWindow::buildScriptedTab()
{
    auto *tab = new QWidget;
    QFormLayout *form = formTab(tab, "For test runs. These stay in ps2x.ini, so they also apply to later runs until you clear them.");

    form->addRow("Button presses", optionText("PS2X_AUTOPAD", "start:duration:hexmask;...",
                                              "Scripted presses in seconds, counted from the first pad read. 0008 is Start, 4000 is Cross."));
    form->addRow("Stop after", optionCount("PS2X_MAX_TICKS", "No limit", "Exits after this many frames."));
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
    form->addRow("Console log", pathRow(m_consoleLogPath, browse));
    layout->addLayout(form);

    m_log = new QPlainTextEdit;
    m_log->setReadOnly(true);
    m_log->setMaximumBlockCount(5000);
    m_log->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    m_log->setPlaceholderText("Game output appears here.");
    layout->addWidget(m_log, 1);

    return tab;
}

// The first size for each aspect is the one the game opens at by itself, so it sets nothing. The
// large size follows the aspect; a size typed into ps2x.ini stays.
void MainWindow::refreshWindowSizes()
{
    const QString current = m_windowSize->currentData().toString();
    const bool wide = !m_aspect->currentData().toString().isEmpty();
    const QString large = wide ? "1592x896" : "1280x896";
    const bool wasLarge = current == "1280x896" || current == "1592x896";

    {
        const QSignalBlocker blocker(m_windowSize); // rebuilding the list mustn't write each step
        m_windowSize->clear();
        m_windowSize->addItem(wide ? "796x448" : "640x448", QString());
        m_windowSize->addItem(large, large);
        if (!current.isEmpty() && !wasLarge)
            m_windowSize->addItem(current + " (set in ps2x.ini)", current);
        selectData(m_windowSize, wasLarge ? large : current);
    }
    if (m_windowSize->currentData().toString() != current)
        store("PS2X_WINDOW_SIZE", m_windowSize->currentData().toString());
}

// ---- Game folder ----------------------------------------------------------------------------

QString MainWindow::gameFolder() const
{
    const QString elf = m_elfPath->text().trimmed();
    return elf.isEmpty() ? QString() : QFileInfo(elf).absolutePath();
}

void MainWindow::openGameFolder()
{
    const QString folder = gameFolder();
    m_ini.open(folder.isEmpty() ? QString() : QDir(folder).filePath("ps2x.ini"));
    const QString shown = QDir::toNativeSeparators(m_ini.path());
    m_iniLabel->setText(QFileInfo::exists(m_ini.path())
                            ? "Options are kept in " + shown + "."
                            : "Options will be kept in " + shown + " once one is changed.");
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

    const int index = m_controls ? m_tabs->indexOf(m_controls) : 1;
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

// Lists the recordings folder, newest first, and keeps the selection (or selects a given file).
void MainWindow::refreshRecordings(const QString &select)
{
    const QString wanted = select.isEmpty() ? m_replayFile->currentData().toString() : select;

    m_replayFile->clear();
    const QFileInfoList files = QDir(m_recordingsDir).entryInfoList({"*.txt"}, QDir::Files, QDir::Time);
    for (const QFileInfo &file : files)
        m_replayFile->addItem(file.fileName(), file.absoluteFilePath());

    if (wanted.isEmpty() || !QFileInfo::exists(wanted))
        return;
    const QString absolute = QFileInfo(wanted).absoluteFilePath();
    if (m_replayFile->findData(absolute) < 0)
        m_replayFile->insertItem(0, QFileInfo(absolute).fileName(), absolute);
    selectData(m_replayFile, absolute);
}

void MainWindow::updateControls()
{
    const bool running = m_process->state() != QProcess::NotRunning;
    m_play->setEnabled(!running);
    m_stop->setEnabled(running);

    const bool replay = m_inputMode->currentIndex() == InputReplay;
    m_replayFile->setEnabled(replay);
    m_replayBrowse->setEnabled(replay);
}

// ---- Running the game -----------------------------------------------------------------------

QString MainWindow::validate() const
{
    if (!QFileInfo(m_runnerPath->text()).isFile())
        return "The runner was not found:\n" + m_runnerPath->text() + "\n\nBuild the ps2EntryRunner target first, or select it on the Game tab.";
    if (!QFileInfo(m_elfPath->text()).isFile())
        return "The game ELF was not found:\n" + m_elfPath->text() + "\n\nSelect SLUS_207.32 in the extracted disc on the Game tab.";
    if (m_inputMode->currentIndex() == InputReplay && !QFileInfo(m_replayFile->currentData().toString()).isFile())
        return "Select a recording to replay.";
    return {};
}

// The game reads ps2x.ini itself. The environment only carries this run's pad recording, and loses
// any PS2X_ variables the launcher inherited, since those would win over ps2x.ini.
QProcessEnvironment MainWindow::buildEnvironment(const QString &recordFile) const
{
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    const QStringList inherited = env.keys();
    for (const QString &key : inherited)
    {
        if (key.startsWith("PS2X_", Qt::CaseInsensitive))
            env.remove(key);
    }

    if (m_inputMode->currentIndex() == InputRecord)
        env.insert("PS2X_INPUT_RECORD", QDir::toNativeSeparators(recordFile));
    else if (m_inputMode->currentIndex() == InputReplay)
        env.insert("PS2X_INPUT_REPLAY", QDir::toNativeSeparators(m_replayFile->currentData().toString()));
    return env;
}

void MainWindow::startGame()
{
    if (const QString problem = validate(); !problem.isEmpty())
    {
        QMessageBox::warning(this, "Cannot start the game", problem);
        return;
    }

    QString recordFile;
    if (m_inputMode->currentIndex() == InputRecord)
    {
        QDir().mkpath(m_recordingsDir);
        const QString stamp = QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss");
        recordFile = QDir(m_recordingsDir).filePath("input_" + stamp + ".txt");
    }

    const QProcessEnvironment env = buildEnvironment(recordFile);
    const QFileInfo elf(m_elfPath->text());

    m_log->clear();
    m_pendingLog.clear();
    m_consoleLog.close();
    if (!m_consoleLogPath->text().isEmpty())
    {
        m_consoleLog.setFileName(m_consoleLogPath->text());
        if (!m_consoleLog.open(QIODevice::WriteOnly | QIODevice::Truncate))
            launcherLog("Cannot write console log: " + m_consoleLog.errorString());
    }

    launcherLog(QDir::toNativeSeparators(m_runnerPath->text()) + " " + QDir::toNativeSeparators(elf.absoluteFilePath()));
    QStringList runtimeVars = env.keys().filter(QRegularExpression("^PS2X_", QRegularExpression::CaseInsensitiveOption));
    runtimeVars.sort();
    for (const QString &key : runtimeVars)
        launcherLog(key + "=" + env.value(key));

    m_stopRequested = false;
    m_process->setProgram(m_runnerPath->text());
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

    refreshRecordings();
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
