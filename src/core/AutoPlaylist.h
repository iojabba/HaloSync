#pragma once
#include "Track.h"
#include <QJsonObject>
#include <QStringList>

namespace halo {

// Autoplaylist rule model (SPEC §2.5).
//
// Rules on different fields are combined with AND. Within a text rule the
// values (split on ';') are combined with OR. Exclude rules drop any track
// that matches one of their values.
struct AutoRule {
    enum Field { AlbumArtist, Artist, Composer, Genre, Album, Rating, Year, AddedInLastDays, Plays };
    enum Mode  { Include, Exclude };
    enum RatingFilter { RatingAll, RatingLike, RatingDislike, RatingUnrated, RatingLikeOrUnrated };
    enum PlaysOp { PlaysAll, PlaysNever, PlaysLessThan, PlaysMoreThan };

    Field       field = Genre;
    Mode        mode = Include;
    QStringList values;              // text fields
    int         min = 0, max = 0;    // Year range (0 = open), AddedInLastDays uses min
    int         rating = RatingAll;  // Rating field
    int         playsOp = PlaysAll;  // Plays field
    int         playsN = 0;

    bool matches(const Track &t, qint64 nowSecs) const;
    QJsonObject toJson() const;
    static AutoRule fromJson(const QJsonObject &o);
    static QStringList fieldNames();          // UI labels, index = Field
};

struct AutoPlaylistSpec {
    enum Arrange { Random, ByAlbumArtist, BySongArtist, ByComposer, ByAlbum, ByGenre, ByDateAdded,
                   ByYear, BySong, ByMostPlayed, ByLeastPlayed };
    enum Limit { NoLimit, LimitSongs, LimitMinutes };

    QVector<AutoRule> rules;
    int arrange = ByAlbumArtist;
    int limit = NoLimit;
    int limitCount = 0;
    quint32 randomSeed = 0;   // 0 = new order every evaluation

    TrackList evaluate(const TrackList &library, qint64 nowSecs) const;
    QString toJsonString() const;
    static AutoPlaylistSpec fromJsonString(const QString &json);
    static QStringList arrangeNames();
};

} // namespace halo
