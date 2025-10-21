CFLAGS=-Wall -Wextra -Wno-unused -xc -std=c11
DEBUG_FLAGS=-g -fsanitize=address -fsanitize=undefined

generate:
	gcc -g $(CFLAGS) -lpng -o build/embed_assets src/tools/embed_assets.c
	./build/embed_assets assets

debug:
	gcc $(DEBUG_FLAGS) $(CFLAGS) -o build/lamebar src/lamebar/*.c src/base/*.c src/generated/*.c
