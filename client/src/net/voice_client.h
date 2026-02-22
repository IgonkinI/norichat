#pragma once
#include <cstdint>
#include <deque>
#include <functional>
#include <mutex>
#include <string>
#include <vector>

// Forward-declare miniaudio device type so we can store pointers without
// pulling the heavy single-header into every translation unit.
// The actual ma_device struct is defined in miniaudio.h, included only in
// voice_client.cpp and miniaudio_impl.cpp.
struct ma_device;

// ─── VoiceClient ──────────────────────────────────────────────────────────────
// Captures microphone audio and plays back received audio using miniaudio.
// Audio format: 16 kHz, mono, int16_t PCM.
// Each captured frame is ~20 ms (320 samples = 640 bytes), encoded as base64
// and passed to the FrameCallback for transmission over WebSocket.

class VoiceClient {
public:
    // Called from the audio capture thread with a base64-encoded PCM frame
    // and the voice channel id.  Must be fast and non-blocking.
    using FrameCallback = std::function<void(const std::string& b64_pcm, int ch_id)>;

    VoiceClient();
    ~VoiceClient();

    // Start capture for `channel_id`.  `on_frame` is called whenever a 20 ms
    // frame of audio is ready to send.  Returns false on device error.
    bool start(int channel_id, FrameCallback on_frame);

    // Stop capture and playback and release audio devices.
    void stop();

    // Queue base64-encoded PCM from the server for playback.  Thread-safe.
    void play_frame(const std::string& b64_pcm);

    bool is_active()        const { return active_; }
    int  voice_channel_id() const { return channel_id_; }

private:
    // miniaudio device callbacks – called on dedicated audio threads.
    static void capture_cb(ma_device* dev, void* out, const void* in,
                           unsigned int frame_count);
    static void playback_cb(ma_device* dev, void* out, const void* in,
                            unsigned int frame_count);

    ma_device*    cap_dev_   = nullptr;
    ma_device*    play_dev_  = nullptr;
    bool          active_    = false;
    int           channel_id_ = -1;
    FrameCallback on_frame_;

    static constexpr int SAMPLE_RATE   = 16000;
    static constexpr int FRAME_SAMPLES = 320;  // 20 ms @ 16 kHz, mono

    std::mutex           cap_mutex_;
    std::vector<int16_t> cap_buf_;   // accumulates captured samples

    std::mutex           play_mutex_;
    std::deque<int16_t>  play_buf_;  // buffered samples for playback
};
