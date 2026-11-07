CC ?= gcc
WINCC ?= x86_64-w64-mingw32-gcc
CFLAGS ?= -std=c11 -Wall -Wextra -O2
SRCS = src/ddpak.c src/ddobm.c src/ddtext.c
PAK ?= ../investigation/cache1327/res.pak

.PHONY: all test win clean frames slum shot

all: build/ddpak build/test_resources build/test_sim build/ddslum

build:
	mkdir -p build assets

frames: assets/frames.bin

assets/frames.bin: tools/bake_frames.py $(PAK) | build
	python3 tools/bake_frames.py $(PAK) assets/frames.bin

build/ddpak: src/ddpak_main.c $(SRCS) | build
	$(CC) $(CFLAGS) -Iinclude -o $@ src/ddpak_main.c $(SRCS)

build/test_resources: tests/test_resources.c $(SRCS) | build
	$(CC) $(CFLAGS) -Iinclude -o $@ tests/test_resources.c $(SRCS)

build/test_sim: tests/test_sim.c src/sim.c | build
	$(CC) $(CFLAGS) -Iinclude -o $@ tests/test_sim.c src/sim.c

build/ddslum: src/game.c src/sim.c assets/frames.bin | build
	$(CC) $(CFLAGS) -Iinclude -o $@ src/game.c src/sim.c -lX11

test: build/test_resources build/ddpak build/test_sim
	./build/test_resources $(PAK)
	./build/ddpak text $(PAK) res/Language_EN/ddiPhoneAchieve_En.dat | head -n 3
	./build/test_sim

shot: build/ddslum
	./build/ddslum assets/frames.bin --dump build/slum.ppm --ticks 150

win: assets/frames.bin | build
	$(WINCC) $(CFLAGS) -Iinclude -static -o build/ddpak.exe src/ddpak_main.c $(SRCS)
	$(WINCC) $(CFLAGS) -Iinclude -static -o build/test_resources.exe tests/test_resources.c $(SRCS)
	$(WINCC) $(CFLAGS) -Iinclude -static -mwindows -o build/ddslum.exe src/game.c src/sim.c -lgdi32 -luser32

clean:
	rm -rf build
