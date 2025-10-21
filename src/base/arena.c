#include "arena.h"
#include "sys/mman.h"
#include <stdio.h>
#include <stdlib.h>

typedef struct ArenaNode ArenaNode;
struct ArenaNode { ArenaNode *next; };
static ArenaNode *arena_freelist = NULL;

Arena arena_create(void)
{
	if (arena_freelist != NULL) {
		ArenaNode *node = arena_freelist;
		arena_freelist = node->next;
		return (Arena){.base = node, .pos = node };
	}
	int prot = PROT_READ | PROT_WRITE;
	int flags = MAP_ANONYMOUS | MAP_PRIVATE;
	void *buf = mmap(0, ARENA_SIZE, prot, flags, -1, 0);
	if (buf == MAP_FAILED) {
		perror("failed to mmap");
		exit(1);
	}
	return (Arena){.base = buf, .pos = buf };
}

void arena_destroy(Arena *arena)
{
	ArenaNode *node = (ArenaNode*)arena->base;
	node->next = arena_freelist;
	arena_freelist = node;
}
