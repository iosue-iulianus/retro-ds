/* A test frontend for libretro's Dolphin, from the Wind Waker spike
 * (docs/research/the-wind-waker.md; not RetroDS code). It loads a core,
 * runs a script of inputs, reads the game's RAM, times frames and takes
 * screenshots. The core draws with the GPU (its Software and Null renderers
 * crash at boot): headless GLES (an EGL pbuffer, or no surface on Mesa), or
 * on Android with LRTEST_VK=1 Vulkan, its frames handed to GLES through an
 * AHardwareBuffer and a sync fd as RetroDS would (lrvk.c). Build and run
 * with build.sh, run_host.sh and run_device.sh.
 *
 *   lrtest CORE GAME SYSDIR OUTDIR SCRIPT [key=value ...]
 *
 * SCRIPT may be a named pipe: the game stays loaded and reads commands as
 * they come (it doesn't run between them). key=value are core options.
 *
 * Script lines:
 *   hold BUTTONS        buttons held from now on (A B X Y Z L R START UP DOWN LEFT RIGHT, or -)
 *   stick X Y           main stick, -32767..32767 (Start's menus move with it, not the D-pad)
 *   cstick X Y          C stick
 *   wait N              run N frames
 *   press BUTTON [N]    hold BUTTON for 4 frames, then N frames (default 20) without it
 *   mash BUTTON N       press BUTTON every 10 frames for N frames
 *   shot NAME           write the last frame as OUTDIR/NAME.png
 *   peek ADDR LEN       hex of GameCube RAM at ADDR (0x80000000-based)
 *   peekp ADDR OFF LEN  hex at (*(u32*)ADDR)+OFF
 *   poke ADDR HEX       write bytes (tests only)
 *   bench N             run N frames, report the time per retro_run (and the bridge's)
 *   pace on|off         frames paced at 59.94 Hz as a frontend would (unpaced, Dolphin's
 *                       speed limiter sleeps whenever the game runs ahead: ~850 ms stalls)
 *   measure on|off      the bridge waits for GLES's drawing each frame, to time it
 *   check               (Vulkan) compare GLES's drawing with the frame Vulkan copied
 *   save FILE / load FILE   serialize / unserialize (load after the first frames)
 *   watch ADDR LEN N    run N frames, print the bytes whenever they change
 *   mark NAME           print "mark NAME" (run_device.sh waits for it)
 *   quit
 */
#include "libretro.h"

#include <dlfcn.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/stat.h>

#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES3/gl3.h>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

static const char *sysdir, *outdir;
static char kv[32][2][128];
static int nkv;
static unsigned pixfmt = RETRO_PIXEL_FORMAT_0RGB1555;
static uint8_t* frame;
static unsigned fw, fh;
static long frames;
static unsigned held;
static int16_t stick[2][2];

/* hardware rendering: a headless GLES 3 context, an FBO the core draws into */
static struct retro_hw_render_callback hw;
static bool hwOn, hwFrame;
static EGLDisplay edpy;
static EGLContext ectx;
static EGLSurface esurf;
static GLuint fbo, fboTex, fboDepth;
#define FBO_W 2048
#define FBO_H 2048
static uintptr_t get_fb(void) { return fbo; }
static retro_proc_address_t get_proc(const char* sym) { return (retro_proc_address_t) eglGetProcAddress(sym); }
static bool gl_init(unsigned major, unsigned minor) {
	if (!edpy) {
		edpy = eglGetDisplay(EGL_DEFAULT_DISPLAY);
		if (!eglInitialize(edpy, NULL, NULL)) return false;
	}
	/* a pbuffer where there is one (Android); otherwise no surface at all
	 * (EGL_KHR_surfaceless_context: Mesa on WSL): the core draws into our FBO */
	const EGLint ca[] = { EGL_SURFACE_TYPE, EGL_PBUFFER_BIT, EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT_KHR, EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8,
		EGL_BLUE_SIZE, 8, EGL_ALPHA_SIZE, 8, EGL_DEPTH_SIZE, 24, EGL_STENCIL_SIZE, 8, EGL_NONE };
	EGLConfig cfg = NULL;
	EGLint n = 0;
	if (!eglChooseConfig(edpy, ca, &cfg, 1, &n) || n < 1) cfg = NULL;
	esurf = EGL_NO_SURFACE;
	if (cfg) {
		const EGLint sa[] = { EGL_WIDTH, 16, EGL_HEIGHT, 16, EGL_NONE };
		esurf = eglCreatePbufferSurface(edpy, cfg, sa);
	}
	eglBindAPI(EGL_OPENGL_ES_API);
	const EGLint xa[] = { EGL_CONTEXT_MAJOR_VERSION, (EGLint) major, EGL_CONTEXT_MINOR_VERSION, (EGLint) minor, EGL_NONE };
	if (ectx) eglDestroyContext(edpy, ectx);
	ectx = eglCreateContext(edpy, cfg ? cfg : (EGLConfig) 0 /* EGL_NO_CONFIG_KHR */, EGL_NO_CONTEXT, xa);
	if (!ectx) {
		fprintf(stderr, "no GLES %u.%u context (EGL error %x)\n", major, minor, eglGetError());
		return false;
	}
	if (!eglMakeCurrent(edpy, esurf, esurf, ectx)) return false;
	glGenTextures(1, &fboTex);
	glBindTexture(GL_TEXTURE_2D, fboTex);
	glTexStorage2D(GL_TEXTURE_2D, 1, GL_RGBA8, FBO_W, FBO_H);
	glGenRenderbuffers(1, &fboDepth);
	glBindRenderbuffer(GL_RENDERBUFFER, fboDepth);
	glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, FBO_W, FBO_H);
	glGenFramebuffers(1, &fbo);
	glBindFramebuffer(GL_FRAMEBUFFER, fbo);
	glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, fboTex, 0);
	glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, fboDepth);
	fprintf(stderr, "GL: %s, %s; fbo %s\n", glGetString(GL_RENDERER), glGetString(GL_VERSION),
	        glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE ? "complete" : "INCOMPLETE");
	return true;
}

static double now_ms(void);
static bool vkMode, measureLatency, pace;
static double paceNext;
#ifdef __ANDROID__
#include "lrvk.c"
#endif

static void logcb(enum retro_log_level l, const char* fmt, ...) {
	if (l < RETRO_LOG_WARN && !getenv("LRTEST_VERBOSE")) return;
	va_list ap;
	va_start(ap, fmt);
	fprintf(stderr, "[core] ");
	vfprintf(stderr, fmt, ap);
	va_end(ap);
}

static bool env(unsigned cmd, void* data) {
	switch (cmd & 0xFFFF) {
	case RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY:
	case RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY:
		*(const char**) data = sysdir;
		return true;
	case RETRO_ENVIRONMENT_GET_LOG_INTERFACE:
		((struct retro_log_callback*) data)->log = logcb;
		return true;
	case RETRO_ENVIRONMENT_SET_PIXEL_FORMAT:
		pixfmt = *(const enum retro_pixel_format*) data;
		return true;
	case RETRO_ENVIRONMENT_GET_VARIABLE: {
		struct retro_variable* v = data;
		for (int i = 0; i < nkv; ++i)
			if (!strcmp(kv[i][0], v->key)) {
				v->value = kv[i][1];
				return true;
			}
		v->value = NULL;
		return false;
	}
	case RETRO_ENVIRONMENT_GET_CAN_DUPE:
		*(bool*) data = true;
		return true;
	case RETRO_ENVIRONMENT_GET_CORE_OPTIONS_VERSION:
		*(unsigned*) data = 0;
		return true;
#ifdef __ANDROID__
	case RETRO_ENVIRONMENT_GET_PREFERRED_HW_RENDER:
		if (!getenv("LRTEST_VK")) return false;
		*(unsigned*) data = RETRO_HW_CONTEXT_VULKAN;
		return true;
	case RETRO_ENVIRONMENT_SET_HW_RENDER_CONTEXT_NEGOTIATION_INTERFACE & 0xFFFF:
		vkNeg = data;
		fprintf(stderr, "vk: negotiation interface v%u (create_device2 %s)\n", vkNeg->interface_version, vkNeg->create_device2 ? "yes" : "no");
		return true;
	case RETRO_ENVIRONMENT_GET_HW_RENDER_INTERFACE & 0xFFFF:
		if (!vkMode) return false;
		*(const struct retro_hw_render_interface**) data = (const struct retro_hw_render_interface*) &vkIface;
		return true;
#endif
	case RETRO_ENVIRONMENT_SET_HW_RENDER: {
		struct retro_hw_render_callback* cb = data;
#ifdef __ANDROID__
		if (cb->context_type == RETRO_HW_CONTEXT_VULKAN && getenv("LRTEST_VK")) {
			/* GLES too: the bridge draws with it, as RetroDS's renderer would */
			if (!gl_init(3, 0)) return false;
			hw = *cb;
			hwOn = vkMode = true;
			fprintf(stderr, "hw render: Vulkan\n");
			return true;
		}
#endif
		if (getenv("LRTEST_NO_HW") || (cb->context_type != RETRO_HW_CONTEXT_OPENGLES3 && cb->context_type != RETRO_HW_CONTEXT_OPENGLES_VERSION)) return false;
		if (!gl_init(cb->version_major ? cb->version_major : 3, cb->version_major ? cb->version_minor : 0)) return false;
		fprintf(stderr, "hw render: type %d, GLES %u.%u\n", cb->context_type, cb->version_major, cb->version_minor);
		cb->get_current_framebuffer = get_fb;
		cb->get_proc_address = get_proc;
		hw = *cb;
		hwOn = true;
		return true;
	}
	case RETRO_ENVIRONMENT_SET_HW_SHARED_CONTEXT & 0xFFFF: /* (an experimental call: the switch masks the flag off) */
		fprintf(stderr, "shared context: yes\n");
		return true;
	case RETRO_ENVIRONMENT_SET_MEMORY_MAPS: {
		const struct retro_memory_map* m = data;
		for (unsigned i = 0; i < m->num_descriptors; ++i)
			fprintf(stderr, "memory map: %s at %08zx, %zu bytes, flags %llx\n", m->descriptors[i].addrspace ? m->descriptors[i].addrspace : "?",
			        m->descriptors[i].start, m->descriptors[i].len, (unsigned long long) m->descriptors[i].flags);
		return true;
	}
	default:
		return false;
	}
}

static void video(const void* data, unsigned w, unsigned h, size_t pitch) {
	if (data == RETRO_HW_FRAME_BUFFER_VALID) {
		fw = w, fh = h, hwFrame = true;
		return;
	}
	if (!data) return;
	hwFrame = false;
	if (w * h * 4 > (unsigned) (fw * fh * 4) || !frame) frame = realloc(frame, w * h * 4);
	fw = w, fh = h;
	for (unsigned y = 0; y < h; ++y) {
		const uint8_t* row = (const uint8_t*) data + y * pitch;
		uint8_t* o = frame + y * w * 4;
		for (unsigned x = 0; x < w; ++x, o += 4) {
			if (pixfmt == RETRO_PIXEL_FORMAT_XRGB8888) {
				uint32_t p = ((const uint32_t*) row)[x];
				o[0] = p >> 16, o[1] = p >> 8, o[2] = p, o[3] = 255;
			} else {
				uint16_t p = ((const uint16_t*) row)[x];
				o[0] = (p >> 11 & 31) * 255 / 31, o[1] = (p >> 5 & 63) * 255 / 63, o[2] = (p & 31) * 255 / 31, o[3] = 255;
			}
		}
	}
}

static void audio1(int16_t l, int16_t r) { (void) l, (void) r; }
static size_t audiob(const int16_t* d, size_t n) { (void) d; return n; }
static void poll(void) {}

static const char* const names[] = { "B", "Y", "SELECT", "START", "UP", "DOWN", "LEFT", "RIGHT", "A", "X", "Z", "ZL_", "L", "R" };
static int button(const char* s) {
	/* GameCube names -> libretro ids (the core: A=A, B=B, X=X, Y=Y, Z=R, L=L2, R=R2) */
	if (!strcmp(s, "Z")) return RETRO_DEVICE_ID_JOYPAD_R;
	if (!strcmp(s, "L")) return RETRO_DEVICE_ID_JOYPAD_L2;
	if (!strcmp(s, "R")) return RETRO_DEVICE_ID_JOYPAD_R2;
	for (int i = 0; i < 10; ++i)
		if (!strcmp(s, names[i])) return i;
	fprintf(stderr, "unknown button %s\n", s);
	return -1;
}

static int16_t input(unsigned port, unsigned dev, unsigned idx, unsigned id) {
	if (port) return 0;
	if (dev == RETRO_DEVICE_JOYPAD) {
		if (id == RETRO_DEVICE_ID_JOYPAD_MASK) return (int16_t) held;
		return held >> id & 1;
	}
	if (dev == RETRO_DEVICE_ANALOG && idx < 2 && id < 2) return stick[idx][id];
	return 0;
}

static struct {
	void (*init)(void);
	void (*set_environment)(retro_environment_t);
	void (*set_video_refresh)(retro_video_refresh_t);
	void (*set_audio_sample)(retro_audio_sample_t);
	void (*set_audio_sample_batch)(retro_audio_sample_batch_t);
	void (*set_input_poll)(retro_input_poll_t);
	void (*set_input_state)(retro_input_state_t);
	bool (*load_game)(const struct retro_game_info*);
	void (*run)(void);
	void (*unload_game)(void);
	void (*deinit)(void);
	void* (*get_memory_data)(unsigned);
	size_t (*get_memory_size)(unsigned);
	size_t (*serialize_size)(void);
	bool (*serialize)(void*, size_t);
	bool (*unserialize)(const void*, size_t);
	void (*get_system_av_info)(struct retro_system_av_info*);
} c;

static double now_ms(void) {
	struct timespec t;
	clock_gettime(CLOCK_MONOTONIC, &t);
	return t.tv_sec * 1e3 + t.tv_nsec / 1e6;
}

static double lastRun, maxRun, sumRun;
static double brSum[4];
static long brN;
static double runs[100000];
static long nRun;
static void step(void) {
	double t = now_ms();
	c.run();
	if (hwOn && getenv("LRTEST_FINISH")) glFinish();
	else if (hwOn && getenv("LRTEST_FLUSH")) glFlush();
	lastRun = now_ms() - t;
#ifdef __ANDROID__
	if (vkMode && lr_vk_bridge(fw, fh, measureLatency)) {
		brSum[0] += brQueued, brSum[1] += brHanded, brSum[2] += brGlQueued, brSum[3] += brGlDone, ++brN;
	}
#endif
	if (pace) { /* as a frontend at the game's 59.94 Hz would */
		double now = now_ms();
		if (paceNext < now - 50) paceNext = now;
		paceNext += 1000.0 / 59.94;
		if (paceNext > now) usleep((useconds_t) ((paceNext - now) * 1000));
	}
	if (lastRun > maxRun) maxRun = lastRun;
	if (nRun < 100000) runs[nRun] = lastRun;
	sumRun += lastRun, ++nRun;
	++frames;
}

static const uint8_t* ram(uint32_t addr, uint32_t len) {
	uint8_t* p = c.get_memory_data(RETRO_MEMORY_SYSTEM_RAM);
	size_t n = c.get_memory_size(RETRO_MEMORY_SYSTEM_RAM);
	uint32_t off = addr - 0x80000000u;
	if (!p || off + len > n) return NULL;
	return p + off;
}

static void hex(uint32_t addr, uint32_t len) {
	const uint8_t* p = ram(addr, len);
	printf("f%ld %08X:", frames, addr);
	if (!p) {
		printf(" (out of range)\n");
		return;
	}
	for (uint32_t i = 0; i < len; ++i) printf(" %02X", p[i]);
	printf("\n");
}

#define LOAD(n) (*(void**) &c.n = dlsym(h, "retro_" #n)) || (fprintf(stderr, "missing retro_" #n "\n"), exit(1), 0)

int main(int argc, char** argv) {
	if (argc < 6) {
		fprintf(stderr, "lrtest CORE GAME SYSDIR OUTDIR SCRIPT [key=value ...]\n");
		return 2;
	}
	setvbuf(stdout, NULL, _IONBF, 0);
	sysdir = argv[3], outdir = argv[4];
	for (int i = 6; i < argc && nkv < 32; ++i) {
		const char* eq = strchr(argv[i], '=');
		if (!eq) continue;
		snprintf(kv[nkv][0], 128, "%.*s", (int) (eq - argv[i]), argv[i]);
		snprintf(kv[nkv][1], 128, "%s", eq + 1);
		++nkv;
	}
	void* h = dlopen(argv[1], RTLD_NOW | RTLD_LOCAL);
	if (!h) {
		fprintf(stderr, "dlopen: %s\n", dlerror());
		return 1;
	}
	LOAD(init); LOAD(set_environment); LOAD(set_video_refresh); LOAD(set_audio_sample); LOAD(set_audio_sample_batch);
	LOAD(set_input_poll); LOAD(set_input_state); LOAD(load_game); LOAD(run); LOAD(unload_game); LOAD(deinit);
	LOAD(get_memory_data); LOAD(get_memory_size); LOAD(serialize_size); LOAD(serialize); LOAD(unserialize); LOAD(get_system_av_info);
	c.set_environment(env);
	c.init();
	c.set_video_refresh(video);
	c.set_audio_sample(audio1);
	c.set_audio_sample_batch(audiob);
	c.set_input_poll(poll);
	c.set_input_state(input);
	struct retro_game_info gi = { argv[2], NULL, 0, NULL };
	double t0 = now_ms();
	if (!c.load_game(&gi)) {
		fprintf(stderr, "load_game failed\n");
		return 1;
	}
#ifdef __ANDROID__
	if (vkMode && !lr_vk_init()) {
		fprintf(stderr, "vk: setup failed\n");
		return 1;
	}
#endif
	if (hwOn && hw.context_reset) hw.context_reset();
	void (*set_port)(unsigned, unsigned) = (void (*)(unsigned, unsigned)) dlsym(h, "retro_set_controller_port_device");
	if (set_port) set_port(0, RETRO_DEVICE_JOYPAD);
	struct retro_system_av_info av;
	c.get_system_av_info(&av);
	printf("loaded in %.0f ms; %ux%u (max %ux%u), %.3f fps\n", now_ms() - t0, av.geometry.base_width, av.geometry.base_height,
	       av.geometry.max_width, av.geometry.max_height, av.timing.fps);
	FILE* sf = fopen(argv[5], "r");
	if (!sf) {
		perror(argv[5]);
		return 1;
	}
	/* a named pipe: keep the game loaded and read commands as they come
	 * (the game doesn't run between commands) */
	struct stat st;
	bool fifo = !stat(argv[5], &st) && S_ISFIFO(st.st_mode);
	char line[256];
	for (;;) {
		if (!fgets(line, sizeof line, sf)) {
			if (!fifo) break;
			fclose(sf);
			if (!(sf = fopen(argv[5], "r"))) break;
			continue;
		}
		char cmd[32] = "", a[128] = "", b[64] = "", d[64] = "";
		if (sscanf(line, "%31s %127s %63s %63s", cmd, a, b, d) < 1 || cmd[0] == '#') continue;
		if (!strcmp(cmd, "hold")) {
			held = 0;
			for (char* tok = strtok(a, "+"); tok; tok = strtok(NULL, "+"))
				if (strcmp(tok, "-") && button(tok) >= 0) held |= 1u << button(tok);
		} else if (!strcmp(cmd, "stick") || !strcmp(cmd, "cstick")) {
			int s = cmd[0] == 'c';
			stick[s][0] = (int16_t) atoi(a), stick[s][1] = (int16_t) atoi(b);
		} else if (!strcmp(cmd, "wait")) {
			for (long n = atol(a); n > 0; --n) step();
		} else if (!strcmp(cmd, "press")) {
			int id = button(a);
			unsigned keep = held;
			if (id >= 0) held |= 1u << id;
			for (int n = 0; n < 4; ++n) step();
			held = keep;
			for (int n = b[0] ? atoi(b) : 20; n > 0; --n) step();
		} else if (!strcmp(cmd, "mash")) {
			int id = button(a);
			unsigned keep = held;
			for (long n = 0; n < atol(b); ++n) {
				held = keep | (n % 10 < 3 && id >= 0 ? 1u << id : 0);
				step();
			}
			held = keep;
		} else if (!strcmp(cmd, "shot")) {
			char path[512];
			snprintf(path, sizeof path, "%s/%s.png", outdir, a);
			if (hwFrame) {
				frame = realloc(frame, fw * fh * 4);
				glBindFramebuffer(GL_READ_FRAMEBUFFER, fbo);
				glReadPixels(0, 0, fw, fh, GL_RGBA, GL_UNSIGNED_BYTE, frame);
				stbi_flip_vertically_on_write(!vkMode && hw.bottom_left_origin);
			}
			if (frame && stbi_write_png(path, fw, fh, 4, frame, fw * 4)) printf("f%ld shot %s (%ux%u)\n", frames, path, fw, fh);
			else printf("f%ld shot %s: no frame\n", frames, a);
		} else if (!strcmp(cmd, "peek")) {
			hex((uint32_t) strtoul(a, NULL, 16), (uint32_t) strtoul(b, NULL, 0));
		} else if (!strcmp(cmd, "peekp")) {
			const uint8_t* p = ram((uint32_t) strtoul(a, NULL, 16), 4);
			uint32_t ptr = p ? (uint32_t) p[0] << 24 | p[1] << 16 | p[2] << 8 | p[3] : 0;
			printf("ptr %08X -> ", ptr);
			if (ptr >= 0x80000000u) hex(ptr + (uint32_t) strtoul(b, NULL, 16), (uint32_t) strtoul(d, NULL, 0));
			else printf("null\n");
		} else if (!strcmp(cmd, "bench")) {
			maxRun = sumRun = 0, nRun = 0;
			memset(brSum, 0, sizeof brSum), brN = 0;
			double t = now_ms();
			for (long n = atol(a); n > 0; --n) step();
			double all = now_ms() - t;
			printf("f%ld bench %ld frames: %.2f ms per frame (max %.2f), %.1f fps unthrottled\n", frames, nRun, sumRun / nRun, maxRun,
			       nRun * 1000.0 / all);
			{
				int slow = 0;
				double rest = 0;
				printf("  slow frames:");
				for (long k = 0; k < nRun && k < 100000; ++k) {
					if (runs[k] > 50) printf(" #%ld=%.0f", k, runs[k]), ++slow;
					else rest += runs[k];
				}
				printf("\n  %d over 50 ms; the other %ld average %.2f ms\n", slow, nRun - slow, nRun > slow ? rest / (nRun - slow) : 0);
			}
			if (brN)
				printf("  bridge, ms after retro_run (average): copy queued %.3f, handed to GLES %.3f, GLES draw queued %.3f, GLES draw done %.3f\n",
				       brSum[0] / brN, brSum[1] / brN, brSum[2] / brN, brSum[3] / brN);
		} else if (!strcmp(cmd, "watch")) {
			uint32_t addr = (uint32_t) strtoul(a, NULL, 16), len = (uint32_t) strtoul(b, NULL, 0);
			uint8_t last[256];
			memset(last, 0xEE, sizeof last);
			if (len > sizeof last) len = sizeof last;
			for (long n = atol(d); n > 0; --n) {
				step();
				const uint8_t* p = ram(addr, len);
				if (p && memcmp(p, last, len)) {
					memcpy(last, p, len);
					hex(addr, len);
				}
			}
		} else if (!strcmp(cmd, "save") || !strcmp(cmd, "load")) {
			size_t n = c.serialize_size();
			void* buf = malloc(n);
			if (cmd[0] == 's') {
				double t = now_ms();
				bool ok = c.serialize(buf, n);
				double ms = now_ms() - t;
				FILE* f = fopen(a, "wb");
				if (ok && f) fwrite(buf, 1, n, f);
				if (f) fclose(f);
				printf("f%ld save %s: %s, %zu bytes, %.1f ms\n", frames, a, ok ? "ok" : "FAILED", n, ms);
			} else {
				FILE* f = fopen(a, "rb");
				size_t got = f ? fread(buf, 1, n, f) : 0;
				if (f) fclose(f);
				double t = now_ms();
				bool ok = got && c.unserialize(buf, got);
				printf("f%ld load %s: %s, %zu bytes, %.1f ms\n", frames, a, ok ? "ok" : "FAILED", got, now_ms() - t);
			}
			free(buf);
#ifdef __ANDROID__
		} else if (!strcmp(cmd, "check") && vkMode) {
			rbWanted = true;
			step();
			rbWanted = false;
			pthread_mutex_lock(&vkQueueLock); /* (Dolphin submits from a thread of its own) */
			vkQueueWaitIdle(vkQueue);
			pthread_mutex_unlock(&vkQueueLock);
			glFinish();
			uint8_t* gl = malloc(ahbW * ahbH * 4);
			glBindFramebuffer(GL_READ_FRAMEBUFFER, fbo);
			glReadPixels(0, 0, ahbW, ahbH, GL_RGBA, GL_UNSIGNED_BYTE, gl);
			long diff = 0, nonblack = 0;
			for (unsigned y = 0; y < ahbH; ++y)
				for (unsigned x = 0; x < ahbW * 4; ++x) {
					uint8_t v = ((uint8_t*) rbPtr)[y * ahbW * 4 + x];
					/* (GLES's rows: the quad puts the buffer's first row at the bottom) */
					diff += v != gl[y * ahbW * 4 + x];
					nonblack += (x & 3) != 3 && v > 16;
				}
			printf("f%ld check: GLES drew %ld of %u bytes differently from the frame Vulkan copied (%ld bright bytes in it)\n", frames, diff,
			       ahbW * ahbH * 4, nonblack);
			free(gl);
#endif
		} else if (!strcmp(cmd, "poke")) { /* (tests only: poke ADDR HEXBYTES) */
			uint32_t addr = (uint32_t) strtoul(a, NULL, 16), n = (uint32_t) strlen(b) / 2;
			uint8_t* p = (uint8_t*) ram(addr, n);
			for (uint32_t k = 0; p && k < n; ++k) {
				char hx[3] = { b[2 * k], b[2 * k + 1], 0 };
				p[k] = (uint8_t) strtoul(hx, NULL, 16);
			}
			hex(addr, n);
		} else if (!strcmp(cmd, "pace")) {
			pace = !strcmp(a, "on");
			paceNext = 0;
		} else if (!strcmp(cmd, "measure")) {
			measureLatency = !strcmp(a, "on");
		} else if (!strcmp(cmd, "mark")) {
			printf("mark %s\n", a);
		} else if (!strcmp(cmd, "quit")) {
			break;
		}
		fflush(stdout);
	}
	printf("f%ld done\n", frames);
	fflush(stdout);
	c.unload_game();
	c.deinit();
	_exit(0);
}
