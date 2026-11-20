#pragma once
#include "Track.h"
#include <QNetworkAccessManager>
#include <QObject>
#include <QVector>

namespace halo {

struct LyricLine {
    qint64 ms = -1;     // -1 for unsynced (plain) lyrics
    QString text;
};

struct Lyrics {
    QVector<LyricLine> lines;
    bool synced = false;
    bool instrumental = false;
    QString source;     // "file", "tag", "cache" or "lrclib"
    bool isEmpty() const { return lines.isEmpty() && !instrumental; }
    // Index of the line being sung at `ms`, or -1.
    int lineAt(qint64 ms) const;
};

// Lyrics for the playing song (SPEC §5, 2b): a .lrc next to the file, then
// embedded lyrics, then the local cache, then LRCLIB (lrclib.net, a free and
// open lyrics database). Results from LRCLIB are cached in the data folder.
class LyricsService : public QObject {
    Q_OBJECT
public:
    explicit LyricsService(QObject *parent = nullptr);
    static bool enabled();                 // setting lyrics/enabled, default on
    static bool onlineEnabled();           // setting lyrics/online, default on

    void fetch(const Track &t);            // emits ready(), possibly synchronously
    static Lyrics parse(const QString &text);    // LRC or plain text
    static Lyrics fromLrclibJson(const QByteArray &json);
    static QString cacheFile(const Track &t);

signals:
    void ready(qint64 trackId, const halo::Lyrics &lyrics);

private:
    Lyrics local(const Track &t) const;
    QNetworkAccessManager m_net;
    qint64 m_pending = 0;
};

} // namespace halo

Q_DECLARE_METATYPE(halo::Lyrics)
