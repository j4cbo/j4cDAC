#include "ilda.hpp"
#include "etherdream.h"

#include <utility>
#include <algorithm>
#include <iostream>
#include <fstream>
#include <cassert>

enum State {
    STATE_BETWEEN_FRAMES = -1,
    STATE_ILDA_0 = 0,
    STATE_ILDA_1 = 1,
    STATE_ILDA_2 = 2,
    STATE_ILDA_3 = 3,
    STATE_ILDA_4 = 4,
    STATE_ILDA_5 = 5,
};

struct ILDAFile::Impl {

    Impl(const char * filename, bool do_repeat, const Palette & palette)
        : m_stream(filename),
          m_do_repeat(do_repeat),
          m_palette(palette) {

        if (!m_stream) {
            std::cerr << "failed to open " << filename << "\n";
            exit(1);
        }
    }

    static void calculate_intensity(etherdream_point & p) {
        p.i = std::max({p.r, p.g, p.b});
    }

    void ilda_palette_point(etherdream_point & p, uint16_t color) {
        if (color & 0x4000) {
            /* "Blanking" flag */
            p.r = 0;
            p.g = 0;
            p.b = 0;
            p.i = 0;
        } else {
            /* Palette index */
            p.r = m_palette.data[3 * color] << 8;
            p.g = m_palette.data[3 * color + 1] << 8;
            p.b = m_palette.data[3 * color + 2] << 8;
            calculate_intensity(p);
        }
    }

    void ilda_tc_point(etherdream_point & p, int r, int g, int b, int flags) {
        if (flags & 0x40) {
            /* "Blanking" flag */
            p.r = 0;
            p.g = 0;
            p.b = 0;
            p.i = 0;
        } else {
            p.r = r << 8;
            p.g = g << 8;
            p.b = b << 8;
            calculate_intensity(p);
        }
    }

    void require_read(uint8_t *buf, size_t n) {
        m_stream.read((char*)buf, n);
        if (m_stream.gcount() != n) {
            std::cerr << "unexpected EOF in ILDA file\n";
            exit(1);
        }
    }

    void read_header() {
        uint8_t buf[8];

        m_stream.read((char*)buf, 8);
        if (m_stream.gcount() == 0) {
            if (m_do_repeat) {
                m_stream.clear();
                m_stream.seekg(0);
                m_stream.read((char*)buf, 8);
            } else {
                std::cerr << "EOF reached\n";
                exit(0);
            }
        }

        if (m_stream.gcount() != 8) {
            std::cerr << "unexpected EOF in ILDA file\n";
            exit(1);
        }

        if (memcmp(buf, "ILDA\0\0\0", 7) != 0) {
            std::cerr << "expected ILDA header\n";
            exit(1);
        }

        m_state = (State)buf[7];

        /* Read the rest of the header of this particular frame. */
        switch (m_state) {
        case STATE_ILDA_0:
        case STATE_ILDA_1:
        case STATE_ILDA_4:
        case STATE_ILDA_5:
            {
            /* 2D, 3D, and formats 4 and 5 */

            /* Throw away "frame name" and "company name" */
            require_read(buf, 8);
            require_read(buf, 8);

            /* Read the rest of the header */
            require_read(buf, 8);

            /* The rest of the header contains total points, frame number,
             * total frames, scanner head, and "future". We only care about
             * the first. */
            int npoints = buf[0] << 8 | buf[1];

            std::cout << "frame - " << npoints << " points\n";
            m_points_left = npoints;

            if (!npoints) {
                m_state = STATE_BETWEEN_FRAMES;
            }
            }
            break;

        default:
            std::cerr << "ILDA: bad format " << m_state << "\n";
            exit(1);
        }
    }

#define ILDA_MAX_POINTS_PER_LOOP    2000

    int do_read_points(int points, std::vector<etherdream_point> & point_buf) {
        int i;

        uint8_t ilda_buffer[16 * ILDA_MAX_POINTS_PER_LOOP];

        /* Now that we have some actual data, read from the file. */
        if (points > m_points_left)
            points = m_points_left;

        if (points > ILDA_MAX_POINTS_PER_LOOP)
            points = ILDA_MAX_POINTS_PER_LOOP;

        std::cout << m_points_left << " left, reading up to " << points << "\n";

        point_buf.resize(points);

        switch (m_state) {
            case STATE_ILDA_0:
                /* 3D w/ palette */
                require_read(ilda_buffer, 8 * points);
                for (i = 0; i < points; i++) {
                    uint8_t *b = ilda_buffer + 8*i;
                    point_buf[i].x = b[0] << 8 | b[1];
                    point_buf[i].y = b[2] << 8 | b[3];
                    ilda_palette_point(point_buf[i], b[6] << 8 | b[7]);
                }

                break;

            case STATE_ILDA_1:
                /* 2D w/ palette */
                require_read(ilda_buffer, 6 * points);
                for (i = 0; i < points; i++) {
                    uint8_t *b = ilda_buffer + 6*i;
                    point_buf[i].x = b[0] << 8 | b[1];
                    point_buf[i].y = b[2] << 8 | b[3];
                    ilda_palette_point(point_buf[i], b[4] << 8 | b[5]);
                }
                break;

            case STATE_ILDA_4:
                /* 3D truecolor */
                require_read(ilda_buffer, 10 * points);
                for (i = 0; i < points; i++) {
                    uint8_t *b = ilda_buffer + 10*i;
                    point_buf[i].x = b[0] << 8 | b[1];
                    point_buf[i].y = b[2] << 8 | b[3];
                    ilda_tc_point(point_buf[i], b[9], b[8], b[7], b[6]);
                }
                break;

            case STATE_ILDA_5:
                /* 2D truecolor */
                require_read(ilda_buffer, 8 * points);
                for (i = 0; i < points; i++) {
                    uint8_t *b = ilda_buffer + 8*i;
                    point_buf[i].x = b[0] << 8 | b[1];
                    point_buf[i].y = b[2] << 8 | b[3];
                    ilda_tc_point(point_buf[i], b[7], b[6], b[5], b[4]);
                }
                break;

            default:
                std::cerr << "bad m_state\n";
                exit(1);
        }

        /* Now that we've read points, advance */
        m_points_left -= points;

        /* Do we need to move to the next frame, or repeat this one? */
        if (!m_points_left) {
            m_state = STATE_BETWEEN_FRAMES;
        }

        return points;
}

    std::ifstream m_stream;
    bool m_do_repeat;
    const Palette & m_palette;
    State m_state = STATE_BETWEEN_FRAMES;
    int m_points_left;
};

ILDAFile::ILDAFile(const char * filename, bool do_repeat, const Palette & palette)
    : m_impl(std::make_unique<Impl>(filename, do_repeat, palette))
      {}

ILDAFile::~ILDAFile() = default;

size_t ILDAFile::read(size_t max,
                      std::vector<etherdream_point> & point_buf) {

    std::cout << "read(" << max << ")\n";

    point_buf.clear();

    while (m_impl->m_state == STATE_BETWEEN_FRAMES) {
        m_impl->read_header();
    }

    return m_impl->do_read_points(max, point_buf);
}

const ILDAFile::Palette ILDAFile::Palette::ilda64 = { {
    255,   0,   0, 255,  16,   0, 255,  32,   0, 255,  48,   0,
    255,  64,   0, 255,  80,   0, 255,  96,   0, 255, 112,   0,
    255, 128,   0, 255, 144,   0, 255, 160,   0, 255, 176,   0,
    255, 192,   0, 255, 208,   0, 255, 224,   0, 255, 240,   0,
    255, 255,   0, 224, 255,   0, 192, 255,   0, 160, 255,   0,
    128, 255,   0,  96, 255,   0,  64, 255,   0,  32, 255,   0,
      0, 255,   0,   0, 255,  32,   0, 255,  64,   0, 255,  96,
      0, 255, 128,   0, 255, 160,   0, 255, 192,   0, 255, 224,
      0, 130, 255,   0, 114, 255,   0, 104, 255,  10,  96, 255,
      0,  82, 255,   0,  74, 255,   0,  64, 255,   0,  32, 255,
      0,   0, 255,  32,   0, 255,  64,   0, 255,  96,   0, 255,
    128,   0, 255, 160,   0, 255, 192,   0, 255, 224,   0, 255,
    255,   0, 255, 255,  32, 255, 255,  64, 255, 255,  96, 255,
    255, 128, 255, 255, 160, 255, 255, 192, 255, 255, 224, 255,
    255, 255, 255, 255, 224, 224, 255, 192, 192, 255, 160, 160,
    255, 128, 128, 255,  96,  96, 255,  64,  64, 255,  32,  32
} };

const ILDAFile::Palette ILDAFile::Palette::ilda256 = { {
      0,   0,   0, 255, 255, 255, 255,   0,   0, 255, 255,   0,
      0, 255,   0,   0, 255, 255,   0,   0, 255, 255,   0, 255,
    255, 128, 128, 255, 140, 128, 255, 151, 128, 255, 163, 128,
    255, 174, 128, 255, 186, 128, 255, 197, 128, 255, 209, 128,
    255, 220, 128, 255, 232, 128, 255, 243, 128, 255, 255, 128,
    243, 255, 128, 232, 255, 128, 220, 255, 128, 209, 255, 128,
    197, 255, 128, 186, 255, 128, 174, 255, 128, 163, 255, 128,
    151, 255, 128, 140, 255, 128, 128, 255, 128, 128, 255, 140,
    128, 255, 151, 128, 255, 163, 128, 255, 174, 128, 255, 186,
    128, 255, 197, 128, 255, 209, 128, 255, 220, 128, 255, 232,
    128, 255, 243, 128, 255, 255, 128, 243, 255, 128, 232, 255,
    128, 220, 255, 128, 209, 255, 128, 197, 255, 128, 186, 255,
    128, 174, 255, 128, 163, 255, 128, 151, 255, 128, 140, 255,
    128, 128, 255, 140, 128, 255, 151, 128, 255, 163, 128, 255,
    174, 128, 255, 186, 128, 255, 197, 128, 255, 209, 128, 255,
    220, 128, 255, 232, 128, 255, 243, 128, 255, 255, 128, 255,
    255, 128, 243, 255, 128, 232, 255, 128, 220, 255, 128, 209,
    255, 128, 197, 255, 128, 186, 255, 128, 174, 255, 128, 163,
    255, 128, 151, 255, 128, 140, 255,   0,   0, 255,  23,   0,
    255,  46,   0, 255,  70,   0, 255,  93,   0, 255, 116,   0,
    255, 139,   0, 255, 162,   0, 255, 185,   0, 255, 209,   0,
    255, 232,   0, 255, 255,   0, 232, 255,   0, 209, 255,   0,
    185, 255,   0, 162, 255,   0, 139, 255,   0, 116, 255,   0,
     93, 255,   0,  70, 255,   0,  46, 255,   0,  23, 255,   0,
      0, 255,   0,   0, 255,  23,   0, 255,  46,   0, 255,  70,
      0, 255,  93,   0, 255, 116,   0, 255, 139,   0, 255, 162,
      0, 255, 185,   0, 255, 209,   0, 255, 232,   0, 255, 255,
      0, 232, 255,   0, 209, 255,   0, 185, 255,   0, 162, 255,
      0, 139, 255,   0, 116, 255,   0,  93, 255,   0,  70, 255,
      0,  46, 255,   0,  23, 255,   0,   0, 255,  23,   0, 255,
     46,   0, 255,  70,   0, 255,  93,   0, 255, 116,   0, 255,
    139,   0, 255, 162,   0, 255, 185,   0, 255, 209,   0, 255,
    232,   0, 255, 255,   0, 255, 255,   0, 232, 255,   0, 209,
    255,   0, 185, 255,   0, 162, 255,   0, 139, 255,   0, 116,
    255,   0,  93, 255,   0,  70, 255,   0,  46, 255,   0,  23,
    128,  0,   0, 128,  12,   0, 128,  23,   0, 128,  35,   0,
    128,  47,   0, 128,  58,   0, 128,  70,   0, 128,  81,   0,
    128,  93,   0, 128, 105,   0, 128, 116,   0, 128, 128,   0,
    116, 128,   0, 105, 128,   0,  93, 128,   0,  81, 128,   0,
     70, 128,   0,  58, 128,   0,  47, 128,   0,  35, 128,   0,
     23, 128,   0,  12, 128,   0,   0, 128,   0,   0, 128,  12,
      0, 128,  23,   0, 128,  35,   0, 128,  47,   0, 128,  58,
      0, 128,  70,   0, 128,  81,   0, 128,  93,   0, 128, 105,
      0, 128, 116,   0, 128, 128,   0, 116, 128,   0, 105, 128,
      0,  93, 128,   0,  81, 128,   0,  70, 128,   0,  58, 128,
      0,  47, 128,   0,  35, 128,   0,  23, 128,   0,  12, 128,
      0,   0, 128,  12,   0, 128,  23,   0, 128,  35,   0, 128,
     47,   0, 128,  58,   0, 128,  70,   0, 128,  81,   0, 128,
     93,   0, 128, 105,   0, 128, 116,   0, 128, 128,   0, 128,
    128,   0, 116, 128,   0, 105, 128,   0,  93, 128,   0,  81,
    128,   0,  70, 128,   0,  58, 128,   0,  47, 128,   0,  35,
    128,   0,  23, 128,   0,  12, 255, 192, 192, 255,  64,  64,
    192,   0,   0,  64,   0,   0, 255, 255, 192, 255, 255,  64,
    192, 192,   0,  64,  64,   0, 192, 255, 192,  64, 255,  64,
      0, 192,   0,   0,  64,   0, 192, 255, 255,  64, 255, 255,
      0, 192, 192,   0,  64,  64, 192, 192, 255,  64,  64, 255,
      0,   0, 192,   0,   0,  64, 255, 192, 255, 255,  64, 255,
    192,   0, 192,  64,   0,  64, 255,  96,  96, 255, 255, 255,
    245, 245, 245, 235, 235, 235, 224, 224, 224, 213, 213, 213,
    203, 203, 203, 192, 192, 192, 181, 181, 181, 171, 171, 171,
    160, 160, 160, 149, 149, 149, 139, 139, 139, 128, 128, 128,
    117, 117, 117, 107, 107, 107,  96,  96,  96,  85,  85,  85,
     75,  75,  75,  64,  64,  64,  53,  53,  53,  43,  43,  43,
     32,  32,  32,  21,  21,  21,  11,  11,  11,   0,   0,   0
} };
