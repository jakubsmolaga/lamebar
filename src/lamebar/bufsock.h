#pragma once
#include "../base/arena.h"

typedef struct BufSock BufSock;
struct BufSock {
	int fd;
	Arena rcv_arena, snd_arena;
	void *rcv_pos;
};

BufSock bufsock_create(int fd);
bool bufsock_has_data(BufSock *bs);
u64 bufsock_fill(BufSock *bs);

void *bufsock_put(BufSock *bs, const void *data, u64 len);
void *bufsock_get(BufSock *bs, u64 len);
u64 bufsock_flush(BufSock *bs);
u64 bufsock_flush_with_fd(BufSock *bs, int extra_fd);

