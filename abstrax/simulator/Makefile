SRCS = \
	../../firmware/lib/fixpoint.c \
	../../firmware/abstract/io.c \
	../../firmware/abstract/render.c \
	main.c

SDL_FLAGS = -framework SDL -framework Cocoa -framework OpenGL -I/Library/Frameworks/SDL.framework/Headers

abstrax: $(SRCS) SDLmain.m
	gcc $(SRCS) SDLmain.m -I../../firmware/inc -I../../common -Wall $(SDL_FLAGS) -o $@

clean:
	rm -f abstrax
