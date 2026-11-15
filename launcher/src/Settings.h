#pragma once

#include <QString>
#include <QStringList>

// The Drakengard folder the launcher belongs to, found by walking up from the launcher. Empty if
// there is none.
QString findProjectRoot();

// ps2x.ini beside the game: NAME=value lines the game reads at start, below anything set in its
// environment. The launcher keeps no settings of its own; its options are lines in this file.
// Lines it doesn't manage, comments included, are kept as they are.
class Ps2xIni
{
public:
    // Reads the file. A missing file reads as empty, and is created on the first set().
    void open(const QString &path);
    QString path() const { return m_path; }

    // The value of NAME, or empty if it isn't set.
    QString value(const QString &name) const;

    // Sets NAME to value and writes the file at once. An empty value removes the line, which leaves
    // the game's default. Returns false if the file couldn't be written.
    bool set(const QString &name, const QString &value);

private:
    int find(const QString &name, int from = 0) const;

    QString m_path;
    QStringList m_lines;
};
