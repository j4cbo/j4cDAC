/* j4cDAC abstract generator tester
 *
 * Copyright 2011 Jacob Potter
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, version 3.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <SDL/SDL.h>
#include <SDL/SDL_opengl.h>
#include <sys/select.h>
#include <math.h>
#include "fixpoint.h"
#include <protocol.h>
#include "render.h"

SDL_Surface * screen;

void abs_parse_line(char *line);
void dump_state(char *out, int len);

void init(int width) {
	if (SDL_Init(SDL_INIT_VIDEO) < 0) {
		exit(-1);
	}

	screen = SDL_SetVideoMode(width, width, 24,
		SDL_OPENGL | SDL_GL_DOUBLEBUFFER);

	glEnable(GL_TEXTURE_2D);
	glViewport(0, 0, width, width);
	glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
	glClear(GL_COLOR_BUFFER_BIT);
	glMatrixMode(GL_PROJECTION);
	glLoadIdentity();
	glOrtho(0.0f, width, width, 0.0f, -1.0f, 1.0f);
	glMatrixMode(GL_MODELVIEW);
	glLoadIdentity();
}

struct dac_point buffer[PERSIST_POINTS] = { { 0 } };
int buffer_pos;

void renderPoints() {
	glBegin(GL_LINE_STRIP);
	int i;

	for (i = 0; i < POINTS_PER_FRAME; i++) {
		get_next_point(&buffer[buffer_pos]);
		buffer_pos = (buffer_pos + 1) % PERSIST_POINTS;
	}

	for (i = 0; i < PERSIST_POINTS; i++) {
		struct dac_point * p = &buffer[(buffer_pos + i) % PERSIST_POINTS];
		glColor3f(p->r / 65535.0, p->g / 65535.0, p->b / 65535.0);
		glVertex2i(p->x / 150, p->y / 150);
	}
	glEnd();
}

int line_available(void) {
	struct timeval tv;
	fd_set fds;
	tv.tv_sec = 0;
	tv.tv_usec = 0;
	FD_ZERO(&fds);
	FD_SET(0, &fds);
	select(1, &fds, NULL, NULL, &tv);
	return FD_ISSET(0, &fds);
}

int main(int argc, char **argv) {
	init(450);

	glTranslatef(225, 225, 0);
	SDL_Event event;

	while (!SDL_QuitRequested()) {
		while (SDL_PollEvent(&event)) {
			switch (event.type) {
			default:
				break;
			}
		}

		while (line_available()) {
			char l[300];
			fgets(l, sizeof(l), stdin);
			abs_parse_line(l);

			char statebuf[300];
			dump_state(statebuf, sizeof(statebuf));
			printf("--> %s\n", statebuf);
		}

		glClear(GL_COLOR_BUFFER_BIT);
		renderPoints();
		SDL_GL_SwapBuffers();
		SDL_Delay(1000 / FPS);
	}

	return 0;
}
