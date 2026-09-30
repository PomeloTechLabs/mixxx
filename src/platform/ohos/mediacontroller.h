#pragma once

#include <QObject>
#include <QStringList>

#include "library/coverart.h"

class Library;
class PlayerManager;

namespace mixxx::ohos {
class MediaController : public QObject {
  public:
    MediaController(Library* library, PlayerManager* players, QObject* parent);
    ~MediaController() override;
    void command(const QString& command, double value = 0);
    void loadAdjacent(int direction);

  private:
    void publish();
    void updateArtwork(const TrackPointer& track);
    Library* m_library;
    PlayerManager* m_players;
    QString m_group = QStringLiteral("[Channel1]");
    QStringList m_pausedGroups;
    CoverInfo m_coverInfo;
    QByteArray m_artwork;
    quint64 m_artworkRevision = 0;
};
}
