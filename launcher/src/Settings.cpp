#include "Settings.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QSaveFile>

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

void Ps2xIni::open(const QString &path)
{
    m_path = path;
    m_lines.clear();
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return;
    m_lines = QString::fromUtf8(file.readAll()).split('\n');
    if (!m_lines.isEmpty() && m_lines.last().isEmpty())
        m_lines.removeLast(); // the final line break
}

// The game reads the name before '=' with blanks trimmed, and skips comments starting with # or ;.
int Ps2xIni::find(const QString &name, int from) const
{
    for (int i = from; i < m_lines.size(); ++i)
    {
        const QString line = m_lines[i].trimmed();
        const qsizetype equals = line.indexOf('=');
        if (equals > 0 && line.left(equals).trimmed() == name)
            return i;
    }
    return -1;
}

QString Ps2xIni::value(const QString &name) const
{
    // Like the game, the first line for a name is the one that counts.
    const int index = find(name);
    if (index < 0)
        return {};
    const QString line = m_lines[index];
    return line.mid(line.indexOf('=') + 1).trimmed();
}

bool Ps2xIni::set(const QString &name, const QString &value)
{
    const int first = find(name);
    if (value.isEmpty() && first < 0)
        return true;

    if (m_lines.isEmpty() && !QFile::exists(m_path))
        m_lines << "# Drakengard's options, as NAME=value lines. The launcher edits this file; tools/settings.json lists every option.";

    if (first >= 0)
    {
        for (int i = find(name, first + 1); i >= 0; i = find(name, i))
            m_lines.removeAt(i);
        if (value.isEmpty())
            m_lines.removeAt(first);
        else
            m_lines[first] = name + "=" + value;
    }
    else
    {
        m_lines << name + "=" + value;
    }

    QSaveFile file(m_path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text))
        return false;
    file.write((m_lines.join('\n') + '\n').toUtf8());
    return file.commit();
}
