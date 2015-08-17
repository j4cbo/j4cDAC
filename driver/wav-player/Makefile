SRCS = wplay.cpp gl-render.cpp wav8.cpp ../libetherdream/etherdream.c

CFLAGS := -I../../common -I../libetherdream

FLAGS = $(CFLAGS) -F/Library/Frameworks -framework SDL2 -framework Cocoa -framework OpenGL -I/usr/local/include -L/usr/local/lib -laudiofile

wplay: $(SRCS)
	$(CXX) -std=c++1y $(SRCS) -I../src -Wall $(FLAGS) -o $@

.PHONY: clean
clean:
	rm -f wplay
