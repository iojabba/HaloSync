#include "TrackModel.h"
#include <QDateTime>
#include <QFont>
#include <QGuiApplication>
#include <QMimeData>
#include <QPalette>
#include <QUrl>

namespace halo {

TrackModel::TrackModel(QObject *parent) : QAbstractTableModel(parent) {}

void TrackModel::setTracks(const TrackList &tracks)
{
    beginResetModel();
    m_tracks = tracks;
    endResetModel();
}

void TrackModel::setNowPlayingId(qint64 id)
{
    if (id == m_nowPlaying) return;
    m_nowPlaying = id;
    if (!m_tracks.isEmpty()) emit dataChanged(index(0, 0), index(rowCount() - 1, ColumnCount - 1), {Qt::FontRole, Qt::ForegroundRole});
}

int TrackModel::rowCount(const QModelIndex &parent) const { return parent.isValid() ? 0 : int(m_tracks.size()); }
int TrackModel::columnCount(const QModelIndex &parent) const { return parent.isValid() ? 0 : ColumnCount; }

QString TrackModel::columnName(int c)
{
    static const QStringList names = {"Title", "Contributing artist", "Album", "Album artist", "Genre", "Length",
                                      "Rating", "Play count", "Release year", "Track number", "Disc", "Composer",
                                      "Date added", "Date last played", "Bitrate", "Size", "Type", "Location"};
    return names.value(c);
}

QString TrackModel::formatDuration(qint64 ms)
{
    const qint64 s = ms / 1000;
    if (s >= 3600) return QStringLiteral("%1:%2:%3").arg(s / 3600).arg((s / 60) % 60, 2, 10, QChar('0')).arg(s % 60, 2, 10, QChar('0'));
    return QStringLiteral("%1:%2").arg(s / 60).arg(s % 60, 2, 10, QChar('0'));
}

QString TrackModel::formatSize(qint64 bytes)
{
    if (bytes >= 1LL << 30) return QStringLiteral("%1 GB").arg(bytes / double(1LL << 30), 0, 'f', 2);
    if (bytes >= 1LL << 20) return QStringLiteral("%1 MB").arg(bytes / double(1LL << 20), 0, 'f', 1);
    return QStringLiteral("%1 KB").arg(bytes > 0 ? qMax<qint64>(1, bytes / 1024) : 0);
}

QString TrackModel::ratingGlyph(int rating)
{
    if (rating > 0) return QStringLiteral("♥");
    if (rating < 0) return QStringLiteral("✕");
    return {};
}

QVariant TrackModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= m_tracks.size()) return {};
    const Track &t = m_tracks[index.row()];
    if (role == TrackIdRole) return t.id;
    if (role == Qt::FontRole && t.id == m_nowPlaying) { QFont f; f.setBold(true); return f; }
    if (role == Qt::ForegroundRole && t.id == m_nowPlaying) return QGuiApplication::palette().color(QPalette::Highlight);
    if (role == Qt::TextAlignmentRole) {
        switch (index.column()) {
        case Length: case PlayCount: case Year: case TrackNo: case DiscNo: case Bitrate: case Size:
            return int(Qt::AlignRight | Qt::AlignVCenter);
        case Rating: return int(Qt::AlignCenter);
        default: return {};
        }
    }
    if (role == Qt::ToolTipRole && index.column() == Path) return t.path;
    if (role != Qt::DisplayRole && role != SortRole) return {};
    const bool sort = role == SortRole;
    auto dt = [](qint64 secs) { return secs ? QDateTime::fromSecsSinceEpoch(secs).toString("yyyy-MM-dd") : QString(); };
    switch (index.column()) {
    case Title:       return sort ? t.displayTitle().toLower() : t.displayTitle();
    case Artist:      return sort ? QVariant(t.displayArtist().toLower()) : QVariant(t.displayArtist());
    case Album:       return sort ? QVariant(t.displayAlbum().toLower() + QString::asprintf("%03d%04d", t.discNo, t.trackNo))
                                  : QVariant(t.displayAlbum());
    case AlbumArtist: return sort ? QVariant(t.displayAlbumArtist().toLower() + t.displayAlbum().toLower() +
                                             QString::asprintf("%03d%04d", t.discNo, t.trackNo))
                                  : QVariant(t.displayAlbumArtist());
    case Genre:       return sort ? QVariant(t.displayGenre().toLower()) : QVariant(t.displayGenre());
    case Length:      return sort ? QVariant(t.durationMs) : QVariant(formatDuration(t.durationMs));
    case Rating:      return sort ? QVariant(t.rating) : QVariant(ratingGlyph(t.rating));
    case PlayCount:   return t.playCount;
    case Year:        return sort ? QVariant(t.year) : QVariant(t.year ? QString::number(t.year) : QString());
    case TrackNo:     return sort ? QVariant(t.trackNo) : QVariant(t.trackNo ? QString::number(t.trackNo) : QString());
    case DiscNo:      return sort ? QVariant(t.discNo) : QVariant(t.discNo ? QString::number(t.discNo) : QString());
    case Composer:    return sort ? QVariant(t.composer.toLower()) : QVariant(t.composer);
    case DateAdded:   return sort ? QVariant(t.dateAdded) : QVariant(dt(t.dateAdded));
    case LastPlayed:  return sort ? QVariant(t.lastPlayed) : QVariant(dt(t.lastPlayed));
    case Bitrate:     return sort ? QVariant(t.bitrateKbps) : QVariant(t.bitrateKbps ? QStringLiteral("%1 Kbps").arg(t.bitrateKbps) : QString());
    case Size:        return sort ? QVariant(t.sizeBytes) : QVariant(formatSize(t.sizeBytes));
    case Format:      return t.format.toUpper();
    case Path:        return t.path;
    }
    return {};
}

QVariant TrackModel::headerData(int section, Qt::Orientation o, int role) const
{
    if (o == Qt::Horizontal && role == Qt::DisplayRole) {
        if (section == TrackNo) return QStringLiteral("#");
        if (section == DiscNo) return QStringLiteral("DISC");
        return columnName(section).toUpper();
    }
    if (o == Qt::Horizontal && role == Qt::ToolTipRole) return columnName(section);
    return {};
}

Qt::ItemFlags TrackModel::flags(const QModelIndex &index) const
{
    return QAbstractTableModel::flags(index) | Qt::ItemIsDragEnabled;
}

QStringList TrackModel::mimeTypes() const { return {"application/x-halo-track-ids", "text/uri-list"}; }

QMimeData *TrackModel::mimeData(const QModelIndexList &indexes) const
{
    auto *m = new QMimeData;
    QStringList ids;
    QList<QUrl> urls;
    QSet<int> rows;
    for (const QModelIndex &i : indexes) rows.insert(i.row());
    QList<int> sorted(rows.begin(), rows.end());
    std::sort(sorted.begin(), sorted.end());
    for (int r : sorted) { ids << QString::number(m_tracks[r].id); urls << QUrl::fromLocalFile(m_tracks[r].path); }
    m->setData("application/x-halo-track-ids", ids.join(',').toUtf8());
    m->setUrls(urls);
    return m;
}

// ------------------------------------------------------------------ proxy
TrackFilterProxy::TrackFilterProxy(QObject *parent) : QSortFilterProxyModel(parent)
{
    setSortRole(TrackModel::SortRole);
    setSortCaseSensitivity(Qt::CaseInsensitive);
}

void TrackFilterProxy::setText(const QString &text)
{
    m_words = text.toLower().split(' ', Qt::SkipEmptyParts);
    invalidateFilter();
}

bool TrackFilterProxy::filterAcceptsRow(int row, const QModelIndex &) const
{
    if (m_words.isEmpty()) return true;
    auto *m = static_cast<TrackModel *>(sourceModel());
    const Track &t = m->trackAt(row);
    const QString hay = (t.displayTitle() + ' ' + t.displayArtist() + ' ' + t.displayAlbumArtist() + ' ' +
                         t.displayAlbum() + ' ' + t.displayGenre() + ' ' + t.composer).toLower();
    for (const QString &w : m_words) if (!hay.contains(w)) return false;
    return true;
}

} // namespace halo
