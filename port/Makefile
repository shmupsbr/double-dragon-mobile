CC ?= gcc
WINCC ?= x86_64-w64-mingw32-gcc
CFLAGS ?= -std=c11 -Wall -Wextra -O2
SRCS = src/ddpak.c src/ddobm.c src/ddtext.c
PAK ?= ../investigation/cache1327/res.pak

.PHONY: all test win clean

all: build/ddpak build/test_resources

build:
	mkdir -p build

build/ddpak: src/ddpak_main.c $(SRCS) | build
	$(CC) $(CFLAGS) -Iinclude -o $@ src/ddpak_main.c $(SRCS)

build/test_resources: tests/test_resources.c $(SRCS) | build
	$(CC) $(CFLAGS) -Iinclude -o $@ tests/test_resources.c $(SRCS)

test: build/test_resources build/ddpak
	./build/test_resources $(PAK)
	./build/ddpak text $(PAK) res/Language_EN/ddiPhoneAchieve_En.dat | head -n 3

win: | build
	$(WINCC) $(CFLAGS) -Iinclude -static -o build/ddpak.exe src/ddpak_main.c $(SRCS)
	$(WINCC) $(CFLAGS) -Iinclude -static -o build/test_resources.exe tests/test_resources.c $(SRCS)

clean:
	rm -rf build
