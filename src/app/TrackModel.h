#pragma once
#include "Track.h"
#include <QAbstractTableModel>
#include <QSortFilterProxyModel>

namespace halo {

// Song grid (SPEC §2.3). Column visibility is chosen by the view.
class TrackModel : public QAbstractTableModel {
    Q_OBJECT
public:
    enum Column { Title, Artist, Album, AlbumArtist, Genre, Length, Rating, PlayCount, Year, TrackNo, DiscNo,
                  Composer, DateAdded, LastPlayed, Bitrate, Size, Format, Path, ColumnCount };
    static constexpr int SortRole = Qt::UserRole + 1;
    static constexpr int TrackIdRole = Qt::UserRole + 2;

    explicit TrackModel(QObject *parent = nullptr);
    void setTracks(const TrackList &tracks);
    const TrackList &tracks() const { return m_tracks; }
    const Track &trackAt(int row) const { return m_tracks.at(row); }
    void setNowPlayingId(qint64 id);

    int rowCount(const QModelIndex &parent = {}) const override;
    int columnCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QVariant headerData(int section, Qt::Orientation o, int role) const override;
    Qt::ItemFlags flags(const QModelIndex &index) const override;
    QStringList mimeTypes() const override;
    QMimeData *mimeData(const QModelIndexList &indexes) const override;

    static QString columnName(int c);
    static QString formatDuration(qint64 ms);
    static QString formatSize(qint64 bytes);
    static QString ratingGlyph(int rating);

private:
    TrackList m_tracks;
    qint64 m_nowPlaying = 0;
};

// Filters by free text across title/artist/album/genre/composer.
class TrackFilterProxy : public QSortFilterProxyModel {
    Q_OBJECT
public:
    explicit TrackFilterProxy(QObject *parent = nullptr);
    void setText(const QString &text);
protected:
    bool filterAcceptsRow(int row, const QModelIndex &parent) const override;
private:
    QStringList m_words;
};

} // namespace halo
