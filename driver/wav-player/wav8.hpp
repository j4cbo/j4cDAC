#pragma once

#include <memory>
#include <vector>

struct etherdream_point;

class WAV8File {
public:
    WAV8File(const char * filename, double initial_seek = 0);
    ~WAV8File();

    double get_rate() const { return m_rate; }

    size_t read(size_t max,
                std::vector<etherdream_point> & point_buf,
                std::vector<std::pair<int16_t, int16_t>> & audio_buf);

private:
    struct Impl;
    const std::unique_ptr<Impl> m_impl;
    const double m_rate;
};
