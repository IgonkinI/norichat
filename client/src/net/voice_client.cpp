#include "voice_client.h"

// Include miniaudio declarations (implementation is in miniaudio_impl.cpp).
#include <miniaudio.h>

#include <algorithm>
#include <cassert>
#include <cstdio>
#include <cstring>

// ─── Base64 helpers ───────────────────────────────────────────────────────────

static const char B64_CHARS[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

static std::string b64_encode(const uint8_t* data, size_t len) {
    std::string out;
    out.reserve(((len + 2) / 3) * 4);
    for (size_t i = 0; i < len; i += 3) {
        uint32_t b = static_cast<uint32_t>(data[i]) << 16;
        if (i + 1 < len) b |= static_cast<uint32_t>(data[i + 1]) << 8;
        if (i + 2 < len) b |= static_cast<uint32_t>(data[i + 2]);
        out += B64_CHARS[(b >> 18) & 0x3f];
        out += B64_CHARS[(b >> 12) & 0x3f];
        out += (i + 1 < len) ? B64_CHARS[(b >> 6) & 0x3f] : '=';
        out += (i + 2 < len) ? B64_CHARS[b & 0x3f]        : '=';
    }
    return out;
}

static std::vector<int16_t> b64_decode_pcm(const std::string& s) {
    // Lookup table: ASCII → 6-bit value, -1 = skip
    static const int8_t T[256] = {
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,62,-1,-1,-1,63,
        52,53,54,55,56,57,58,59,60,61,-1,-1,-1,-1,-1,-1,
        -1, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9,10,11,12,13,14,
        15,16,17,18,19,20,21,22,23,24,25,-1,-1,-1,-1,-1,
        -1,26,27,28,29,30,31,32,33,34,35,36,37,38,39,40,
        41,42,43,44,45,46,47,48,49,50,51,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
    };

    std::vector<uint8_t> bytes;
    bytes.reserve(s.size() * 3 / 4);
    uint32_t buf = 0;
    int      bits = 0;
    for (unsigned char c : s) {
        int v = T[c];
        if (v < 0) continue;
        buf  = (buf << 6) | static_cast<uint32_t>(v);
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            bytes.push_back(static_cast<uint8_t>(buf >> bits));
            buf &= (1u << bits) - 1u;
        }
    }

    // Reinterpret raw bytes as int16_t samples (little-endian, as captured)
    const size_t n_samples = bytes.size() / 2;
    std::vector<int16_t> samples(n_samples);
    if (n_samples > 0)
        std::memcpy(samples.data(), bytes.data(), n_samples * 2);
    return samples;
}

// ─── VoiceClient ──────────────────────────────────────────────────────────────

VoiceClient::VoiceClient()  = default;
VoiceClient::~VoiceClient() { stop(); }

bool VoiceClient::start(int channel_id, FrameCallback on_frame) {
    stop(); // ensure clean state

    channel_id_ = channel_id;
    on_frame_   = std::move(on_frame);

    // ── Capture device ───────────────────────────────────────────────────────
    cap_dev_ = new ma_device{};
    ma_device_config cap_cfg = ma_device_config_init(ma_device_type_capture);
    cap_cfg.capture.format   = ma_format_s16;
    cap_cfg.capture.channels = 1;
    cap_cfg.sampleRate       = static_cast<ma_uint32>(SAMPLE_RATE);
    cap_cfg.dataCallback     = VoiceClient::capture_cb;
    cap_cfg.pUserData        = this;

    if (ma_device_init(nullptr, &cap_cfg, cap_dev_) != MA_SUCCESS) {
        fprintf(stderr, "[voice] failed to init capture device\n");
        delete cap_dev_; cap_dev_ = nullptr;
        return false;
    }

    // ── Playback device ──────────────────────────────────────────────────────
    play_dev_ = new ma_device{};
    ma_device_config play_cfg  = ma_device_config_init(ma_device_type_playback);
    play_cfg.playback.format   = ma_format_s16;
    play_cfg.playback.channels = 1;
    play_cfg.sampleRate        = static_cast<ma_uint32>(SAMPLE_RATE);
    play_cfg.dataCallback      = VoiceClient::playback_cb;
    play_cfg.pUserData         = this;

    if (ma_device_init(nullptr, &play_cfg, play_dev_) != MA_SUCCESS) {
        fprintf(stderr, "[voice] failed to init playback device\n");
        ma_device_uninit(cap_dev_);
        delete cap_dev_;  cap_dev_  = nullptr;
        delete play_dev_; play_dev_ = nullptr;
        return false;
    }

    ma_device_start(cap_dev_);
    ma_device_start(play_dev_);
    active_ = true;
    fprintf(stdout, "[voice] started, channel=%d\n", channel_id_);
    return true;
}

void VoiceClient::stop() {
    if (!active_) return;
    active_ = false;

    if (cap_dev_) {
        ma_device_stop(cap_dev_);
        ma_device_uninit(cap_dev_);
        delete cap_dev_;
        cap_dev_ = nullptr;
    }
    if (play_dev_) {
        ma_device_stop(play_dev_);
        ma_device_uninit(play_dev_);
        delete play_dev_;
        play_dev_ = nullptr;
    }
    {
        std::lock_guard<std::mutex> lk(cap_mutex_);
        cap_buf_.clear();
    }
    {
        std::lock_guard<std::mutex> lk(play_mutex_);
        play_buf_.clear();
    }
    channel_id_ = -1;
    fprintf(stdout, "[voice] stopped\n");
}

void VoiceClient::play_frame(const std::string& b64_pcm) {
    auto samples = b64_decode_pcm(b64_pcm);
    std::lock_guard<std::mutex> lk(play_mutex_);
    for (int16_t s : samples)
        play_buf_.push_back(s);
    // Trim buffer to ~500 ms to prevent unbounded growth under packet loss
    constexpr size_t MAX_BUFFERED = SAMPLE_RATE / 2; // 8000 samples
    while (play_buf_.size() > MAX_BUFFERED)
        play_buf_.pop_front();
}

// ─── Audio callbacks (called from miniaudio audio threads) ───────────────────

void VoiceClient::capture_cb(ma_device* dev, void* /*out*/, const void* in,
                             unsigned int frame_count) {
    auto* self = static_cast<VoiceClient*>(dev->pUserData);
    if (!self || !self->active_) return;

    const int16_t* samples = static_cast<const int16_t*>(in);

    std::lock_guard<std::mutex> lk(self->cap_mutex_);
    for (unsigned int i = 0; i < frame_count; ++i)
        self->cap_buf_.push_back(samples[i]);

    // Fire callback for each complete 20 ms frame
    while (static_cast<int>(self->cap_buf_.size()) >= FRAME_SAMPLES) {
        const uint8_t* raw = reinterpret_cast<const uint8_t*>(self->cap_buf_.data());
        std::string b64 = b64_encode(raw, static_cast<size_t>(FRAME_SAMPLES) * 2);
        self->cap_buf_.erase(self->cap_buf_.begin(),
                             self->cap_buf_.begin() + FRAME_SAMPLES);
        if (self->on_frame_)
            self->on_frame_(b64, self->channel_id_);
    }
}

void VoiceClient::playback_cb(ma_device* dev, void* out, const void* /*in*/,
                              unsigned int frame_count) {
    auto* self = static_cast<VoiceClient*>(dev->pUserData);
    int16_t* dst = static_cast<int16_t*>(out);

    if (!self || !self->active_) {
        std::memset(out, 0, frame_count * sizeof(int16_t));
        return;
    }

    std::lock_guard<std::mutex> lk(self->play_mutex_);
    for (unsigned int i = 0; i < frame_count; ++i) {
        if (!self->play_buf_.empty()) {
            dst[i] = self->play_buf_.front();
            self->play_buf_.pop_front();
        } else {
            dst[i] = 0; // underrun → silence
        }
    }
}
