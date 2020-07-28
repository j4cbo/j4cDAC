SRCS = play.cpp ilda.cpp ../libetherdream/etherdream.c

CFLAGS := -I../../common -I../libetherdream

FLAGS = $(CFLAGS)

play: $(SRCS)
	$(CXX) -std=c++1y $(SRCS) -I../src -Wall $(FLAGS) -o $@

.PHONY: clean
clean:
	rm -f play
