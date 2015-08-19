#include "wav8.hpp"
#include "etherdream.h"

#include <audiofile.h>

#include <utility>
#include <algorithm>
#include <iostream>
#include <cassert>

struct WAV8File::Impl {

    struct wav8_sample {
        int16_t x;
        int16_t y;
        int16_t red;
        int16_t green;
        int16_t blue;
        int16_t z;
        int16_t left;
        int16_t right;
    };

    void adjust_bias(int & num_positive, int & num_negative, int16_t sample) {
        static constexpr const int threshold = 8000;
        if (sample > threshold && sample < (INT16_MAX - threshold)) num_positive++;
        if (sample < -threshold && sample > (INT16_MIN + threshold)) num_negative++;
    }

    bool detect_rgb_invert() {

        // Read at most the first 30 seconds for autodetect purposes. (We need a fairly
        // long threshold, since some shows don't start until 10+ seconds in.)
        AFframecount frames_in_5_seconds = afGetRate(m_handle, AF_DEFAULT_TRACK) * 30;
        AFframecount frames_left = std::min(afGetFrameCount(m_handle, AF_DEFAULT_TRACK),
                                            frames_in_5_seconds);

        static constexpr const size_t buffer_size = 1000;
        int num_positive = 0, num_negative = 0;

        wav8_sample buf[buffer_size];

        while (frames_left > 0) {
            int n = std::min(frames_left, static_cast<AFframecount>(buffer_size));
            afReadFrames(m_handle, AF_DEFAULT_TRACK, buf, n);
            frames_left -= n;

            for (int i = 0; i < n; i++) {
                adjust_bias(num_positive, num_negative, buf[i].red);
                adjust_bias(num_positive, num_negative, buf[i].green);
                adjust_bias(num_positive, num_negative, buf[i].blue);
            }
        }

        afSeekFrame(m_handle, AF_DEFAULT_TRACK, 0);
        return (num_negative > num_positive);
    }

    static AFfilehandle open_and_check(const char * filename) {
        AFfilehandle handle = afOpenFile(filename, "r", AF_NULL_FILESETUP);
        if (handle == AF_NULL_FILEHANDLE) {
            std::cerr << "libaudiofile failed to open " << filename << "\n";
            exit(1);
        }

        return handle;
    }

    Impl(const char * filename)
        : m_handle(open_and_check(filename)) {

        afSetVirtualSampleFormat(m_handle, AF_DEFAULT_TRACK, AF_SAMPFMT_TWOSCOMP, 16);

        int channels = afGetChannels(m_handle, AF_DEFAULT_TRACK);
        if (channels != 8) {
            std::cerr << filename << " seems to have " << channels
                      << " channels. I don't know what to do with that.\n";
            exit(1);
        }

        m_rate = afGetRate(m_handle, AF_DEFAULT_TRACK);
        m_inverted = detect_rgb_invert();
    }

    const AFfilehandle m_handle;
    double m_rate;
    bool m_inverted;

    std::vector<wav8_sample> m_input_buf;
};

WAV8File::WAV8File(const char * filename)
    : m_impl(std::make_unique<Impl>(filename)),
      m_rate(m_impl->m_rate) {}

WAV8File::~WAV8File() = default;

size_t WAV8File::read(size_t max,
                      std::vector<etherdream_point> & point_buf,
                      std::vector<std::pair<int16_t, int16_t>> & audio_buf) {

    point_buf.clear();
    audio_buf.clear();
    m_impl->m_input_buf.resize(max);

    int frames_read = afReadFrames(m_impl->m_handle,
                                   AF_DEFAULT_TRACK,
                                   m_impl->m_input_buf.data(),
                                   max);
    assert(frames_read >= 0);

    int colorScale = m_impl->m_inverted ? -2 : 2;
    int xyScale = m_impl->m_inverted ? -1 : 1;

    for (int i = 0; i < frames_read; i++) {
        uint16_t r = std::max(0, int(m_impl->m_input_buf[i].red) * colorScale);
        uint16_t g = std::max(0, int(m_impl->m_input_buf[i].green) * colorScale);
        uint16_t b = std::max(0, int(m_impl->m_input_buf[i].blue) * colorScale);

        point_buf.emplace_back(etherdream_point{
            int16_t(m_impl->m_input_buf[i].x * xyScale),
            int16_t(m_impl->m_input_buf[i].y * xyScale),
            r, g, b, std::max({r, g, b}), 0, 0
        });

        audio_buf.emplace_back(m_impl->m_input_buf[i].left, m_impl->m_input_buf[i].right);
    }

    return frames_read;
}
