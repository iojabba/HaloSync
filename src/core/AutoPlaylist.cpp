#include "AutoPlaylist.h"
#include <QJsonArray>
#include <QJsonDocument>
#include <QRandomGenerator>
#include <algorithm>
#include <random>

namespace halo {

namespace {
QString fieldValue(const Track &t, AutoRule::Field f)
{
    switch (f) {
    case AutoRule::AlbumArtist: return t.displayAlbumArtist();
    case AutoRule::Artist:      return t.displayArtist();
    case AutoRule::Composer:    return t.composer;
    case AutoRule::Genre:       return t.displayGenre();
    case AutoRule::Album:       return t.displayAlbum();
    default:                    return {};
    }
}

bool textMatches(const QString &value, const QStringList &needles)
{
    for (const QString &n : needles) {
        const QString needle = n.trimmed();
        if (!needle.isEmpty() && value.compare(needle, Qt::CaseInsensitive) == 0) return true;
    }
    return false;
}
} // namespace

bool AutoRule::matches(const Track &t, qint64 nowSecs) const
{
    bool hit = true;
    switch (field) {
    case AlbumArtist: case Artist: case Composer: case Genre: case Album: {
        QStringList needles;
        for (const QString &v : values) needles += v.split(';', Qt::SkipEmptyParts);
        if (needles.isEmpty()) return true;     // empty rule = don't filter
        hit = textMatches(fieldValue(t, field), needles);
        // Artist rules also match the album artist (a "contributing" view).
        if (!hit && field == Artist) hit = textMatches(t.displayAlbumArtist(), needles);
        break;
    }
    case Rating:
        switch (rating) {
        case RatingLike:          hit = t.rating == halo::Like; break;
        case RatingDislike:       hit = t.rating == halo::Dislike; break;
        case RatingUnrated:       hit = t.rating == halo::Unrated; break;
        case RatingLikeOrUnrated: hit = t.rating != halo::Dislike; break;
        default:                  return true;
        }
        break;
    case Year:
        hit = (min <= 0 || t.year >= min) && (max <= 0 || t.year <= max);
        break;
    case AddedInLastDays:
        if (min <= 0) return true;
        hit = t.dateAdded >= nowSecs - qint64(min) * 86400;
        break;
    case Plays:
        switch (playsOp) {
        case PlaysNever:    hit = t.playCount == 0; break;
        case PlaysLessThan: hit = t.playCount < playsN; break;
        case PlaysMoreThan: hit = t.playCount > playsN; break;
        default:            return true;
        }
        break;
    }
    return mode == Include ? hit : !hit;
}

QJsonObject AutoRule::toJson() const
{
    QJsonObject o;
    o["field"] = int(field);
    o["mode"] = int(mode);
    o["values"] = QJsonArray::fromStringList(values);
    o["min"] = min;
    o["max"] = max;
    o["rating"] = rating;
    o["playsOp"] = playsOp;
    o["playsN"] = playsN;
    return o;
}

AutoRule AutoRule::fromJson(const QJsonObject &o)
{
    AutoRule r;
    r.field = Field(qBound(0, o["field"].toInt(), int(Plays)));
    r.mode = o["mode"].toInt() == Exclude ? Exclude : Include;
    for (const auto &v : o["values"].toArray()) r.values << v.toString();
    r.min = o["min"].toInt();
    r.max = o["max"].toInt();
    r.rating = o["rating"].toInt();
    r.playsOp = o["playsOp"].toInt();
    r.playsN = o["playsN"].toInt();
    return r;
}

QStringList AutoRule::fieldNames()
{
    return {"Album artist", "Contributing artist", "Composer", "Genre", "Album", "Rating", "Year",
            "Recently added", "Song plays"};
}

TrackList AutoPlaylistSpec::evaluate(const TrackList &library, qint64 nowSecs) const
{
    TrackList out;
    for (const Track &t : library) {
        bool ok = true;
        for (const AutoRule &r : rules) if (!r.matches(t, nowSecs)) { ok = false; break; }
        if (ok) out.push_back(t);
    }

    auto ci = [](const QString &a, const QString &b) { return a.compare(b, Qt::CaseInsensitive); };
    auto albumOrder = [&](const Track &a, const Track &b) {
        if (int c = ci(a.displayAlbum(), b.displayAlbum())) return c < 0;
        if (a.discNo != b.discNo) return a.discNo < b.discNo;
        return a.trackNo < b.trackNo;
    };
    switch (arrange) {
    case Random: {
        std::mt19937 rng(randomSeed ? randomSeed : QRandomGenerator::global()->generate());
        std::shuffle(out.begin(), out.end(), rng);
        break;
    }
    case ByAlbumArtist:
        std::stable_sort(out.begin(), out.end(), [&](const Track &a, const Track &b) {
            if (int c = ci(a.displayAlbumArtist(), b.displayAlbumArtist())) return c < 0;
            return albumOrder(a, b); });
        break;
    case BySongArtist:
        std::stable_sort(out.begin(), out.end(), [&](const Track &a, const Track &b) {
            if (int c = ci(a.displayArtist(), b.displayArtist())) return c < 0;
            return albumOrder(a, b); });
        break;
    case ByComposer:
        std::stable_sort(out.begin(), out.end(), [&](const Track &a, const Track &b) {
            if (int c = ci(a.composer, b.composer)) return c < 0;
            return albumOrder(a, b); });
        break;
    case ByAlbum:
        std::stable_sort(out.begin(), out.end(), albumOrder);
        break;
    case ByGenre:
        std::stable_sort(out.begin(), out.end(), [&](const Track &a, const Track &b) {
            if (int c = ci(a.displayGenre(), b.displayGenre())) return c < 0;
            return albumOrder(a, b); });
        break;
    case ByDateAdded:
        std::stable_sort(out.begin(), out.end(), [](const Track &a, const Track &b) { return a.dateAdded > b.dateAdded; });
        break;
    case ByYear:
        std::stable_sort(out.begin(), out.end(), [&](const Track &a, const Track &b) {
            if (a.year != b.year) return a.year < b.year;
            return albumOrder(a, b); });
        break;
    case BySong:
        std::stable_sort(out.begin(), out.end(), [&](const Track &a, const Track &b) {
            return ci(a.displayTitle(), b.displayTitle()) < 0; });
        break;
    case ByMostPlayed:
        std::stable_sort(out.begin(), out.end(), [](const Track &a, const Track &b) { return a.playCount > b.playCount; });
        break;
    case ByLeastPlayed:
        std::stable_sort(out.begin(), out.end(), [](const Track &a, const Track &b) { return a.playCount < b.playCount; });
        break;
    }

    if (limit == LimitSongs && limitCount > 0 && out.size() > limitCount) {
        out.resize(limitCount);
    } else if (limit == LimitMinutes && limitCount > 0) {
        const qint64 budget = qint64(limitCount) * 60000;
        qint64 total = 0;
        TrackList cut;
        for (const Track &t : out) {
            if (total + t.durationMs > budget) break;
            total += t.durationMs;
            cut.push_back(t);
        }
        out = cut;
    }
    return out;
}

QString AutoPlaylistSpec::toJsonString() const
{
    QJsonObject o;
    QJsonArray rs;
    for (const AutoRule &r : rules) rs.append(r.toJson());
    o["rules"] = rs;
    o["arrange"] = arrange;
    o["limit"] = limit;
    o["limitCount"] = limitCount;
    return QString::fromUtf8(QJsonDocument(o).toJson(QJsonDocument::Compact));
}

AutoPlaylistSpec AutoPlaylistSpec::fromJsonString(const QString &json)
{
    AutoPlaylistSpec s;
    const QJsonObject o = QJsonDocument::fromJson(json.toUtf8()).object();
    for (const auto &v : o["rules"].toArray()) s.rules.push_back(AutoRule::fromJson(v.toObject()));
    s.arrange = o["arrange"].toInt(ByAlbumArtist);
    s.limit = o["limit"].toInt(NoLimit);
    s.limitCount = o["limitCount"].toInt();
    return s;
}

QStringList AutoPlaylistSpec::arrangeNames()
{
    return {"Randomly", "By album artist", "By song artist", "By composer", "By album", "By genre",
            "By date added", "By year", "By song", "By most played", "By least played"};
}

} // namespace halo
