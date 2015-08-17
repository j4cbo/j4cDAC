/*
 * benzene laser synthesis library
 * Copyright (c) 2015 Jacob Potter, All rights reserved.
 *
 * This library is free software; you can redistribute it and/or modify it under the terms of
 * the GNU Lesser General Public License as published by the Free Software Foundation,
 * version 2.1.
 *
 * This library is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY;
 * without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU Lesser General Public License for more details.
 *
 * The License can be found in lgpl-2.1.txt or at https://www.gnu.org/licenses/lgpl-2.1.html
 */

#include <SDL2/SDL.h>

#include "gl-render.hpp"
#include "wav8.hpp"

#include <iostream>
#include <fstream>
#include <thread>
#include <cassert>
#include <deque>

void usage(const char * argv0) {
    std::cerr << "Usage: " << argv0 << " file.wav\n";
    exit(1);
}

class AudioOutput {
public:
    using sample = std::pair<int16_t, int16_t>; // note: pair is standard-layout

    explicit AudioOutput(double rate) {
        SDL_AudioSpec want = {}, have = {};
        want.freq = rate;
        want.format = AUDIO_S16SYS;
        want.channels = 2;
        want.samples = 1024;
        want.callback = callback;
        want.userdata = &m_audio_buf;

        m_dev = SDL_OpenAudioDevice(nullptr, false, &want, &have, 0);
        if (m_dev == 0) {
            std::cerr << "failed to open audio device\n";
            exit(1);
        }
    }

    void set_paused(bool state) {
        SDL_PauseAudioDevice(m_dev, state);
    }

    void add_samples(const std::vector<sample> & vec) {
        SDL_LockAudioDevice(m_dev);
        m_audio_buf.insert(m_audio_buf.cend(), vec.cbegin(), vec.cend());
        SDL_UnlockAudioDevice(m_dev);
    }

private:
    std::deque<sample> m_audio_buf;
    SDL_AudioDeviceID m_dev;

    static void callback(void * user_data, uint8_t * stream_raw, int len) {
        auto buf = reinterpret_cast<sample *>(stream_raw);
        auto deque = reinterpret_cast<std::deque<sample> *>(user_data);
        for (int i = 0; i < (len / sizeof(sample)); i++) {
            if (!deque->empty()) {
                buf[i] = deque->front();
                deque->pop_front();
            } else {
                buf[i] = {};
            }
        }
    }
};

int main(int argc, char **argv) {
    if (argc != 2) {
        usage(argv[0]);
    }

    if (!std::ifstream(argv[1], std::ios::binary)) {
        usage(argv[0]);
    }

    if (SDL_Init(SDL_INIT_AUDIO) < 0) {
        exit(1);
    }

    if (etherdream_lib_start() < 0) {
        exit(1);
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(1100));

    WAV8File f(argv[1]);

    glr_init();
    auto t = std::chrono::steady_clock::now();

    AudioOutput ao(f.get_rate());
    ao.set_paused(false);

    std::vector<etherdream_point> point_buf;
    std::vector<std::pair<int16_t, int16_t>> audio_buf;

    etherdream * ed = nullptr;
    if (etherdream_dac_count() > 0) {
        ed = etherdream_get(0);
        etherdream_connect(ed);
    }

    while (!SDL_QuitRequested()) {
        /* Handle SDL events */
        SDL_Event event;
        while (SDL_PollEvent(&event)) { }

        size_t pts = f.read(1600, point_buf, audio_buf);
        if (!pts) {
            return 0;
        }

        ao.add_samples(audio_buf);

        glr_draw(point_buf);
        if (ed) {
            for (int i = 0; i < point_buf.size(); i++) {
                point_buf[i].x /= 2;
                point_buf[i].y /= 2;
            }
            etherdream_write(ed, point_buf.data(), point_buf.size(), f.get_rate(), -1);
        }

        t += std::chrono::steady_clock::duration(std::chrono::seconds(1)) / int(f.get_rate() / pts);
        std::this_thread::sleep_until(t);
    }

    return 0;
}
