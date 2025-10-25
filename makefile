CFLAGS=-Wall -Wextra -Wno-unused -xc -std=gnu11
DEBUG_FLAGS=-g -fsanitize=address -fsanitize=undefined
RELEASE_FLAGS=-O2 -flto

generate:
	gcc -g $(CFLAGS) -lpng -o build/embed_assets src/tools/embed_assets.c
	./build/embed_assets assets

debug:
	gcc $(DEBUG_FLAGS) $(CFLAGS) -o build/lamebar src/lamebar/unity_build.c

release:
	gcc $(RELEASE_FLAGS) $(CFLAGS) -o build/lamebar src/lamebar/unity_build.c
