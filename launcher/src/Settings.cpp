#include "Settings.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSettings>

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

QString settingsFolder()
{
    static const QString folder = []
    {
        const QString root = findProjectRoot();
        return root.isEmpty() ? QCoreApplication::applicationDirPath() : root;
    }();
    return folder;
}

QString settingsFile()
{
    return QDir(settingsFolder()).filePath("Launcher.ini");
}

QString controlsFile()
{
    return QDir(settingsFolder()).filePath("controls.ini");
}

void copyOldSettings()
{
    const QString oldSettings = QSettings(QSettings::IniFormat, QSettings::UserScope, "Drakengard", "Launcher").fileName();
    const QDir oldFolder = QFileInfo(oldSettings).absoluteDir();
    const struct
    {
        QString from, to;
    } files[] = {
        {oldSettings, settingsFile()},
        {oldFolder.filePath("controls.ini"), controlsFile()},
    };
    for (const auto &file : files)
    {
        if (QFileInfo::exists(file.from) && !QFileInfo::exists(file.to))
            QFile::copy(file.from, file.to);
    }
}
