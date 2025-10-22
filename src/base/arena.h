#pragma once
#include "base.h"

#define ARENA_SIZE GB(4)

typedef struct { void *base, *pos; } Arena;

Arena arena_create(void);
void arena_destroy(Arena *arena);

static void arena_clear(Arena *arena);
static void *arena_push_raw(Arena *arena, u64 size, u64 align);
static void *arena_pos(Arena *arena);
static char *arena_push_str(Arena *arena, const char *str);

#define arena_push(a, t) ((t*)arena_push_raw(a, sizeof(t), alignof(t)))
#define arena_push_arr(a, t, count) ((t*)arena_push_raw(a, sizeof(t) * count, alignof(t)))

/******************************************************************************/
/*                           inline implementations                           */
/******************************************************************************/

static void
arena_clear(Arena *arena) { 
	arena->pos = arena->base; 
}

static void*
arena_push_raw(Arena *arena, u64 size, u64 align)
{
	arena->pos = (void*)align_up((u64)arena->pos, align);
	void *result = arena->pos;
	arena->pos += size;
	return result;
}

static void*
arena_pos(Arena *arena) 
{ 
	return arena->pos; 
}

static char*
arena_push_str(Arena *arena, const char *str)
{
	char *result = arena->pos;
	while (*str) *(char*)arena->pos++ = *str++;
	*(char*)arena->pos++ = '\0';
	return result;
}
