#define _GNU_SOURCE
#include "wl.h"
#include <stdlib.h>
#include <string.h>
#include <sys/un.h>
#include <sys/socket.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <sys/poll.h>
#include <unistd.h>
#include <assert.h>
#include "../base/arena.h"
#include "../base/log.h"

static struct {
	int fd;
	u32 next_id;
	Arena rcv_arena, snd_arena;
	void *rcv_pos;
	int shm_fd;
	PixelBuf fb;
	u32 scale;
	// object ids
	u32 surface, buffer, shm, shm_pool, layer_surface;
} wl;

typedef struct { u32 obj; u16 opcode, size; } WL_Hdr;

static void
wl_connect(void)
{
	char *filename = getenv("WAYLAND_DISPLAY");
	if (!filename) filename = "wayland-0";
	char *dirname = getenv("XDG_RUNTIME_DIR");
	log_assert(dirname, "XDG_RUNTIME_DIR not set");
	struct sockaddr_un addr = { .sun_family = AF_UNIX };
	addr.sun_path[0] = '\0';
	strcat(addr.sun_path, dirname);
	strcat(addr.sun_path, "/");
	strcat(addr.sun_path, filename);
	wl.fd = socket(AF_UNIX, SOCK_STREAM, 0);
	int ret = connect(wl.fd, (void*)&addr, sizeof(addr));
	log_assert(ret >= 0, "failed to connect to wayland socket");
}

static void
wl_fill_rcvbuf(void)
{
	arena_clear(&wl.rcv_arena);
	wl.rcv_pos = arena_pos(&wl.rcv_arena);
	i64 len = recv(wl.fd, wl.rcv_pos, ARENA_SIZE, 0);
	arena_push_raw(&wl.rcv_arena, len, 1);
}

static void*
wl_recv(u64 len)
{
	if (len == 0) return 0;
	if (wl.rcv_pos == arena_pos(&wl.rcv_arena)) wl_fill_rcvbuf();
	void *ptr = wl.rcv_pos;
	wl.rcv_pos += len;
	return ptr;
}

static u32 wl_recv_u32(void) { return *(u32*)wl_recv(sizeof(u32)); }
static WL_Hdr wl_recv_hdr(void) { return *(WL_Hdr*)wl_recv(sizeof(WL_Hdr)); }
static char* wl_recv_str(void) { u32 len = wl_recv_u32(); return wl_recv(align_up(len, 4)); }
static bool wl_has_data(void) { 
	if (wl.rcv_pos != arena_pos(&wl.rcv_arena))
		return true;
	struct pollfd pfd = { .fd = wl.fd, .events = POLLIN };
	int ret = poll(&pfd, 1, 0);
	return (ret > 0) && (pfd.revents & POLLIN);
}

static WL_Hdr*
wl_msg_begin(u32 obj, u16 opcode) {
	WL_Hdr *hdr = arena_push(&wl.snd_arena, WL_Hdr);
	hdr->obj = obj;
	hdr->opcode = opcode;
	hdr->size = sizeof(WL_Hdr);
	return hdr;
}

static void*
wl_msg_push(WL_Hdr *hdr, const void *data, u64 len)
{
	void *ptr = arena_push_raw(&wl.snd_arena, len, 1);
	memcpy(ptr, data, len);
	hdr->size += len;
	return ptr;
}

static void wl_msg_push_u32(WL_Hdr *hdr, u32 data) { wl_msg_push(hdr, &data, sizeof(u32)); }
static void wl_msg_push_str(WL_Hdr *hdr, const char *str) {
	u64 len = strlen(str) + 1;
	wl_msg_push_u32(hdr, len);
	wl_msg_push(hdr, str, align_up(len, 4));
}

static void
wl_flush(void)
{
	void *base = wl.snd_arena.base;
	u64 size = wl.snd_arena.pos - base;
	if (size > 0) send(wl.fd, base, size, 0);
	arena_clear(&wl.snd_arena);
}

static void
wl_flush_with_fd(int extra_fd)
{
	void *base = wl.snd_arena.base;
	u64 size = wl.snd_arena.pos - base;
	log_assert(size > 0, "wl_flush_with_fd() called with empty buffer");
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

	// Copy the fd into the ancillary data
	*((int*) CMSG_DATA(cmsg)) = extra_fd;

	sendmsg(wl.fd, &msgh, 0);
	arena_clear(&wl.snd_arena);
}

static void*
wl_create_shared_memory(u32 size, int *out_fd)
{
	u64 page_size = sysconf(_SC_PAGESIZE);
	size = align_up(size, page_size);

	int fd = memfd_create("lamebar-shm", MFD_CLOEXEC);
	ftruncate(fd, size);
	
	// reserve address space so the buffer can grow without moving
	void *reserved = mmap(NULL, GB(4), PROT_NONE, MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
	log_assert(reserved != MAP_FAILED, "failed to mmap shared memory");

	// map the buffer to the file descriptor
	// NOTE: this will overwrite the reserved address space
	//       which is what we want
	void *ptr = mmap(reserved, size, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_FIXED, fd, 0);
	log_assert(ptr != MAP_FAILED, "failed to mmap shared memory");

	*out_fd = fd;
	return ptr;
}

static void
wl_grow_shared_memory(int fd, void *ptr, u64 old_size, u64 new_size)
{
	log_assert(old_size < new_size, "tried to grow shared memory to smaller size");

	// make sure sizes are page-aligned
	u64 page_size = sysconf(_SC_PAGESIZE);
	old_size = align_up(old_size, page_size);
	new_size = align_up(new_size, page_size);

	// resize the underlying memory
	ftruncate(fd, new_size);

	// map the new memory
	// NOTE: we want to only map the "extension" of the new memory
	//       instead of the entire new size, doing it the other way
	//       would work, but it would destroy the page table and
	//       flush the TLB
	//       additionally, mremap() cannot be used because it can't
	//       overwrite existing mappings and we're keeping the
	//       anonymous mapping around to "reserve" the address space
	void *ext_ptr = ptr + old_size;
	u64 ext_size = new_size - old_size;
	void *result = mmap(ext_ptr, ext_size, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_FIXED, fd, old_size);
	log_assert(result != MAP_FAILED, "failed to mmap shared memory");
	log_assert(result == ext_ptr, "mmap did not return the expected address");
}

static void
wl_drain_events(void)
{
	while (wl_has_data()) {
		WL_Hdr hdr = wl_recv_hdr();
		if (hdr.obj != 1 || hdr.opcode != 0) {
			wl_recv(hdr.size - sizeof(hdr));
			continue;
		}
		u32 obj_id = wl_recv_u32();
		u32 errcode = wl_recv_u32();
		char *msg = wl_recv_str();
		log_crash("wayland error (%d) (obj=%d): %s", errcode, obj_id, msg);
	}
}

/***************************** protocol messages ******************************/

static u32
wl_display_get_registry(void)
{
	u32 id = wl.next_id++;
	WL_Hdr *hdr = wl_msg_begin(1, 1);
	wl_msg_push_u32(hdr, id);
	return id;
}

static u32
wl_registry_bind(u32 self, u32 name, const char *iface, u32 version)
{
	u32 id = wl.next_id++;
	WL_Hdr *hdr = wl_msg_begin(self, 0);
	wl_msg_push_u32(hdr, name);
	wl_msg_push_str(hdr, iface);
	wl_msg_push_u32(hdr, version);
	wl_msg_push_u32(hdr, id);
	return id;
}

static u32
wl_compositor_create_surface(u32 self)
{
	u32 id = wl.next_id++;
	WL_Hdr *hdr = wl_msg_begin(self, 0);
	wl_msg_push_u32(hdr, id);
	return id;
}

static u32
zwlr_layer_shell_v1_get_layer_surface(u32 self, u32 surface)
{
	u32 id = wl.next_id++;
	WL_Hdr *hdr = wl_msg_begin(self, 0);
	wl_msg_push_u32(hdr, id);
	wl_msg_push_u32(hdr, surface);
	wl_msg_push_u32(hdr, 0); // output
	wl_msg_push_u32(hdr, 3); // layer=overlay
	wl_msg_push_str(hdr, "lamebar");
	return id;
}

static void
zwlr_layer_surface_v1_set_size(u32 self, u32 w, u32 h)
{
	WL_Hdr *hdr = wl_msg_begin(self, 0);
	wl_msg_push_u32(hdr, w);
	wl_msg_push_u32(hdr, h);
}

typedef u32 WL_Anchor;
enum {
	WL_ANCHOR_TOP = (1 << 0),
	WL_ANCHOR_BOTTOM = (1 << 1),
	WL_ANCHOR_LEFT = (1 << 2),
	WL_ANCHOR_RIGHT = (1 << 3),
};

static void
zwlr_layer_surface_v1_set_anchor(u32 self, WL_Anchor anchor)
{
	WL_Hdr *hdr = wl_msg_begin(self, 1);
	wl_msg_push_u32(hdr, anchor);
}

static void
zwlr_layer_surface_v1_ack_configure(u32 self, u32 serial)
{
	WL_Hdr *hdr = wl_msg_begin(self, 6);
	wl_msg_push_u32(hdr, serial);
}

static u32
wl_shm_create_pool(u32 self, int mem_fd, u32 size)
{
	u32 id = wl.next_id++;
	WL_Hdr *hdr = wl_msg_begin(self, 0);
	wl_msg_push_u32(hdr, id);
	wl_msg_push_u32(hdr, size);
	wl_flush_with_fd(mem_fd);
	return id;
}

typedef u32 WL_Format;
enum {
	WL_FORMAT_ARGB8888 = 0,
	WL_FORMAT_XRGB8888 = 1,
};

static u32
wl_shm_pool_create_buffer(u32 self, u32 offset, u32 w, u32 h, u32 stride, WL_Format format)
{
	u32 id = wl.next_id++;
	WL_Hdr *hdr = wl_msg_begin(self, 0);
	wl_msg_push_u32(hdr, id);
	wl_msg_push_u32(hdr, offset);
	wl_msg_push_u32(hdr, w);
	wl_msg_push_u32(hdr, h);
	wl_msg_push_u32(hdr, stride);
	wl_msg_push_u32(hdr, format);
	return id;
}

static void
wl_shm_pool_destroy(u32 self)
{
	wl_msg_begin(self, 1);
}

static void
wl_buffer_destroy(u32 self)
{
	wl_msg_begin(self, 0);
}

static void
wl_surface_commit(u32 self)
{
	wl_msg_begin(self, 6);
}

static void
wl_surface_attach(u32 self, u32 buffer, u32 x, u32 y)
{
	WL_Hdr *hdr = wl_msg_begin(self, 1);
	wl_msg_push_u32(hdr, buffer);
	wl_msg_push_u32(hdr, x);
	wl_msg_push_u32(hdr, y);
}

static void
wl_surface_damage_buffer(u32 self, u32 x, u32 y, u32 w, u32 h)
{
	WL_Hdr *hdr = wl_msg_begin(self, 9);
	wl_msg_push_u32(hdr, x);
	wl_msg_push_u32(hdr, y);
	wl_msg_push_u32(hdr, w);
	wl_msg_push_u32(hdr, h);
}

/******************************************************************************/

static void
wl_bind_interfaces(u32 registry, u32 *compositor, u32 *layer_shell, u32 *shm)
{
	wl_fill_rcvbuf();
	while (wl_has_data()) {
		WL_Hdr hdr = wl_recv_hdr();
		if (hdr.obj != registry || hdr.opcode != 0) {
			wl_recv(hdr.size - sizeof(hdr)); // skip
			continue;
		}
		u32 name = wl_recv_u32();
		char *iface = wl_recv_str();
		u32 version = wl_recv_u32();
		if (strcmp(iface, "wl_compositor") == 0)
			*compositor = wl_registry_bind(registry, name, iface, version);
		else if (strcmp(iface, "zwlr_layer_shell_v1") == 0)
			*layer_shell = wl_registry_bind(registry, name, iface, version);
		else if (strcmp(iface, "wl_shm") == 0)
			*shm = wl_registry_bind(registry, name, iface, version);
	}
	log_assert(compositor != 0, "your compositor does not support wl_compositor (how?)");
	log_assert(layer_shell != 0, "your compositor does not support zwlr_layer_shell_v1"); 
	log_assert(shm != 0, "your compositor does not support wl_shm");
}

static u32
wl_wait_for_configure(u32 layer_surface)
{
	while (true) {
		WL_Hdr hdr = wl_recv_hdr();
		if (hdr.obj == layer_surface && hdr.opcode == 0) {
			u32 serial = wl_recv_u32();
			wl_recv_u32(); // width
			wl_recv_u32(); // height
			return serial;
		}
		wl_recv(hdr.size - sizeof(hdr)); // skip
	}
}

static void
wl_ensure_fb_size(u32 w, u32 h)
{
	u64 old_size = wl.fb.w * wl.fb.h * sizeof(Pixel);
	u64 new_size = w * h * sizeof(Pixel);
	if (old_size >= new_size) return;
	wl_buffer_destroy(wl.buffer);
	wl_shm_pool_destroy(wl.shm_pool);
	wl_grow_shared_memory(wl.shm_fd, wl.fb.data, old_size, new_size);
	wl.fb.w = w;
	wl.fb.h = h;
	wl.shm_pool = wl_shm_create_pool(wl.shm, wl.shm_fd, new_size);

	u32 stride = w * sizeof(Pixel);
	wl.buffer = wl_shm_pool_create_buffer(wl.shm_pool, 0, w, h, stride, WL_FORMAT_ARGB8888);

	zwlr_layer_surface_v1_set_size(wl.layer_surface, w, h);
	zwlr_layer_surface_v1_set_anchor(wl.layer_surface, WL_ANCHOR_TOP | WL_ANCHOR_RIGHT);
	wl_surface_commit(wl.surface);
	wl_flush();
	u32 serial = wl_wait_for_configure(wl.layer_surface);
	zwlr_layer_surface_v1_ack_configure(wl.layer_surface, serial);
}

/********************************* public api *********************************/

void wl_init(u32 scale) {
	wl.scale = scale;
	wl.next_id = 2; // 0=NULL, 1=display
	wl.rcv_arena = arena_create();
	wl.snd_arena = arena_create();
	wl.rcv_pos = arena_pos(&wl.rcv_arena);

	wl_connect();

	u32 registry = wl_display_get_registry();
	wl_flush();
	u32 compositor = 0, layer_shell = 0;
	wl_bind_interfaces(registry, &compositor, &layer_shell, &wl.shm);

	wl.surface = wl_compositor_create_surface(compositor);
	wl.layer_surface = zwlr_layer_shell_v1_get_layer_surface(layer_shell, wl.surface);
	// TODO: this should probably be parameterized somehow
	u32 width = 100;
	u32 height = 5;
	zwlr_layer_surface_v1_set_size(wl.layer_surface, width * scale, height * scale);
	zwlr_layer_surface_v1_set_anchor(wl.layer_surface, WL_ANCHOR_TOP | WL_ANCHOR_RIGHT);
	wl_surface_commit(wl.surface);
	wl_flush();
	u32 serial = wl_wait_for_configure(wl.layer_surface);
	zwlr_layer_surface_v1_ack_configure(wl.layer_surface, serial);

	wl.fb.w = width * scale;
	wl.fb.h = height * scale;
	u64 framebuf_size = wl.fb.w * wl.fb.h * sizeof(Pixel);
	wl.fb.data = wl_create_shared_memory(framebuf_size, &wl.shm_fd);
	memset(wl.fb.data, 0, framebuf_size);
	wl.shm_pool = wl_shm_create_pool(wl.shm, wl.shm_fd, framebuf_size);
	u32 stride = wl.fb.w * sizeof(Pixel);
	wl.buffer = wl_shm_pool_create_buffer(wl.shm_pool, 0, wl.fb.w, wl.fb.h, stride, WL_FORMAT_ARGB8888);
	wl_surface_attach(wl.surface, wl.buffer, 0, 0);
	wl_surface_damage_buffer(wl.surface, 0, 0, wl.fb.w, wl.fb.h);
	wl_surface_commit(wl.surface);
	wl_flush();
	wl_drain_events();
}

void wl_hide(void) {
	memset(wl.fb.data, 0, wl.fb.w * wl.fb.h * sizeof(Pixel));
	wl_surface_attach(wl.surface, wl.buffer, 0, 0);
	wl_surface_damage_buffer(wl.surface, 0, 0, wl.fb.w, wl.fb.h);
	wl_surface_commit(wl.surface);
	wl_flush();
}

void
wl_show(PixelBuf pixels)
{
	wl_ensure_fb_size(pixels.w * wl.scale, pixels.h * wl.scale);
	log_assert(pixels.w <= wl.fb.w && pixels.h <= wl.fb.h, "frame is too large for the framebuffer");
	wl_flush();
	wl_drain_events();
	memset(wl.fb.data, 0, wl.fb.w * wl.fb.h * sizeof(Pixel));
	pixelbuf_copy(wl.fb, pixels, wl.fb.w - pixels.w * wl.scale, 0, wl.scale);
	wl_surface_attach(wl.surface, wl.buffer, 0, 0);
	wl_surface_damage_buffer(wl.surface, 0, 0, wl.fb.w, wl.fb.h);
	wl_surface_commit(wl.surface);
	wl_flush();
}
