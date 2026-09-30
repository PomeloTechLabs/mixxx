#include "mediacontroller.h"

#include <QBuffer>
#include <QFutureWatcher>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTimer>
#include <QtConcurrentRun>
#include <algorithm>
#include <cmath>

#include "control/controlobject.h"
#include "library/library.h"
#include "library/librarytablemodel.h"
#include "mixer/playerinfo.h"
#include "mixer/basetrackplayer.h"
#include "mixer/playermanager.h"
#include "platform/ohos/mediabridge.h"
#include "track/track.h"

namespace mixxx::ohos {
MediaController::MediaController(Library* library, PlayerManager* players, QObject* parent)
        : QObject(parent), m_library(library), m_players(players) {
    attachMediaBridge(this, [this](const QString& name, double value) {
        command(name, value);
    });
    auto* timer = new QTimer(this);
    connect(timer, &QTimer::timeout, this, [this] { publish(); });
    timer->start(250);
    publish();
}

MediaController::~MediaController() {
    detachMediaBridge(this);
}

void MediaController::updateArtwork(const TrackPointer& track) {
    const CoverInfo coverInfo = track ? track->getCoverInfoWithLocation() : CoverInfo();
    if (coverInfo == m_coverInfo) {
        return;
    }
    m_coverInfo = coverInfo;
    m_artwork.clear();
    const auto revision = ++m_artworkRevision;
    if (!coverInfo.hasImage()) {
        return;
    }
    auto* watcher = new QFutureWatcher<QByteArray>(this);
    connect(watcher, &QFutureWatcher<QByteArray>::finished, this, [this, watcher, revision] {
        const auto artwork = watcher->result();
        watcher->deleteLater();
        if (revision != m_artworkRevision) {
            return;
        }
        m_artwork = artwork;
        ++m_artworkRevision;
        publish();
    });
    watcher->setFuture(QtConcurrent::run([coverInfo] {
        auto image = coverInfo.loadImage().image;
        if (image.isNull()) {
            return QByteArray();
        }
        if (image.width() > 512 || image.height() > 512) {
            image = image.scaled(512, 512, Qt::KeepAspectRatio, Qt::SmoothTransformation);
        }
        QByteArray bytes;
        QBuffer buffer(&bytes);
        if (!buffer.open(QIODevice::WriteOnly) || !image.save(&buffer, "PNG")) {
            return QByteArray();
        }
        return bytes;
    }));
}

void MediaController::publish() {
    auto& info = PlayerInfo::instance();
    const int deck = info.getCurrentPlayingDeck();
    if (deck >= 0) {
        m_group = PlayerManager::groupForDeck(deck);
    }
    const auto tracks = info.getLoadedTracks();
    bool loading = false;
    for (const auto& group : m_players->getVisualPlayerGroups()) {
        const auto* player = m_players->getPlayer(group);
        const auto requested = player ? player->getLoadedTrack() : TrackPointer();
        if (requested && requested != tracks.value(group)) {
            loading = true;
            break;
        }
    }
    bool playing = false;
    for (auto it = tracks.cbegin(); it != tracks.cend(); ++it) {
        if (it.value() && ControlObject::get(ConfigKey(it.key(), "play")) > 0) {
            playing = true;
            if (deck < 0) {
                m_group = it.key();
            }
        }
    }
    auto track = info.getTrackInfo(m_group);
    if (!track) {
        for (auto it = tracks.cbegin(); it != tracks.cend(); ++it) {
            if (it.value()) {
                m_group = it.key();
                track = it.value();
                break;
            }
        }
    }
    updateArtwork(track);
    const double duration = track ? std::max(0.0, track->getDuration() * 1000) : 0;
    const double fraction = ControlObject::get(ConfigKey(m_group, "playposition"));
    QJsonObject state;
    state.insert("ready", true);
    state.insert("playing", playing);
    state.insert("loading", loading);
    state.insert("assetId", track ? track->getId().toString() : QString());
    state.insert("title", track ? track->getTitleInfo() : QString());
    state.insert("artist", track ? track->getArtist() : QString());
    state.insert("album", track ? track->getAlbum() : QString());
    const QString artworkKey = QString::number(m_artworkRevision);
    state.insert("artworkKey", artworkKey);
    state.insert("artworkAvailable", !m_artwork.isEmpty());
    state.insert("duration", duration);
    state.insert("position", std::clamp(fraction, 0.0, 1.0) * duration);
    publishMediaState(QJsonDocument(state).toJson(QJsonDocument::Compact), artworkKey, m_artwork);
}

void MediaController::command(const QString& name, double value) {
    if (name == "pause" || name == "stop") {
        QStringList pausedGroups;
        const auto tracks = PlayerInfo::instance().getLoadedTracks();
        for (auto it = tracks.cbegin(); it != tracks.cend(); ++it) {
            if (ControlObject::get(ConfigKey(it.key(), "play")) > 0) {
                pausedGroups.append(it.key());
                ControlObject::set(ConfigKey(it.key(), "play"), 0);
            }
        }
        if (!pausedGroups.isEmpty()) {
            m_pausedGroups = pausedGroups;
        }
    } else if (name == "play") {
        if (m_pausedGroups.isEmpty()) {
            m_pausedGroups.append(m_group);
        }
        for (const auto& group : std::as_const(m_pausedGroups)) {
            if (PlayerInfo::instance().getTrackInfo(group)) {
                ControlObject::set(ConfigKey(group, "play"), 1);
            }
        }
        m_pausedGroups.clear();
    } else if (name == "next" || name == "previous") {
        loadAdjacent(name == "next" ? 1 : -1);
    } else if (name == "seek" && std::isfinite(value)) {
        const auto track = PlayerInfo::instance().getTrackInfo(m_group);
        if (track && track->getDuration() > 0) {
            ControlObject::set(ConfigKey(m_group, "playposition"),
                    std::clamp(value / (1000 * track->getDuration()), 0.0, 1.0));
        }
    }
    publish();
}

void MediaController::loadAdjacent(int direction) {
    auto* model = m_library->trackTableModel();
    if (!model || !model->initialized() || model->rowCount() == 0) {
        return;
    }
    const auto current = PlayerInfo::instance().getTrackInfo(m_group);
    const auto rows = current ? model->getTrackRows(current->getId()) : QVector<int>();
    const int row = rows.isEmpty() ? (direction > 0 ? -1 : 0) : rows.first();
    const int target = (row + direction + model->rowCount()) % model->rowCount();
    const auto track = model->getTrack(model->index(target, 0));
    if (track) {
        const bool playing = ControlObject::get(ConfigKey(m_group, "play")) > 0;
        ControlObject::set(ConfigKey(m_group, "play"), 0);
        auto* player = m_players->getPlayer(m_group);
        if (!player) {
            return;
        }
        player->slotLoadTrack(track,
#ifdef __STEM__
                mixxx::StemChannelSelection(),
#endif
                playing);
    }
}
}
