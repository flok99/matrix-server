CXXFLAGS=-Wall -O3 -ggdb3 -fno-strict-aliasing -std=c++0x -Iinclude `pkg-config --cflags freetype2` `pkg-config --cflags jansson` `pkg-config --cflags fontconfig` -fno-omit-frame-pointer -fsanitize=address
LDFLAGS+=-Llib -ggdb3 -lrgbmatrix -lrt -lm -pthread `pkg-config --libs freetype2` `pkg-config --libs jansson` `pkg-config --libs fontconfig`

all : matrix-server

lib/librgbmatrix.a:
	$(MAKE) -C lib

font-test: error.o font.o utils.o
	g++ error.o font.o utils.o `pkg-config --libs freetype2` `pkg-config --libs fontconfig` -pthread

matrix-server: error.o matrix-server.o lib/librgbmatrix.a utils.o font.o
	$(CXX) $(CXXFLAGS) error.o matrix-server.o utils.o font.o -o $@ $(LDFLAGS)

%.o : %.cc
	$(CXX) $(CXXFLAGS) -DADAFRUIT_RGBMATRIX_HAT -c -o $@ $<

clean:
	rm -f *.o $(OBJECTS) matrix-server
	$(MAKE) -C lib clean
