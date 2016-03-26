#include "gl-render.hpp"
#include <SDL2/SDL.h>
#include <SDL2/SDL_opengl.h>

static SDL_Window * win;

void glr_draw(const std::vector<etherdream_point> & pts) {
    glClear(GL_COLOR_BUFFER_BIT);

    glBegin(GL_LINE_STRIP);

    for (const auto & pt : pts) {
        glColor3f(pt.r / 65536.0, pt.g / 65536.0, pt.b / 65536.0);
        glVertex2f(pt.x / 32768.0, pt.y / 32768.0);
    }

    glEnd();
    SDL_GL_SwapWindow(win);
}

void glr_init() {

    const int width = 600;

    win = SDL_CreateWindow("wplay",
                           SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
                           width, width,
                           SDL_WINDOW_OPENGL);

    SDL_GL_CreateContext(win);

    glEnable(GL_TEXTURE_2D);
    glViewport(0, 0, width, width);
    glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(0.0f, width, width, 0.0f, -1.0f, 1.0f);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    glTranslatef(width / 2, width / 2, 0);
    glScalef(width / -2, width / -2, 0);
}
