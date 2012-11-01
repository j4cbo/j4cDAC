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

#include <sys/socket.h>
#include <netinet/in.h>

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

void dump_state_to_stdout(void) {
	char statebuf[300];
	dump_state(statebuf, sizeof(statebuf));
	printf("--> %s\n", statebuf);
}

char abstract_prefix[] = "/abstract/conf\0\0,s\0";

void run(int udpfd) {
	/* Handle SDL events */
	SDL_Event event;
	while (SDL_PollEvent(&event)) {
		switch (event.type) {
		default:
			break;
		}
	}

	/* Check for input from stdin or OSC */
	int got_something = 0;
	do {
		char buf[300];

		got_something = 0;
		struct timeval tv;
		fd_set fds;
		tv.tv_sec = 0;
		tv.tv_usec = 0;
		FD_ZERO(&fds);
		FD_SET(0, &fds);
		FD_SET(udpfd, &fds);
		select(udpfd + 1, &fds, NULL, NULL, &tv);

		if (FD_ISSET(0, &fds)) {
			fgets(buf, sizeof buf, stdin);
			abs_parse_line(buf);
			got_something = 1;
		}

		if (FD_ISSET(udpfd, &fds)) {
			int len = recv(udpfd, buf, sizeof buf, 0);
			if (len < 0) {
				perror("recv");
			} else if (buf[len - 1]) {
				printf("Bogus OSC data\n");
			} else if (memcmp(buf, abstract_prefix, sizeof abstract_prefix)) {
				printf("Bogus OSC data\n");
			} else {
				abs_parse_line(buf + sizeof abstract_prefix);
				got_something = 1;
			}
		}

		if (got_something)
			dump_state_to_stdout();

	} while(got_something);

	glClear(GL_COLOR_BUFFER_BIT);
	renderPoints();
	SDL_GL_SwapBuffers();
	SDL_Delay(1000 / FPS);
}

int main(int argc, char **argv) {
	init(450);

	glTranslatef(225, 225, 0);

	int udpfd = socket(PF_INET, SOCK_DGRAM, 0);

	struct sockaddr_in addr;
	memset(&addr, 0, sizeof addr);
	addr.sin_family = AF_INET;
	addr.sin_port = htons(60000);
	addr.sin_addr.s_addr = htonl(INADDR_ANY);
	if (bind(udpfd, (struct sockaddr *)&addr, sizeof addr) < 0) {
		perror("bind");
		exit(1);
	}

	while (!SDL_QuitRequested())
		run(udpfd);

	return 0;
}
