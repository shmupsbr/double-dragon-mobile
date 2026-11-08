CC ?= gcc
MINGW ?= x86_64-w64-mingw32-gcc

.PHONY: linux windows

linux: scene_view
windows: scene_view.exe

scene_view: scene_view.c
	$(CC) -O2 -Wall -Wextra -o $@ scene_view.c -lX11

scene_view.exe: scene_view.c
	$(MINGW) -O2 -Wall -Wextra -o $@ scene_view.c -lgdi32
