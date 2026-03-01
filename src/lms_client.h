#pragma once
#include <Arduino.h>
#include <ArduinoJson.h>

// ---------------------------------------------------------------------------
//  Structures de données
// ---------------------------------------------------------------------------

struct ServerStatus {
    bool    valid        = false;
    String  version;
    int     playerCount  = 0;
    int     totalAlbums  = 0;
    int     totalSongs   = 0;
};

struct PlayerStatus {
    bool    valid          = false;
    String  playerid;           // adresse MAC
    String  playerName;
    String  playerIp;
    bool    isPlaying      = false;
    float   elapsed        = 0;  // secondes écoulées dans le morceau
    int     volume         = 0;  // 0-100
    bool    hasSleepTimer  = false;
    float   willSleepIn    = 0;
    int     playlistIndex  = 0;
    int     playlistTracks = 0;
    String  currentTitle;
    int     currentSongId  = 0;
};

struct TrackInfo {
    bool    valid       = false;
    int     id          = 0;
    String  title;
    String  artist;
    String  album;
    String  albumArtist;
    String  fileType;          // "mp3", "flac", "aac"…
    String  bitrate;           // ex: "320k", "1411k"
    int     sampleRate  = 0;   // Hz, ex: 44100
    int     sampleSize  = 0;   // bits, ex: 16, 24
    float   duration    = 0;   // secondes
    String  remoteTitle;       // pour les radios
};

// ---------------------------------------------------------------------------
//  Client LMS
// ---------------------------------------------------------------------------

class LmsClient {
public:
    LmsClient() = default;
    LmsClient(const char* ip, int port);
    void init(const char* ip, int port);  // (re)configure l'URL du serveur

    ServerStatus getServerStatus();

    // Cherche le player préféré (par nom) ou le premier en lecture.
    // Retourne true si un player a été trouvé, et remplit `out`.
    bool findPlayer(const char* preferredName, PlayerStatus& out);

    PlayerStatus getPlayerStatus(const String& mac);
    TrackInfo    getSongInfo(int songId, const String& mac);

    // Commandes de contrôle
    void play(const String& mac);
    void pause(const String& mac);
    void nextTrack(const String& mac);
    void prevTrack(const String& mac);
    void setVolume(const String& mac, int vol);

private:
    String _url;
    bool   _post(const String& payload, JsonDocument& doc);
};
