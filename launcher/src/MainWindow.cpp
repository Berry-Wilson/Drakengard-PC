#include "MainWindow.h"

#include "ControlsTab.h"
#include "SetupPage.h"

#include <QAbstractItemView>
#include <QApplication>
#include <QCheckBox>
#include <QCloseEvent>
#include <QComboBox>
#include <QDateTime>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFontDatabase>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QScrollBar>
#include <QSettings>
#include <QSpinBox>
#include <QStackedWidget>
#include <QTabWidget>
#include <QTableWidget>
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

    // Walks up from the launcher's own folder to the Drakengard project.
    QString findProjectRoot()
    {
        QDir dir(QCoreApplication::applicationDirPath());
        do
        {
            if (dir.exists("external/PS2Recomp/ps2xRuntime"))
                return dir.absolutePath();
        } while (dir.cdUp());
        return {};
    }

    QString defaultPath(const QString &root, const QString &relative)
    {
        return root.isEmpty() ? QString() : QDir(root).filePath(relative);
    }

    // A saved path that no longer exists (the project moved) falls back to the detected one.
    QString existingOr(const QString &saved, const QString &fallback)
    {
        return !saved.isEmpty() && QFileInfo::exists(saved) ? saved : fallback;
    }

    // The log may not exist yet, so its folder is what has to. Empty means no log file and is kept.
    QString logPathOr(const QString &saved, const QString &fallback)
    {
        return saved.isEmpty() || QFileInfo(saved).absoluteDir().exists() ? saved : fallback;
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

    QComboBox *vuModeCombo()
    {
        auto *combo = new QComboBox;
        combo->addItem("Translated (default)", QString());
        combo->addItem("Interpreter", QString("0"));
        combo->addItem("Verify (runs both, logs divergence)", QString("verify"));
        return combo;
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

    auto *tabs = new QTabWidget;
    tabs->addTab(buildGameTab(), "Game");
    // The controls file sits beside the launcher's own settings.
    const QString settingsFile = QSettings(QSettings::IniFormat, QSettings::UserScope, "Drakengard", "Launcher").fileName();
    m_controls = new ControlsTab(QFileInfo(settingsFile).absoluteDir().filePath("controls.ini"));
    tabs->addTab(m_controls, "Controls");
    tabs->addTab(buildVideoTab(), "Video");
    tabs->addTab(buildDeveloperTab(), "Developer");
    tabs->addTab(buildEnvironmentTab(), "Environment");
    tabs->addTab(buildLogTab(), "Log");

    m_play = new QPushButton("Play");
    m_play->setDefault(true);
    m_play->setMinimumHeight(36);
    m_stop = new QPushButton("Stop");
    m_stop->setMinimumHeight(36);
    m_status = new QLabel("Not running");

    auto *buttons = new QHBoxLayout;
    buttons->addWidget(m_play);
    buttons->addWidget(m_stop);
    buttons->addWidget(m_status, 1);

    auto *main = new QWidget;
    auto *mainLayout = new QVBoxLayout(main);
    mainLayout->addWidget(tabs);
    mainLayout->addLayout(buttons);

    m_pages = new QStackedWidget;
    m_pages->addWidget(main);
    setCentralWidget(m_pages);

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
    connect(m_vu1Mode, &QComboBox::currentIndexChanged, this, [this] { updateControls(); });
    connect(m_vu0Mode, &QComboBox::currentIndexChanged, this, [this] { updateControls(); });
    connect(m_recordingsDir, &QLineEdit::editingFinished, this, [this] { refreshRecordings(); });

    loadSettings();
    updateControls();

    // Setup needs its script next to the launcher. A checkout with a built runner never opens it by itself.
    if (!m_projectRoot.isEmpty() && QFileInfo::exists(QDir(m_projectRoot).filePath("tools/setup_game.py")))
    {
        m_setup = new SetupPage(m_projectRoot);
        m_pages->addWidget(m_setup);
        connect(m_setup, &SetupPage::setupFinished, this, [this]
                {
                    m_runnerPath->setText(existingOr(m_runnerPath->text(), defaultPath(m_projectRoot, kRunnerRelative)));
                    m_elfPath->setText(existingOr(m_elfPath->text(), defaultPath(m_projectRoot, kElfRelative)));
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

QWidget *MainWindow::buildGameTab()
{
    auto *tab = new QWidget;
    auto *form = new QFormLayout(tab);

    const auto addPath = [&](const QString &label, QLineEdit *&edit, const QString &tip, auto browse)
    {
        edit = new QLineEdit;
        edit->setToolTip(tip);
        auto *button = new QPushButton("Browse...");
        connect(button, &QPushButton::clicked, this, browse);
        form->addRow(label, pathRow(edit, button));
    };

    addPath("Runner", m_runnerPath, "The recompiled game, ps2EntryRunner.exe.", [this]
            {
                const QString path = QFileDialog::getOpenFileName(this, "Select runner", m_runnerPath->text(), "Programs (*.exe)");
                if (!path.isEmpty())
                    m_runnerPath->setText(path);
            });
    addPath("Game ELF", m_elfPath, "SLUS_207.32 in the extracted ISO. The runner reads the extracted files, not the .iso; "
                                   "game data and the mc0 memory card are found next to this file.",
            [this]
            {
                const QString path = QFileDialog::getOpenFileName(this, "Select game ELF", m_elfPath->text());
                if (!path.isEmpty())
                    m_elfPath->setText(path);
            });
    addPath("Recordings", m_recordingsDir, "Folder for pad input recordings.", [this]
            {
                const QString path = QFileDialog::getExistingDirectory(this, "Select recordings folder", m_recordingsDir->text());
                if (path.isEmpty())
                    return;
                m_recordingsDir->setText(path);
                refreshRecordings();
            });

    m_vif1Thread = new QCheckBox("VIF1/GIF worker thread");
    m_vif1Thread->setToolTip("Clearing this sets PS2X_VIF1_THREAD=0.");
    form->addRow("Performance", m_vif1Thread);

    m_inputMode = new QComboBox;
    m_inputMode->addItem("Off");
    m_inputMode->addItem("Record to a new timestamped file");
    m_inputMode->addItem("Replay a recording");
    form->addRow("Pad input", m_inputMode);

    m_replayFile = new QComboBox;
    m_replayBrowse = new QPushButton("Browse...");
    connect(m_replayBrowse, &QPushButton::clicked, this, [this]
            {
                const QString path = QFileDialog::getOpenFileName(this, "Select recording", m_recordingsDir->text(), "Recordings (*.txt);;All files (*)");
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

    return tab;
}

QWidget *MainWindow::buildVideoTab()
{
    auto *tab = new QWidget;
    auto *form = new QFormLayout(tab);

    m_aspect = new QComboBox;
    m_aspect->addItem("4:3 (original)", QString());
    m_aspect->addItem("16:9 (experimental)", QString("16:9"));
    m_aspect->setToolTip("16:9 shows more of the world and moves the HUD to the screen edges. "
                         "Menus stay 4:3. Sets PS2X_ASPECT.");
    form->addRow("Aspect ratio", m_aspect);

    m_windowSize = new QComboBox;
    m_windowSize->setToolTip("The game window's size when it opens. Sets PS2X_WINDOW_SIZE.");
    form->addRow("Window size", m_windowSize);
    refreshWindowSizes();
    connect(m_aspect, &QComboBox::currentIndexChanged, this, [this] { refreshWindowSizes(); });

    m_drawDistance = new QComboBox;
    m_drawDistance->addItem("Original", QString());
    m_drawDistance->addItem("2x (experimental)", QString("2"));
    m_drawDistance->addItem("3x (experimental)", QString("3"));
    m_drawDistance->addItem("5x (experimental)", QString("5"));
    m_drawDistance->setToolTip("Pushes the fog further away, which reveals more of the level. Soldiers may "
                               "appear suddenly, and the level's edge may show. Sets PS2X_DRAW_DISTANCE.");
    form->addRow("Draw distance", m_drawDistance);

    m_gsBackend = new QComboBox;
    m_gsBackend->addItem("Vulkan (parallel-gs)", QString());
    m_gsBackend->addItem("CPU", QString("cpu"));
    form->addRow("GS backend", m_gsBackend);

    return tab;
}

QWidget *MainWindow::buildDeveloperTab()
{
    auto *tab = new QWidget;
    auto *columns = new QHBoxLayout(tab);

    auto *left = new QFormLayout;
    m_vu1Mode = vuModeCombo();
    left->addRow("VU1", m_vu1Mode);
    m_vu0Mode = vuModeCombo();
    left->addRow("VU0", m_vu0Mode);

    m_vuVerifyAfter = new QDoubleSpinBox;
    m_vuVerifyAfter->setRange(0, 86400);
    m_vuVerifyAfter->setDecimals(1);
    m_vuVerifyAfter->setSuffix(" s");
    m_vuVerifyAfter->setToolTip("Translated code runs alone until then, so a slow verified run can reach a late scene.");
    left->addRow("Verify after", m_vuVerifyAfter);

    m_vif1Stats = new QCheckBox("VIF1 worker statistics");
    left->addRow(m_vif1Stats);
    m_vuStats = new QCheckBox("VU statistics");
    left->addRow(m_vuStats);
    m_vifVerify = new QCheckBox("Verify VIF UNPACK");
    m_vifVerify->setToolTip("Runs every UNPACK through both paths and reports any difference.");
    left->addRow(m_vifVerify);
    columns->addLayout(left, 1);

    m_dumpFrames = new QGroupBox("Dump frames");
    m_dumpFrames->setCheckable(true);
    auto *dumpForm = new QFormLayout(m_dumpFrames);
    m_dumpDir = new QLineEdit;
    auto *dumpBrowse = new QPushButton("Browse...");
    connect(dumpBrowse, &QPushButton::clicked, this, [this]
            {
                const QString path = QFileDialog::getExistingDirectory(this, "Select frame dump folder", m_dumpDir->text());
                if (!path.isEmpty())
                    m_dumpDir->setText(path);
            });
    dumpForm->addRow("Folder", pathRow(m_dumpDir, dumpBrowse));
    m_dumpEvery = new QSpinBox;
    m_dumpEvery->setRange(1, 1000000);
    m_dumpEvery->setSuffix(" vsyncs");
    dumpForm->addRow("Every", m_dumpEvery);
    m_dumpAfter = new QDoubleSpinBox;
    m_dumpAfter->setRange(0, 86400);
    m_dumpAfter->setDecimals(1);
    m_dumpAfter->setSuffix(" s");
    dumpForm->addRow("After", m_dumpAfter);
    m_dumpCount = new QSpinBox;
    m_dumpCount->setRange(0, 1000000);
    m_dumpCount->setSpecialValueText("Unlimited");
    dumpForm->addRow("Count", m_dumpCount);
    columns->addWidget(m_dumpFrames, 1);

    return tab;
}

QWidget *MainWindow::buildEnvironmentTab()
{
    auto *tab = new QWidget;
    auto *layout = new QVBoxLayout(tab);

    layout->addWidget(new QLabel("Extra variables for the game, such as PS2X_AUTOPAD. These override the other tabs."));

    m_extraEnv = new QTableWidget(0, 2);
    m_extraEnv->setHorizontalHeaderLabels({"Name", "Value"});
    m_extraEnv->horizontalHeader()->setStretchLastSection(true);
    m_extraEnv->verticalHeader()->hide();
    m_extraEnv->setSelectionBehavior(QAbstractItemView::SelectRows);
    layout->addWidget(m_extraEnv);

    auto *add = new QPushButton("Add");
    connect(add, &QPushButton::clicked, this, [this]
            {
                const int row = m_extraEnv->rowCount();
                m_extraEnv->insertRow(row);
                m_extraEnv->setItem(row, 0, new QTableWidgetItem);
                m_extraEnv->setItem(row, 1, new QTableWidgetItem);
                m_extraEnv->setCurrentCell(row, 0);
                m_extraEnv->editItem(m_extraEnv->currentItem());
            });
    auto *remove = new QPushButton("Remove");
    connect(remove, &QPushButton::clicked, this, [this]
            {
                if (m_extraEnv->currentRow() >= 0)
                    m_extraEnv->removeRow(m_extraEnv->currentRow());
            });
    auto *buttons = new QHBoxLayout;
    buttons->addWidget(add);
    buttons->addWidget(remove);
    buttons->addStretch(1);
    layout->addLayout(buttons);

    return tab;
}

QWidget *MainWindow::buildLogTab()
{
    auto *tab = new QWidget;
    auto *layout = new QVBoxLayout(tab);

    m_consoleLogPath = new QLineEdit;
    m_consoleLogPath->setToolTip("Copy of the game output, rewritten on each start. Leave empty for no file.");
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

// The first size for each aspect is the one the game opens at by itself, so it sets no variable.
void MainWindow::refreshWindowSizes()
{
    const bool wide = !m_aspect->currentData().toString().isEmpty();
    const int selected = qMax(0, m_windowSize->currentIndex());
    m_windowSize->clear();
    m_windowSize->addItem(wide ? "796x448" : "640x448", QString());
    const QString large = wide ? "1592x896" : "1280x896";
    m_windowSize->addItem(large, large);
    m_windowSize->setCurrentIndex(selected);
}

void MainWindow::loadSettings()
{
    QSettings s(QSettings::IniFormat, QSettings::UserScope, "Drakengard", "Launcher");

    m_runnerPath->setText(existingOr(s.value("paths/runner").toString(), defaultPath(m_projectRoot, kRunnerRelative)));
    m_elfPath->setText(existingOr(s.value("paths/elf").toString(), defaultPath(m_projectRoot, kElfRelative)));
    m_recordingsDir->setText(existingOr(s.value("paths/recordings").toString(), defaultPath(m_projectRoot, "input_recordings")));
    const QString defaultLog = defaultPath(m_projectRoot, "launch_console.log");
    m_consoleLogPath->setText(logPathOr(s.value("paths/consoleLog", defaultLog).toString(), defaultLog));

    m_vif1Thread->setChecked(s.value("game/vif1Thread", true).toBool());
    selectData(m_gsBackend, s.value("game/gsBackend", QString()));
    selectData(m_aspect, s.value("game/aspect", QString()));
    selectData(m_windowSize, s.value("game/windowSize", QString()));
    selectData(m_drawDistance, s.value("game/drawDistance", QString()));
    m_inputMode->setCurrentIndex(s.value("game/inputMode", int(InputRecord)).toInt());
    refreshRecordings(s.value("game/replayFile").toString());

    selectData(m_vu1Mode, s.value("developer/vu1Mode", QString()));
    selectData(m_vu0Mode, s.value("developer/vu0Mode", QString()));
    m_vuVerifyAfter->setValue(s.value("developer/vuVerifyAfter", 0.0).toDouble());
    m_vif1Stats->setChecked(s.value("developer/vif1Stats", false).toBool());
    m_vuStats->setChecked(s.value("developer/vuStats", false).toBool());
    m_vifVerify->setChecked(s.value("developer/vifVerify", false).toBool());
    m_dumpFrames->setChecked(s.value("developer/dumpFrames", false).toBool());
    m_dumpDir->setText(s.value("developer/dumpDir").toString());
    m_dumpEvery->setValue(s.value("developer/dumpEvery", 60).toInt());
    m_dumpAfter->setValue(s.value("developer/dumpAfter", 0.0).toDouble());
    m_dumpCount->setValue(s.value("developer/dumpCount", 0).toInt());

    const int rows = s.beginReadArray("environment");
    m_extraEnv->setRowCount(rows);
    for (int i = 0; i < rows; ++i)
    {
        s.setArrayIndex(i);
        m_extraEnv->setItem(i, 0, new QTableWidgetItem(s.value("name").toString()));
        m_extraEnv->setItem(i, 1, new QTableWidgetItem(s.value("value").toString()));
    }
    s.endArray();

    if (!restoreGeometry(s.value("window/geometry").toByteArray()))
        resize(860, 720);
}

void MainWindow::saveSettings() const
{
    QSettings s(QSettings::IniFormat, QSettings::UserScope, "Drakengard", "Launcher");

    s.setValue("paths/runner", m_runnerPath->text());
    s.setValue("paths/elf", m_elfPath->text());
    s.setValue("paths/recordings", m_recordingsDir->text());
    s.setValue("paths/consoleLog", m_consoleLogPath->text());

    s.setValue("game/vif1Thread", m_vif1Thread->isChecked());
    s.setValue("game/gsBackend", m_gsBackend->currentData());
    s.setValue("game/aspect", m_aspect->currentData());
    s.setValue("game/windowSize", m_windowSize->currentData());
    s.setValue("game/drawDistance", m_drawDistance->currentData());
    s.setValue("game/inputMode", m_inputMode->currentIndex());
    s.setValue("game/replayFile", m_replayFile->currentData());

    s.setValue("developer/vu1Mode", m_vu1Mode->currentData());
    s.setValue("developer/vu0Mode", m_vu0Mode->currentData());
    s.setValue("developer/vuVerifyAfter", m_vuVerifyAfter->value());
    s.setValue("developer/vif1Stats", m_vif1Stats->isChecked());
    s.setValue("developer/vuStats", m_vuStats->isChecked());
    s.setValue("developer/vifVerify", m_vifVerify->isChecked());
    s.setValue("developer/dumpFrames", m_dumpFrames->isChecked());
    s.setValue("developer/dumpDir", m_dumpDir->text());
    s.setValue("developer/dumpEvery", m_dumpEvery->value());
    s.setValue("developer/dumpAfter", m_dumpAfter->value());
    s.setValue("developer/dumpCount", m_dumpCount->value());

    s.remove("environment");
    s.beginWriteArray("environment");
    for (int i = 0; i < m_extraEnv->rowCount(); ++i)
    {
        s.setArrayIndex(i);
        const QTableWidgetItem *name = m_extraEnv->item(i, 0);
        const QTableWidgetItem *value = m_extraEnv->item(i, 1);
        s.setValue("name", name ? name->text() : QString());
        s.setValue("value", value ? value->text() : QString());
    }
    s.endArray();

    s.setValue("window/geometry", saveGeometry());
}

// Lists the recordings folder, newest first, and keeps the selection (or selects a given file).
void MainWindow::refreshRecordings(const QString &select)
{
    const QString wanted = select.isEmpty() ? m_replayFile->currentData().toString() : select;

    m_replayFile->clear();
    const QFileInfoList files = QDir(m_recordingsDir->text()).entryInfoList({"*.txt"}, QDir::Files, QDir::Time);
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

    m_vuVerifyAfter->setEnabled(m_vu1Mode->currentData().toString() == "verify" || m_vu0Mode->currentData().toString() == "verify");
}

QString MainWindow::validate() const
{
    if (!QFileInfo(m_runnerPath->text()).isFile())
        return "The runner was not found:\n" + m_runnerPath->text() + "\n\nBuild the ps2EntryRunner target first, or select it on the Game tab.";
    if (!QFileInfo(m_elfPath->text()).isFile())
        return "The game ELF was not found:\n" + m_elfPath->text() + "\n\nSelect SLUS_207.32 in the extracted ISO on the Game tab.";
    if (m_inputMode->currentIndex() == InputRecord && m_recordingsDir->text().isEmpty())
        return "Select a recordings folder to record pad input.";
    if (m_inputMode->currentIndex() == InputReplay && !QFileInfo(m_replayFile->currentData().toString()).isFile())
        return "Select a recording to replay.";
    if (m_dumpFrames->isChecked() && m_dumpDir->text().isEmpty())
        return "Select a folder for frame dumps on the Developer tab.";
    return {};
}

QProcessEnvironment MainWindow::buildEnvironment(const QString &recordFile) const
{
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();

    // Variables left over in the launcher's own environment must not override the controls.
    const QStringList inherited = env.keys();
    for (const QString &key : inherited)
    {
        if (key.startsWith("PS2X_", Qt::CaseInsensitive))
            env.remove(key);
    }

    if (!m_vif1Thread->isChecked())
        env.insert("PS2X_VIF1_THREAD", "0");
    if (const QString backend = m_gsBackend->currentData().toString(); !backend.isEmpty())
        env.insert("PS2X_GS_BACKEND", backend);
    if (const QString aspect = m_aspect->currentData().toString(); !aspect.isEmpty())
        env.insert("PS2X_ASPECT", aspect);
    if (const QString size = m_windowSize->currentData().toString(); !size.isEmpty())
        env.insert("PS2X_WINDOW_SIZE", size);
    if (const QString distance = m_drawDistance->currentData().toString(); !distance.isEmpty())
        env.insert("PS2X_DRAW_DISTANCE", distance);
    // Until the Controls tab saves a file, the game's built-in defaults apply.
    if (QFileInfo::exists(m_controls->filePath()))
        env.insert("PS2X_CONTROLS", QDir::toNativeSeparators(m_controls->filePath()));

    if (m_inputMode->currentIndex() == InputRecord)
        env.insert("PS2X_INPUT_RECORD", QDir::toNativeSeparators(recordFile));
    else if (m_inputMode->currentIndex() == InputReplay)
        env.insert("PS2X_INPUT_REPLAY", QDir::toNativeSeparators(m_replayFile->currentData().toString()));

    const QString vu1 = m_vu1Mode->currentData().toString();
    const QString vu0 = m_vu0Mode->currentData().toString();
    if (!vu1.isEmpty())
        env.insert("PS2X_VU1_NATIVE", vu1);
    if (!vu0.isEmpty())
        env.insert("PS2X_VU0_NATIVE", vu0);
    if ((vu1 == "verify" || vu0 == "verify") && m_vuVerifyAfter->value() > 0)
        env.insert("PS2X_VU_VERIFY_AFTER", QString::number(m_vuVerifyAfter->value()));

    if (m_vif1Stats->isChecked())
        env.insert("PS2X_VIF1_STATS", "1");
    if (m_vuStats->isChecked())
        env.insert("PS2X_VU_STATS", "1");
    if (m_vifVerify->isChecked())
        env.insert("PS2X_VIF_VERIFY", "1");

    if (m_dumpFrames->isChecked())
    {
        env.insert("PS2X_DUMP_FRAMES_DIR", QDir::toNativeSeparators(m_dumpDir->text()));
        env.insert("PS2X_DUMP_FRAMES_EVERY", QString::number(m_dumpEvery->value()));
        if (m_dumpAfter->value() > 0)
            env.insert("PS2X_DUMP_FRAMES_AFTER", QString::number(m_dumpAfter->value()));
        if (m_dumpCount->value() > 0)
            env.insert("PS2X_DUMP_FRAMES_COUNT", QString::number(m_dumpCount->value()));
    }

    for (int i = 0; i < m_extraEnv->rowCount(); ++i)
    {
        const QTableWidgetItem *name = m_extraEnv->item(i, 0);
        const QTableWidgetItem *value = m_extraEnv->item(i, 1);
        if (name && !name->text().trimmed().isEmpty())
            env.insert(name->text().trimmed(), value ? value->text() : QString());
    }

    return env;
}

void MainWindow::startGame()
{
    saveSettings();

    if (const QString problem = validate(); !problem.isEmpty())
    {
        QMessageBox::warning(this, "Cannot start the game", problem);
        return;
    }

    QString recordFile;
    if (m_inputMode->currentIndex() == InputRecord)
    {
        QDir().mkpath(m_recordingsDir->text());
        const QString stamp = QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss");
        recordFile = QDir(m_recordingsDir->text()).filePath("input_" + stamp + ".txt");
    }
    if (m_dumpFrames->isChecked())
        QDir().mkpath(m_dumpDir->text());

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
    saveSettings();
    event->accept();
}
