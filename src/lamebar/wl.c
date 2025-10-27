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
#include "../base/log.h"
#include "bufsock.h"

static struct {
	u32 next_id;
	BufSock bs;
	int shm_fd;
	PixelBuf fb;
	// object ids
	u32 display, compositor, surface, buffer, shm, shm_pool, layer_surface, empty_region;
} wl = {0};

typedef struct { u32 obj; u16 opcode, size; } WL_Hdr;

typedef u32 WL_Anchor;
enum {
	WL_ANCHOR_TOP = (1 << 0),
	WL_ANCHOR_BOTTOM = (1 << 1),
	WL_ANCHOR_LEFT = (1 << 2),
	WL_ANCHOR_RIGHT = (1 << 3),
};

typedef u32 WL_Format;
enum {
	WL_FORMAT_ARGB8888 = 0,
	WL_FORMAT_XRGB8888 = 1,
};

// request opcodes
static const u16 WL_DISPLAY_GET_REGISTRY = 1;
static const u16 WL_REGISTRY_BIND = 0;
static const u16 WL_COMPOSITOR_CREATE_SURFACE = 0;
static const u16 WL_COMPOSITOR_CREATE_REGION = 1;
static const u16 ZWLR_LAYER_SHELL_V1_GET_LAYER_SURFACE = 0;
static const u16 ZWLR_LAYER_SURFACE_V1_SET_SIZE = 0;
static const u16 ZWLR_LAYER_SURFACE_V1_SET_ANCHOR = 1;
static const u16 ZWLR_LAYER_SURFACE_V1_ACK_CONFIGURE = 6;
static const u16 WL_SHM_CREATE_POOL = 0;
static const u16 WL_SHM_POOL_CREATE_BUFFER = 0;
static const u16 WL_SHM_POOL_DESTROY = 1;
static const u16 WL_BUFFER_DESTROY = 0;
static const u16 WL_SURFACE_ATTACH = 1;
static const u16 WL_SURFACE_SET_INPUT_REGION = 5;
static const u16 WL_SURFACE_COMMIT = 6;
static const u16 WL_SURFACE_DAMAGE_BUFFER = 9;

// event opcodes
static const u16 WL_DISPLAY_ERROR = 0;
static const u16 WL_REGISTRY_GLOBAL = 0;
static const u16 ZWLR_LAYER_SURFACE_V1_CONFIGURE = 0;

/************************** message building helpers **************************/

static WL_Hdr*
wl_msg_begin(u32 obj, u16 opcode) {
	WL_Hdr hdr = { .obj = obj, .opcode = opcode, .size = sizeof(WL_Hdr) };
	return bufsock_put(&wl.bs, &hdr, sizeof(WL_Hdr));
}

static void*
wl_msg_push(WL_Hdr *hdr, const void *data, u64 len)
{
	void *ptr = bufsock_put(&wl.bs, data, len);
	hdr->size += len;
	return ptr;
}

static void
wl_msg_push_u32(WL_Hdr *hdr, u32 data)
{
	wl_msg_push(hdr, &data, sizeof(u32));
}

static void
wl_msg_push_str(WL_Hdr *hdr, const char *str)
{
	u64 len = strlen(str) + 1;
	wl_msg_push_u32(hdr, len);
	wl_msg_push(hdr, str, align_up(len, 4));
}

/***************************** protocol messages ******************************/

static u32
wl_display_get_registry(void)
{
	u32 id = wl.next_id++;
	WL_Hdr *hdr = wl_msg_begin(wl.display, WL_DISPLAY_GET_REGISTRY);
	wl_msg_push_u32(hdr, id);
	return id;
}

static u32
wl_registry_bind(u32 self, u32 name, const char *iface, u32 version)
{
	u32 id = wl.next_id++;
	WL_Hdr *hdr = wl_msg_begin(self, WL_REGISTRY_BIND);
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
	WL_Hdr *hdr = wl_msg_begin(self, WL_COMPOSITOR_CREATE_SURFACE);
	wl_msg_push_u32(hdr, id);
	return id;
}

static u32
wl_compositor_create_region(u32 self)
{
	u32 id = wl.next_id++;
	WL_Hdr *hdr = wl_msg_begin(self, WL_COMPOSITOR_CREATE_REGION);
	wl_msg_push_u32(hdr, id);
	return id;
}

static u32
zwlr_layer_shell_v1_get_layer_surface(u32 self, u32 surface)
{
	u32 id = wl.next_id++;
	WL_Hdr *hdr = wl_msg_begin(self, ZWLR_LAYER_SHELL_V1_GET_LAYER_SURFACE);
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
	WL_Hdr *hdr = wl_msg_begin(self, ZWLR_LAYER_SURFACE_V1_SET_SIZE);
	wl_msg_push_u32(hdr, w);
	wl_msg_push_u32(hdr, h);
}

static void
zwlr_layer_surface_v1_set_anchor(u32 self, WL_Anchor anchor)
{
	WL_Hdr *hdr = wl_msg_begin(self, ZWLR_LAYER_SURFACE_V1_SET_ANCHOR);
	wl_msg_push_u32(hdr, anchor);
}

static void
zwlr_layer_surface_v1_ack_configure(u32 self, u32 serial)
{
	WL_Hdr *hdr = wl_msg_begin(self, ZWLR_LAYER_SURFACE_V1_ACK_CONFIGURE);
	wl_msg_push_u32(hdr, serial);
}

static u32
wl_shm_create_pool(u32 self, int mem_fd, u32 size)
{
	u32 id = wl.next_id++;
	WL_Hdr *hdr = wl_msg_begin(self, WL_SHM_CREATE_POOL);
	wl_msg_push_u32(hdr, id);
	wl_msg_push_u32(hdr, size);
	bufsock_flush_with_fd(&wl.bs, mem_fd);
	return id;
}

static u32
wl_shm_pool_create_buffer(u32 self, u32 offset, u32 w, u32 h, u32 stride, WL_Format format)
{
	u32 id = wl.next_id++;
	WL_Hdr *hdr = wl_msg_begin(self, WL_SHM_POOL_CREATE_BUFFER);
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
	wl_msg_begin(self, WL_SHM_POOL_DESTROY);
}

static void
wl_buffer_destroy(u32 self)
{
	wl_msg_begin(self, WL_BUFFER_DESTROY);
}

static void
wl_surface_commit(u32 self)
{
	wl_msg_begin(self, WL_SURFACE_COMMIT);
}

static void
wl_surface_attach(u32 self, u32 buffer, u32 x, u32 y)
{
	WL_Hdr *hdr = wl_msg_begin(self, WL_SURFACE_ATTACH);
	wl_msg_push_u32(hdr, buffer);
	wl_msg_push_u32(hdr, x);
	wl_msg_push_u32(hdr, y);
}

static void
wl_surface_damage_buffer(u32 self, u32 x, u32 y, u32 w, u32 h)
{
	WL_Hdr *hdr = wl_msg_begin(self, WL_SURFACE_DAMAGE_BUFFER);
	wl_msg_push_u32(hdr, x);
	wl_msg_push_u32(hdr, y);
	wl_msg_push_u32(hdr, w);
	wl_msg_push_u32(hdr, h);
}

static void
wl_surface_set_input_region(u32 self, u32 region)
{
	WL_Hdr *hdr = wl_msg_begin(self, WL_SURFACE_SET_INPUT_REGION);
	wl_msg_push_u32(hdr, region);
}

/******************************** misc helpers ********************************/

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
	int fd = socket(AF_UNIX, SOCK_STREAM, 0);
	int ret = connect(fd, (void*)&addr, sizeof(addr));
	log_assert(ret >= 0, "failed to connect to wayland socket");
	wl.bs = bufsock_create(fd);
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
	if (old_size >= new_size) return;

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
	while (bufsock_has_data(&wl.bs)) {
		WL_Hdr hdr = *(WL_Hdr*)bufsock_get(&wl.bs, sizeof(WL_Hdr));
		if (hdr.obj != wl.display || hdr.opcode != WL_DISPLAY_ERROR) {
			bufsock_get(&wl.bs, hdr.size - sizeof(hdr));
			continue;
		}
		u32 obj_id = *(u32*)bufsock_get(&wl.bs, sizeof(u32));
		u32 errcode = *(u32*)bufsock_get(&wl.bs, sizeof(u32));
		u32 msg_len = *(u32*)bufsock_get(&wl.bs, sizeof(u32));
		char *msg = bufsock_get(&wl.bs, align_up(msg_len, 4));
		log_crash("wayland error (%d) (obj=%d): %s", errcode, obj_id, msg);
	}
}

static void
wl_bind_interfaces(u32 registry, u32 *compositor, u32 *layer_shell, u32 *shm)
{
	bufsock_fill(&wl.bs);
	while (bufsock_has_data(&wl.bs)) {
		WL_Hdr hdr = *(WL_Hdr*)bufsock_get(&wl.bs, sizeof(WL_Hdr));
		if (hdr.obj != registry || hdr.opcode != WL_REGISTRY_GLOBAL) {
			bufsock_get(&wl.bs, hdr.size - sizeof(hdr)); // skip
			continue;
		}
		u32 name = *(u32*)bufsock_get(&wl.bs, sizeof(u32));
		u32 iface_len = *(u32*)bufsock_get(&wl.bs, sizeof(u32));
		char *iface = bufsock_get(&wl.bs, align_up(iface_len, 4));
		u32 version = *(u32*)bufsock_get(&wl.bs, sizeof(u32));
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
		WL_Hdr hdr = *(WL_Hdr*)bufsock_get(&wl.bs, sizeof(WL_Hdr));
		if (hdr.obj == layer_surface && hdr.opcode == ZWLR_LAYER_SURFACE_V1_CONFIGURE) {
			u32 serial = *(u32*)bufsock_get(&wl.bs, sizeof(u32));
			bufsock_get(&wl.bs, sizeof(u32)); // width
			bufsock_get(&wl.bs, sizeof(u32)); // height
			return serial;
		}
		bufsock_get(&wl.bs, hdr.size - sizeof(hdr)); // skip
	}
}

static void
wl_ensure_fb_size(u32 w, u32 h)
{
	u64 old_size = wl.fb.w * wl.fb.h * sizeof(Pixel);
	u64 new_size = w * h * sizeof(Pixel);
	if (old_size >= new_size) return;

	if (wl.buffer)
		wl_buffer_destroy(wl.buffer);
	if (wl.shm_pool)
		wl_shm_pool_destroy(wl.shm_pool);

	if (!wl.fb.data || !wl.shm_fd)
		wl.fb.data = wl_create_shared_memory(new_size, &wl.shm_fd);
	wl_grow_shared_memory(wl.shm_fd, wl.fb.data, old_size, new_size);

	wl.fb.w = w;
	wl.fb.h = h;
	wl.shm_pool = wl_shm_create_pool(wl.shm, wl.shm_fd, new_size);

	u32 stride = w * sizeof(Pixel);
	u32 format = WL_FORMAT_ARGB8888;
	wl.buffer = wl_shm_pool_create_buffer(wl.shm_pool, 0, w, h, stride, format);

	if (!wl.empty_region)
		wl.empty_region = wl_compositor_create_region(wl.compositor);
	wl_surface_set_input_region(wl.surface, wl.empty_region);

	u32 anchor = WL_ANCHOR_TOP | WL_ANCHOR_RIGHT;
	zwlr_layer_surface_v1_set_size(wl.layer_surface, w, h);
	zwlr_layer_surface_v1_set_anchor(wl.layer_surface, anchor);
	wl_surface_commit(wl.surface);
	bufsock_flush(&wl.bs);

	u32 serial = wl_wait_for_configure(wl.layer_surface);
	zwlr_layer_surface_v1_ack_configure(wl.layer_surface, serial);
}

/********************************* public api *********************************/

void wl_init(void) {
	wl.display = 1;
	wl.next_id = 2; // 0=NULL, 1=display

	wl_connect();

	u32 registry = wl_display_get_registry();
	bufsock_flush(&wl.bs);

	u32 layer_shell = 0;
	wl_bind_interfaces(registry, &wl.compositor, &layer_shell, &wl.shm);

	wl.surface = wl_compositor_create_surface(wl.compositor);
	wl.layer_surface = zwlr_layer_shell_v1_get_layer_surface(layer_shell, wl.surface);

	bufsock_flush(&wl.bs);
	wl_drain_events();
}

void wl_hide(void) {
	memset(wl.fb.data, 0, wl.fb.w * wl.fb.h * sizeof(Pixel));
	wl_surface_attach(wl.surface, wl.buffer, 0, 0);
	wl_surface_damage_buffer(wl.surface, 0, 0, wl.fb.w, wl.fb.h);
	wl_surface_commit(wl.surface);
	bufsock_flush(&wl.bs);
}

void
wl_show(PixelBuf pixels, u32 scale)
{
	wl_ensure_fb_size(pixels.w * scale, pixels.h * scale);
	log_assert(pixels.w <= wl.fb.w && pixels.h <= wl.fb.h, "frame is too large for the framebuffer");
	bufsock_flush(&wl.bs);
	wl_drain_events();
	memset(wl.fb.data, 0, wl.fb.w * wl.fb.h * sizeof(Pixel));
	pixelbuf_copy(wl.fb, pixels, wl.fb.w - pixels.w * scale, 0, scale);
	wl_surface_attach(wl.surface, wl.buffer, 0, 0);
	wl_surface_damage_buffer(wl.surface, 0, 0, wl.fb.w, wl.fb.h);
	wl_surface_commit(wl.surface);
	bufsock_flush(&wl.bs);
}
