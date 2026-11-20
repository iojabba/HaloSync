#include "RadioView.h"
#include "Database.h"
#include "Player.h"
#include <QComboBox>
#include <QDesktopServices>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QHeaderView>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QNetworkReply>
#include <QPushButton>
#include <QSplitter>
#include <QTableWidget>
#include <QUrl>
#include <QVBoxLayout>

namespace halo {

namespace {
QPushButton *flat(const QString &text)
{
    auto *b = new QPushButton(text);
    b->setObjectName("flat");
    b->setCursor(Qt::PointingHandCursor);
    return b;
}
QString describe(const RadioStation &s)
{
    QStringList bits;
    if (!s.codec.isEmpty()) bits << (s.bitrate > 0 ? QStringLiteral("%1 %2 Kbps").arg(s.codec).arg(s.bitrate) : s.codec);
    if (!s.country.isEmpty()) bits << s.country;
    return bits.join(QStringLiteral(" · "));
}
} // namespace

RadioView::RadioView(Database *db, Player *player, QWidget *parent) : QWidget(parent), m_db(db), m_player(player)
{
    auto *l = new QVBoxLayout(this);
    l->setContentsMargins(0, 0, 0, 0);
    auto *head = new QHBoxLayout;
    auto *h = new QLabel(QStringLiteral("radio"));
    h->setObjectName("h1");
    head->addWidget(h);
    head->addSpacing(16);
    m_now = new QLabel;
    m_now->setObjectName("dim");
    head->addWidget(m_now, 1);
    auto *addUrlBtn = flat(QStringLiteral("add stream URL"));
    auto *openBtn = flat(QStringLiteral("open station file"));
    head->addWidget(addUrlBtn);
    head->addWidget(openBtn);
    l->addLayout(head);

    auto *search = new QHBoxLayout;
    m_query = new QLineEdit;
    m_query->setPlaceholderText(QStringLiteral("find stations by name"));
    m_query->setClearButtonEnabled(true);
    search->addWidget(m_query, 1);
    m_tag = new QComboBox;
    m_tag->addItem(QStringLiteral("any genre"));
    m_tag->addItems({"pop", "rock", "jazz", "classical", "electronic", "hiphop", "country", "ambient", "chillout",
                     "metal", "soul", "reggae", "latin", "news", "talk", "80s", "90s"});
    search->addWidget(m_tag);
    auto *go = new QPushButton(QStringLiteral("search"));
    go->setObjectName("accent");
    search->addWidget(go);
    l->addLayout(search);
    m_info = new QLabel(QStringLiteral("Stations come from the Radio Browser community directory (radio-browser.info)."));
    m_info->setObjectName("dim");
    l->addWidget(m_info);

    auto *split = new QSplitter;
    split->setHandleWidth(1);
    auto *left = new QWidget;
    auto *ll = new QVBoxLayout(left);
    ll->setContentsMargins(0, 0, 0, 0);
    auto *fh = new QLabel(QStringLiteral("FAVOURITES"));
    fh->setObjectName("h2");
    ll->addWidget(fh);
    m_favList = new QListWidget;
    m_favList->setContextMenuPolicy(Qt::CustomContextMenu);
    ll->addWidget(m_favList, 1);
    split->addWidget(left);

    m_table = new QTableWidget(0, 3);
    m_table->setHorizontalHeaderLabels({QStringLiteral("STATION"), QStringLiteral("GENRES"), QStringLiteral("STREAM")});
    m_table->horizontalHeader()->setDefaultAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    m_table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    m_table->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    m_table->verticalHeader()->hide();
    m_table->verticalHeader()->setDefaultSectionSize(26);
    m_table->setShowGrid(false);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setContextMenuPolicy(Qt::CustomContextMenu);
    split->addWidget(m_table);
    split->setStretchFactor(0, 1);
    split->setStretchFactor(1, 3);
    l->addWidget(split, 1);

    auto runSearch = [this] {
        const bool byTag = m_tag->currentIndex() > 0;
        m_info->setText(QStringLiteral("Searching…"));
        if (byTag && m_query->text().trimmed().isEmpty()) m_browser.search(m_tag->currentText(), true);
        else m_browser.search(m_query->text(), false);
    };
    connect(go, &QPushButton::clicked, this, runSearch);
    connect(m_query, &QLineEdit::returnPressed, this, runSearch);
    connect(m_tag, &QComboBox::activated, this, runSearch);
    connect(&m_browser, &RadioBrowser::results, this, &RadioView::showResults);
    connect(&m_browser, &RadioBrowser::failed, this, [this](const QString &msg) {
        m_info->setText(QStringLiteral("Couldn't reach the station directory: %1").arg(msg));
    });
    connect(m_table, &QTableWidget::cellDoubleClicked, this, [this](int row, int) { play(m_results.value(row)); });
    connect(m_favList, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem *it) { play(m_favourites.value(m_favList->row(it))); });
    connect(m_table, &QWidget::customContextMenuRequested, this, [this](const QPoint &p) {
        const int row = m_table->rowAt(p.y());
        if (row >= 0) stationMenu(m_results[row], m_table->viewport()->mapToGlobal(p), false);
    });
    connect(m_favList, &QWidget::customContextMenuRequested, this, [this](const QPoint &p) {
        const int row = m_favList->row(m_favList->itemAt(p));
        if (row >= 0) stationMenu(m_favourites[row], m_favList->viewport()->mapToGlobal(p), true);
    });
    connect(addUrlBtn, &QPushButton::clicked, this, &RadioView::addUrl);
    connect(openBtn, &QPushButton::clicked, this, &RadioView::openStationFile);
    connect(m_db, &Database::radioChanged, this, &RadioView::refreshFavourites);
    auto updateNow = [this] {
        if (!m_player->isStream() || !m_player->current()) { m_now->clear(); return; }
        const QString title = m_player->streamTitle();
        m_now->setText(QStringLiteral("playing %1%2").arg(m_player->current()->title, title.isEmpty() ? QString() : QStringLiteral(" · ") + title));
    };
    connect(m_player, &Player::currentChanged, this, updateNow);
    connect(m_player, &Player::streamTitleChanged, this, updateNow);
    refreshFavourites();
}

void RadioView::refreshFavourites()
{
    m_favourites = m_db->radioStations();
    m_favList->clear();
    for (const RadioStation &s : m_favourites) {
        auto *it = new QListWidgetItem(s.name);
        it->setToolTip(s.url);
        m_favList->addItem(it);
    }
    if (m_favourites.isEmpty()) {
        auto *it = new QListWidgetItem(QStringLiteral("Right-click a station to add it here."));
        it->setFlags(Qt::NoItemFlags);
        m_favList->addItem(it);
        m_favourites.clear();
    }
}

void RadioView::showResults(const QVector<RadioStation> &stations)
{
    m_results = stations;
    m_table->setRowCount(int(stations.size()));
    for (int i = 0; i < stations.size(); ++i) {
        const RadioStation &s = stations[i];
        m_table->setItem(i, 0, new QTableWidgetItem(s.name));
        m_table->setItem(i, 1, new QTableWidgetItem(QString(s.tags).replace(',', QStringLiteral(", "))));
        m_table->setItem(i, 2, new QTableWidgetItem(describe(s)));
        m_table->item(i, 0)->setToolTip(s.url);
    }
    m_info->setText(stations.isEmpty() ? QStringLiteral("No stations found.")
                                       : QStringLiteral("%1 stations. Double-click to listen, right-click to add a favourite.").arg(stations.size()));
}

void RadioView::play(const RadioStation &s)
{
    if (s.url.isEmpty()) return;
    const QString path = QUrl(s.url).path().toLower();
    if (path.endsWith(".pls") || path.endsWith(".m3u")) {
        // A station file on the web: fetch it and play the first stream it lists.
        QNetworkRequest req{QUrl(s.url)};
        req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
        req.setTransferTimeout(15000);
        QNetworkReply *reply = m_net.get(req);
        connect(reply, &QNetworkReply::finished, this, [this, reply, s] {
            reply->deleteLater();
            const QStringList urls = RadioBrowser::streamUrlsFromPlaylist(QString::fromUtf8(reply->read(1 << 16)));
            if (urls.isEmpty()) { emit statusMessage(QStringLiteral("Couldn't open %1.").arg(s.name)); return; }
            RadioStation direct = s;
            direct.url = urls.front();
            play(direct);
        });
        return;
    }
    m_player->playStream(s.name, s.url);
    emit statusMessage(QStringLiteral("Tuning in to %1…").arg(s.name));
}

void RadioView::stationMenu(const RadioStation &s, const QPoint &globalPos, bool favourite)
{
    QMenu menu;
    menu.addAction(QStringLiteral("Play"), this, [this, s] { play(s); });
    if (favourite) menu.addAction(QStringLiteral("Remove from favourites"), this, [this, s] { m_db->removeRadioStation(s.id); });
    else menu.addAction(QStringLiteral("Add to favourites"), this, [this, s] { m_db->addRadioStation(s); });
    if (!s.homepage.isEmpty())
        menu.addAction(QStringLiteral("Open the station's website"), this, [s] { QDesktopServices::openUrl(QUrl(s.homepage)); });
    menu.exec(globalPos);
}

void RadioView::addUrl()
{
    bool ok = false;
    const QString url = QInputDialog::getText(this, QStringLiteral("Add stream URL"),
                                              QStringLiteral("Stream address (http:// or https://):"), QLineEdit::Normal, {}, &ok).trimmed();
    if (!ok || url.isEmpty()) return;
    if (!url.startsWith("http://", Qt::CaseInsensitive) && !url.startsWith("https://", Qt::CaseInsensitive)) {
        emit statusMessage(QStringLiteral("A stream address starts with http:// or https://"));
        return;
    }
    const QString name = QInputDialog::getText(this, QStringLiteral("Add stream URL"), QStringLiteral("Station name:"),
                                               QLineEdit::Normal, QUrl(url).host(), &ok).trimmed();
    if (!ok) return;
    RadioStation s;
    s.name = name.isEmpty() ? QUrl(url).host() : name;
    s.url = url;
    m_db->addRadioStation(s);
    play(s);
}

void RadioView::openStationFile()
{
    const QString f = QFileDialog::getOpenFileName(this, QStringLiteral("Open station file"), {},
                                                   QStringLiteral("Station files (*.m3u *.m3u8 *.pls);;All files (*)"));
    if (f.isEmpty()) return;
    QFile file(f);
    if (!file.open(QIODevice::ReadOnly)) return;
    const QStringList urls = RadioBrowser::streamUrlsFromPlaylist(QString::fromUtf8(file.readAll()));
    if (urls.isEmpty()) { emit statusMessage(QStringLiteral("No stream addresses in that file.")); return; }
    RadioStation s;
    s.name = QFileInfo(f).completeBaseName();
    s.url = urls.front();
    m_db->addRadioStation(s);
    play(s);
}

} // namespace halo
