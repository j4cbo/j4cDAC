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

#include <getopt.h>
#include <iostream>
#include <fstream>
#include <thread>
#include <cassert>
#include <deque>

const double MIN_SIZE = 0.1;

void usage(const char * argv0) {
    std::cerr << "Usage: " << argv0 << " file.wav [options]\n";
    std::cerr << "Options:\n";
    std::cerr << "\t-x-size size          Scale the X axis. Range: 0.1 to 1\n";
    std::cerr << "\t-y-size size          Scale the Y axis. Range: 0.1 to 1\n";
    std::cerr << "\t-x-offset offset      Offset the X axis. Range: -1 to 1. This is applied\n";
    std::cerr << "\t                      relative to the scale factor (offseting an image with\n";
    std::cerr << "\t                      size 1 has no effect).\n";
    std::cerr << "\t-y-offset offset      Offset the Y axis. Similar to -x-offset.\n";
    std::cerr << "\t-x-flip               Flip the X axis.\n";
    std::cerr << "\t-y-flip               Flip the Y axis.\n";
    std::cerr << "\t-brightness level     Scale all colors by multiplying by level. 1.0 for full\n";
    std::cerr << "\t                      brightness, 0.5 for half power, etc.\n";
    std::cerr << "\t-seek pos             Seek partway through the file before beginning playback\n";
    std::cerr << "\t                      (0.5 for 50%% through, etc).\n";
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

enum opt : int {
    FLIP_X,
    FLIP_Y,
    SIZE_X,
    OFFSET_X,
    SIZE_Y,
    OFFSET_Y,
    SEEK,
    BRIGHTNESS,
};

static option opts[] = {
    { "x-flip", no_argument, nullptr, opt::FLIP_X },
    { "y-flip", no_argument, nullptr, opt::FLIP_Y },
    { "x-size", required_argument, nullptr, opt::SIZE_X },
    { "y-size", required_argument, nullptr, opt::SIZE_Y },
    { "x-offset", required_argument, nullptr, opt::OFFSET_X },
    { "y-offset", required_argument, nullptr, opt::OFFSET_Y },
    { "seek", required_argument, nullptr, opt::SEEK },
    { "brightness", required_argument, nullptr, opt::BRIGHTNESS },
    {}
};

static bool valid_size(double size) {
    return (size <= 1) && !(size < MIN_SIZE && size > -MIN_SIZE) && (size >= -1);
}

static bool valid_offset(double offset) {
    return (offset <= 1) && (offset >= -1);
}

int main(int argc, char **argv) {

    bool do_flip_x = false, do_flip_y = false;
    double x_size = 0.5, y_size = 0.5, x_rel_offset = 0, y_rel_offset = 0;
    double initial_seek = 0;
    double brightness = 1;

    int flag;
    while ((flag = getopt_long_only(argc, argv, "", opts, nullptr)) != -1) {
        switch (flag) {
        case opt::FLIP_X:
            do_flip_x = true;
            break;
        case opt::FLIP_Y:
            do_flip_y = true;
            break;
        case opt::SIZE_X:
            x_size = strtod(optarg, nullptr);
            if (!valid_size(x_size)) {
                std::cerr << "x-size must be between " << MIN_SIZE << " and 1\n";
                return 1;
            }
            break;
        case opt::SIZE_Y:
            y_size = strtod(optarg, nullptr);
            if (!valid_size(y_size)) {
                std::cerr << "y-size must be between " << MIN_SIZE << " and 1\n";
                return 1;
            }
            break;
        case opt::OFFSET_X:
            x_rel_offset = strtod(optarg, nullptr);
            if (!valid_offset(x_rel_offset)) {
                std::cerr << "x-offset must be between -1 and 1\n";
                return 1;
            }
            break;
        case opt::OFFSET_Y:
            y_rel_offset = strtod(optarg, nullptr);
            if (!valid_offset(y_rel_offset)) {
                std::cerr << "y-offset must be between -1 and 1\n";
                return 1;
            }
            break;
        case opt::BRIGHTNESS:
            brightness = strtod(optarg, nullptr);
            if (brightness < 0 || brightness > 1) {
                std::cerr << "brightness must be between 0 and 1\n";
                return 1;
            }
            break;
        case opt::SEEK:
            initial_seek = strtod(optarg, nullptr);
            if (initial_seek < 0 || initial_seek > 1) {
                std::cerr << "seek must be between 0 and 1\n";
                return 1;
            }
            break;
        case '?':
            usage(argv[0]);
        default:
            break;
        }
    }

    double x_offset = x_rel_offset * (1 - fabs(x_size));
    double y_offset = y_rel_offset * (1 - fabs(y_size));
    if (do_flip_x) {
        x_size = -x_size;
    }
    if (do_flip_y) {
        y_size = -y_size;
    }

    if (argc - optind != 1) {
        usage(argv[0]);
    }

    if (!std::ifstream(argv[optind], std::ios::binary)) {
        usage(argv[0]);
    }

    if (SDL_Init(SDL_INIT_AUDIO) < 0) {
        exit(1);
    }

    if (etherdream_lib_start() < 0) {
        exit(1);
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(1100));

    WAV8File f(argv[optind], initial_seek);

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

        for (int i = 0; i < point_buf.size(); i++) {
            point_buf[i].x = point_buf[i].x * x_size + (x_offset * 32767);
            point_buf[i].y = point_buf[i].y * y_size + (y_offset * 32767);
            point_buf[i].r *= brightness;
            point_buf[i].g *= brightness;
            point_buf[i].b *= brightness;
        }

        if (ed) {
            // If an Ether Dream is attached, let it drive timing.
            etherdream_wait_for_ready(ed);
            etherdream_write(ed, point_buf.data(), point_buf.size(), f.get_rate(), 1);
        } else {
            // If not, use a fixed clock.
            t += std::chrono::steady_clock::duration(std::chrono::seconds(1)) / int(f.get_rate() / pts);
            std::this_thread::sleep_until(t);
        }
    }

    return 0;
}
