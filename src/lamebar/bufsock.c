#include "bufsock.h"
#include "../base/arena.h"
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>
#include <string.h>

BufSock
bufsock_create(int fd)
{
	BufSock sock = { .fd = fd };
	sock.rcv_arena = arena_create();
	sock.snd_arena = arena_create();
	sock.rcv_pos = sock.rcv_arena.base;
	return sock;
}

bool
bufsock_has_data(BufSock *bs)
{
	if (bs->rcv_pos != bs->rcv_arena.pos)
		return true;
	struct pollfd pfd = { .fd = bs->fd, .events = POLLIN };
	int ret = poll(&pfd, 1, 0);
	return (ret > 0) && (pfd.revents & POLLIN);
}

u64
bufsock_fill(BufSock *bs)
{
	arena_clear(&bs->rcv_arena);
	bs->rcv_pos = arena_pos(&bs->rcv_arena);
	i64 len = recv(bs->fd, bs->rcv_pos, ARENA_SIZE, 0);
	arena_push_raw(&bs->rcv_arena, len, 1);
	return len;
}

void*
bufsock_put(BufSock *bs, const void *data, u64 len)
{
	void *ptr = arena_push_raw(&bs->snd_arena, len, 1);
	memcpy(ptr, data, len);
	return ptr;
}

void*
bufsock_get(BufSock *bs, u64 len)
{
	if (len == 0)
		return 0;
	if (bs->rcv_pos == bs->rcv_arena.pos)
		bufsock_fill(bs);
	void *ptr = bs->rcv_pos;
	bs->rcv_pos += len;
	return ptr;
}

u64
bufsock_flush(BufSock *bs)
{
	void *base = bs->snd_arena.base;
	u64 size = bs->snd_arena.pos - base;
	if (size > 0)
		send(bs->fd, base, size, 0);
	arena_clear(&bs->snd_arena);
	return size;
}

u64
bufsock_flush_with_fd(BufSock *bs, int extra_fd)
{
	void *base = bs->snd_arena.base;
	u64 size = bs->snd_arena.pos - base;
	struct iovec iov = {
		.iov_base = base,
		.iov_len  = size,
	};
	u8 control[CMSG_SPACE(sizeof(extra_fd))];
	memset(control, 0, sizeof(control));
	struct msghdr msgh = {0};
	msgh.msg_iov = &iov;
	msgh.msg_iovlen = 1;
	msgh.msg_control = control;
	msgh.msg_controllen = sizeof(control);
	struct cmsghdr *cmsg = CMSG_FIRSTHDR(&msgh);
	cmsg->cmsg_len   = CMSG_LEN(sizeof(extra_fd));
	cmsg->cmsg_level = SOL_SOCKET;
	cmsg->cmsg_type  = SCM_RIGHTS;
	*((int*) CMSG_DATA(cmsg)) = extra_fd;
	sendmsg(bs->fd, &msgh, 0);
	arena_clear(&bs->snd_arena);
	return size;
}
