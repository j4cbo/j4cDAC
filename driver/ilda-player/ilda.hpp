#pragma once

#include <memory>
#include <vector>
#include "etherdream.h"

class ILDAFile {
public:
    struct Palette {
        static const Palette ilda64;
        static const Palette ilda256;

        const uint8_t data[256*3];
    };

    ILDAFile(const char * filename,
             bool do_repeat,
             const Palette & palette = Palette::ilda64);
    ~ILDAFile();

    double get_rate() const { return 30000; /* ew */ }

    size_t read(size_t max,
                std::vector<etherdream_point> & point_buf);

private:
    struct Impl;
    const std::unique_ptr<Impl> m_impl;
};
