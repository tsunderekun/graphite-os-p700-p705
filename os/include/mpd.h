// mpd.h — minimal MPD client for the graphite shell.
//
// One persistent TCP connection to 127.0.0.1:6600 for commands, plus a second
// socket that sits in `idle` (MPD 0.24) so UI changes arrive in a few ms;
// the 1 s status poll stays as the fallback when idle is unavailable.
//
// Screens must not perform network I/O in draw(): they call tick() (which
// polls at most once a second), register what they need (want_cover,
// set_library_watch) and enqueue commands. Everything is sent from tick().
//
// P0 additions:
//   * covers   — readpicture -> JPEG decode -> disk cache (384 + 96, RGB565);
//   * library  — one listallinfo -> album index on disk, no per-screen queries;
//   * resume   — uri + elapsed in ~/.config/graphite/player.state.
//
// GS_DEMO=1 makes the client serve a fixed track/queue/status for previews.
#pragma once

#include <stdint.h>
#include <deque>
#include <map>
#include <string>
#include <vector>

struct MpdStatus {
    std::string state;       // "play" | "pause" | "stop"
    double elapsed = 0.0;    // seconds
    double duration = 0.0;   // seconds
    int volume = -1;         // 0..100, -1 if unknown
    bool random = false;
    bool repeat = false;
    bool single = false;
    int song = -1;           // current position in the queue
    int songid = -1;
    int playlistlength = 0;
    int playlist = -1;       // queue version
    int bitrate = 0;         // kbit/s
    std::string audio;       // e.g. "44100:16:2"
    std::string error;       // MPD status error ("Failed to decode ...")
};

struct MpdSong {
    std::string file, title, artist, album, albumartist;
    std::string genre, date, format;
    int id = -1;
    int pos = -1;
    int time = 0;            // seconds
    int bitrate = 0;
};

// ---------------------------------------------------------------- covers

// A decoded, square RGB565 cover. Lives in RAM only; the disk cache holds the
// same pixels (see MpdClient::want_cover).
struct CoverImage {
    int w = 0, h = 0;
    std::vector<uint16_t> px;          // w*h, row-major
    bool ok() const {
        return w > 0 && h > 0 && px.size() == (size_t)w * (size_t)h;
    }
    // Copy into a raw RGB565 surface (Canvas::px, stride Canvas::w).
    // Pure memory copy: safe to call from draw().
    void blit(uint16_t *dst, int dstw, int x, int y, int size) const;
};

// Cover identity. Albums group by (AlbumArtist || Artist, Album); without an
// Album tag the track's folder is used. `uri` is only used for the fallback.
std::string cover_key(const std::string &artist, const std::string &album,
                      const std::string &uri);
std::string cover_key(const MpdSong &s);

// ---------------------------------------------------------------- library

struct LibraryAlbum {
    std::string album, artist, year, uri;  // uri = first track
    std::vector<std::string> tracks;       // playable order, exact uris
    int duration = 0;                      // seconds, sum of tracks
};

// Library index state; screens show honest text for each.
enum { LIB_IDLE = 0, LIB_LOADING, LIB_READY, LIB_ERROR };

class MpdClient {
public:
    MpdClient();
    ~MpdClient();
    MpdClient(const MpdClient &) = delete;
    MpdClient &operator=(const MpdClient &) = delete;

    // Network work. Safe to call from every screen's tick(); internally
    // rate-limited to one status poll per second.
    void tick(unsigned now);
    void set_queue_watch(bool on);
    // Drop the reconnect backoff — called when the user taps "retry".
    void retry_now();

    bool connected() const { return connected_; }
    bool demo() const { return demo_; }
    bool has_song() const { return !song_.file.empty(); }
    bool queue_loaded() const { return queue_loaded_; }
    const MpdStatus &status() const { return status_; }
    const MpdSong &song() const { return song_; }
    const std::vector<MpdSong> &queue() const { return queue_; }
    const std::vector<MpdSong> &playlist_songs() const { return pl_songs_; }
    double elapsed(unsigned now) const;

    // All commands below are queued and sent from tick().
    void play(int pos = -1);
    void pause(bool on);
    void toggle();
    void next();
    void previous();
    void seek(double seconds);
    void setvol(int value);
    void random(bool on);
    void repeat(bool on);
    void single(bool on);
    void clear();
    void load(const std::string &name);
    void save(const std::string &name);
    void list_playlist(const std::string &name);

    // ------------------------------------------------------------- covers
    //
    // Call from tick() with the album key and any track uri of that album.
    // Loads the disk cache if present and otherwise fetches the embedded
    // picture (one cover per tick). `large` also keeps the 384 copy in RAM.
    void want_cover(const std::string &key, const std::string &uri,
                    bool large = false);
    // Memory lookup only — safe in draw(). nullptr while loading.
    const CoverImage *cover(const std::string &key, int size) const;
    // True when the picture was tried and is absent (or MPD said no).
    bool cover_missing(const std::string &key) const;

    // ------------------------------------------------------------ library
    // Call from the library screen's tick(); builds/refreshes the index.
    void set_library_watch(bool on);
    bool library_watch() const { return watch_library_; }
    int library_state() const { return lib_state_; }
    int library_songs() const { return lib_songs_; }
    const std::string &library_error() const { return lib_error_; }
    const std::vector<LibraryAlbum> &albums() const { return albums_; }
    void library_rebuild();         // manual "ОБНОВИТЬ"
    // Ask for a fresh stats.db_update check on the next tick (screen open).
    void library_check();
    void play_album(int index);     // clear + add all tracks + play
    void play_all();                // clear + every indexed track + play

private:
    enum Kind { KIND_SIMPLE, KIND_QUEUE, KIND_PLAYLIST };

    struct Pending {
        std::string line;
        Kind kind;
    };

    struct CoverWant {
        std::string key, uri;
        bool large = false;
        unsigned at = 0;          // last time a screen asked for it
    };

    bool open_socket();
    void close_socket();
    void retry_later(unsigned now);
    void io_failed(unsigned now);
    bool send_line(const std::string &line);
    bool read_line(std::string &out);
    bool read_exact(size_t n, std::vector<uint8_t> &out);
    int read_response(std::vector<std::string> &lines); // 1 OK, 0 ACK, -1 I/O
    bool run_pending(unsigned now);
    bool fetch_status(unsigned now);
    bool fetch_current();
    bool fetch_queue();
    void enqueue(const std::string &line, Kind kind = KIND_SIMPLE);
    void enqueue_list(const std::vector<std::string> &cmds);
    void demo_fill();
    void demo_step(int delta);

    // idle socket (second connection, read from tick without blocking)
    void idle_tick(unsigned now);
    void idle_close();
    bool idle_open(unsigned now);
    void idle_send(const std::string &line);
    void idle_read(unsigned now);
    void idle_event(const std::string &name);

    // covers
    void cover_tick(unsigned now);
    bool cover_fetch(const std::string &uri, std::vector<uint8_t> &jpeg);
    bool cover_load_file(const std::string &key, CoverImage *big,
                         CoverImage *small);
    void cover_save_file(const std::string &key, const CoverImage &big,
                         const CoverImage &small);
    void cover_prune(unsigned now);
    void cover_remember(const std::string &key, const CoverImage &big,
                        const CoverImage &small, bool large);
    std::string cover_path(const std::string &key) const;

    // library
    void library_tick(unsigned now);
    bool fetch_stats(unsigned now);
    bool fetch_listallinfo(unsigned now);
    bool library_load_cache();
    void library_save_cache();
    std::string cache_dir() const;
    // resume
    void state_tick(unsigned now);
    void state_save();
    std::string config_dir() const;

    int fd_ = -1;
    bool connected_ = false;
    bool demo_ = false;
    unsigned now_ = 0;
    unsigned next_connect_ = 0;
    unsigned last_status_ = 0;
    unsigned status_at_ = 0;
    unsigned demo_t0_ = 0;
    int fail_count_ = 0;
    bool need_status_ = true;
    bool queue_loaded_ = false;
    bool queue_dirty_ = true;
    bool watch_queue_ = false;
    int last_queue_version_ = -1;
    std::string rbuf_;
    std::string error_;
    std::deque<Pending> pending_;
    MpdStatus status_;
    MpdSong song_;
    std::vector<MpdSong> queue_;
    std::vector<MpdSong> pl_songs_;

    // idle
    int idle_fd_ = -1;
    unsigned next_idle_connect_ = 0;
    std::string idle_buf_;
    bool idle_waiting_ = false;   // "idle" command is outstanding
    int idle_events_ = 0;         // counter of received events (debug/UI)

    // covers
    std::vector<CoverWant> cover_want_;
    std::map<std::string, CoverImage> cover_big_;
    std::map<std::string, CoverImage> cover_small_;
    std::deque<std::string> cover_big_lru_;
    std::deque<std::string> cover_small_lru_;
    std::map<std::string, unsigned> cover_fail_;     // key -> retry-after ms
    std::map<std::string, unsigned> cover_tried_;    // key -> cache read time
    unsigned cover_pruned_ = 0;

    // library
    bool watch_library_ = false;
    unsigned lib_watch_at_ = 0;   // last time the library screen asked
    bool lib_started_ = false;
    bool lib_need_stats_ = true;
    bool lib_need_list_ = false;
    unsigned lib_check_ = 0;      // last db_update seen from MPD
    unsigned lib_cache_ver_ = 0;  // db_update stored in the cache file
    int lib_state_ = LIB_IDLE;
    int lib_songs_ = 0;
    std::string lib_error_;
    std::vector<LibraryAlbum> albums_;
    std::map<std::string, int> album_index_;

    // resume
    std::string state_uri_;
    double state_elapsed_ = 0.0;
    bool state_loaded_ = false;
    bool resume_done_ = false;
    bool state_dirty_ = false;
    unsigned state_saved_ = 0;
    std::string last_uri_;
};

// Shared singleton: one connection for player, queue, info and library.
MpdClient &mpd_client();
