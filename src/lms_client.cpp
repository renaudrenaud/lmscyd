#include "lms_client.h"
#include <WiFi.h>
#include <HTTPClient.h>

void LmsClient::init(const char* ip, int port) {
    _url = String("http://") + ip + ":" + port + "/jsonrpc.js";
}

LmsClient::LmsClient(const char* ip, int port) {
    init(ip, port);
}

// ---------------------------------------------------------------------------
//  Requête HTTP POST interne
// ---------------------------------------------------------------------------
bool LmsClient::_post(const String& payload, JsonDocument& doc) {
    if (WiFi.status() != WL_CONNECTED) return false;

    WiFiClient  wifiClient;
    HTTPClient  http;

    http.begin(wifiClient, _url);
    http.addHeader("Content-Type", "application/json");
    http.setTimeout(3000);

    int code = http.POST(const_cast<String&>(payload));
    if (code != 200) {
        http.end();
        return false;
    }

    DeserializationError err = deserializeJson(doc, http.getStream());
    http.end();
    return (err == DeserializationError::Ok);
}

// ---------------------------------------------------------------------------
//  Statut du serveur
// ---------------------------------------------------------------------------
ServerStatus LmsClient::getServerStatus() {
    ServerStatus s;
    JsonDocument doc;
    String payload = F(R"({"id":0,"params":["-",["serverstatus",0,100]],"method":"slim.request"})");

    if (!_post(payload, doc)) return s;

    JsonVariant r = doc["result"];
    if (r.isNull()) return s;

    s.valid       = true;
    s.version     = r["version"].as<String>();
    s.playerCount = r["player count"] | 0;
    s.totalAlbums = r["info total albums"] | 0;
    s.totalSongs  = r["info total songs"]  | 0;
    return s;
}

// ---------------------------------------------------------------------------
//  Recherche du player actif
// ---------------------------------------------------------------------------
bool LmsClient::findPlayer(const char* preferredName, PlayerStatus& out) {
    JsonDocument doc;
    String payload = F(R"({"id":0,"params":["-",["players","0"]],"method":"slim.request"})");

    if (!_post(payload, doc)) return false;

    JsonArray players = doc["result"]["players_loop"].as<JsonArray>();
    if (players.isNull()) return false;

    bool hasPreferred = (strlen(preferredName) > 0);

    // 1er passage : chercher par nom si demandé
    if (hasPreferred) {
        for (JsonVariant p : players) {
            if (p["name"].as<String>() == preferredName) {
                String mac = p["playerid"].as<String>();
                out = getPlayerStatus(mac);
                return out.valid;
            }
        }
    }

    // 2ème passage : premier player en lecture
    for (JsonVariant p : players) {
        if ((p["isplaying"] | 0) == 1) {
            String mac = p["playerid"].as<String>();
            out = getPlayerStatus(mac);
            return out.valid;
        }
    }

    return false;
}

// ---------------------------------------------------------------------------
//  Statut du player (par adresse MAC)
// ---------------------------------------------------------------------------
PlayerStatus LmsClient::getPlayerStatus(const String& mac) {
    PlayerStatus s;
    JsonDocument doc;

    String payload = "{\"id\":0,\"params\":[\"" + mac
                   + "\",[\"status\",\"-\",1]],\"method\":\"slim.request\"}";

    if (!_post(payload, doc)) return s;

    JsonVariant r = doc["result"];
    if (r.isNull()) return s;

    s.valid          = true;
    s.playerid       = mac;
    s.playerName     = r["player_name"].as<String>();
    s.playerIp       = r["player_ip"].as<String>();
    s.isPlaying      = (r["mode"].as<String>() == "play");
    s.elapsed        = r["time"]          | 0.0f;
    s.volume         = r["mixer volume"]  | 0;
    s.playlistTracks = r["playlist_tracks"] | 0;
    s.currentTitle   = r["current_title"].as<String>();

    if (!r["will_sleep_in"].isNull()) {
        s.hasSleepTimer = true;
        s.willSleepIn   = r["will_sleep_in"].as<float>();
    }

    // Index et ID du morceau courant via playlist_loop
    // (playlist_cur_index est relatif au start "-", donc toujours 0 → inutilisable)
    // "playlist index" dans l'item est l'index absolu fourni explicitement par LMS.
    JsonArray playlist = r["playlist_loop"].as<JsonArray>();
    if (!playlist.isNull() && playlist.size() > 0) {
        s.currentSongId = playlist[0]["id"]             | 0;
        s.playlistIndex = playlist[0]["playlist index"] | 0;
    }

    return s;
}

// ---------------------------------------------------------------------------
//  Informations détaillées du morceau
// ---------------------------------------------------------------------------
TrackInfo LmsClient::getSongInfo(int songId, const String& mac) {
    TrackInfo info;
    if (songId <= 0) {
        // Radio : pas d'ID numérique → retourner vide (titre via currentTitle)
        return info;
    }

    JsonDocument doc;
    String payload = "{\"id\":0,\"params\":[\"" + mac
                   + "\",[\"songinfo\",0,100,\"track_id:" + songId
                   + "\"]],\"method\":\"slim.request\"}";

    if (!_post(payload, doc)) return info;

    JsonArray loop = doc["result"]["songinfo_loop"].as<JsonArray>();
    if (loop.isNull()) return info;

    info.valid = true;
    info.id    = songId;

    // Chaque élément du loop est un objet à une seule clé
    for (JsonVariant item : loop) {
        JsonObject obj = item.as<JsonObject>();
        for (JsonPair kv : obj) {
            const char* k = kv.key().c_str();
            if      (strcmp(k, "title")       == 0) info.title      = kv.value().as<String>();
            else if (strcmp(k, "artist")      == 0) info.artist     = kv.value().as<String>();
            else if (strcmp(k, "album")       == 0) info.album      = kv.value().as<String>();
            else if (strcmp(k, "albumartist") == 0) info.albumArtist= kv.value().as<String>();
            else if (strcmp(k, "type")        == 0) info.fileType   = kv.value().as<String>();
            else if (strcmp(k, "bitrate")     == 0) info.bitrate    = kv.value().as<String>();
            else if (strcmp(k, "samplerate")  == 0) info.sampleRate = kv.value().as<int>();
            else if (strcmp(k, "samplesize")  == 0) info.sampleSize = kv.value().as<int>();
            else if (strcmp(k, "duration")    == 0) info.duration   = kv.value().as<float>();
            else if (strcmp(k, "remote_title")== 0) info.remoteTitle= kv.value().as<String>();
        }
    }

    // Repli sur albumArtist si pas d'artiste
    if (info.artist.isEmpty() && !info.albumArtist.isEmpty()) {
        info.artist = info.albumArtist;
    }

    return info;
}

// ---------------------------------------------------------------------------
//  Commandes de contrôle
// ---------------------------------------------------------------------------
void LmsClient::play(const String& mac) {
    JsonDocument doc;
    String p = "{\"id\":0,\"params\":[\"" + mac + "\",[\"button\",\"play\"]],\"method\":\"slim.request\"}";
    _post(p, doc);
}

void LmsClient::pause(const String& mac) {
    JsonDocument doc;
    String p = "{\"id\":0,\"params\":[\"" + mac + "\",[\"pause\"]],\"method\":\"slim.request\"}";
    _post(p, doc);
}

void LmsClient::nextTrack(const String& mac) {
    JsonDocument doc;
    String p = "{\"id\":0,\"params\":[\"" + mac + "\",[\"playlist\",\"index\",\"+1\"]],\"method\":\"slim.request\"}";
    _post(p, doc);
}

void LmsClient::prevTrack(const String& mac) {
    JsonDocument doc;
    String p = "{\"id\":0,\"params\":[\"" + mac + "\",[\"playlist\",\"index\",\"-1\"]],\"method\":\"slim.request\"}";
    _post(p, doc);
}

void LmsClient::setVolume(const String& mac, int vol) {
    vol = constrain(vol, 0, 100);
    JsonDocument doc;
    String p = "{\"id\":0,\"params\":[\"" + mac + "\",[\"mixer\",\"volume\",\"" + vol + "\"]],\"method\":\"slim.request\"}";
    _post(p, doc);
}
