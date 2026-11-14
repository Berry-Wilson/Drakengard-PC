#pragma once

#include <QString>

// The launcher's settings and controls live in the game folder, so each copy of the game keeps its
// own and they move with it.

// The Drakengard folder the launcher belongs to, found by walking up from the launcher. Empty if
// there is none.
QString findProjectRoot();

// The game folder, or the launcher's own folder when there is no game folder above it.
QString settingsFolder();

// Launcher.ini in settingsFolder(), for QSettings with QSettings::IniFormat.
QString settingsFile();

// controls.ini in settingsFolder().
QString controlsFile();

// Copies Launcher.ini and controls.ini from %APPDATA%\Drakengard, where earlier launchers kept them,
// when the game folder has none yet. The old files stay, since other copies of the game may use them.
void copyOldSettings();
