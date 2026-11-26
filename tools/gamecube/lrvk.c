/* The Wind Waker spike: libretro Vulkan for the test frontend (Android), and
 * the bridge to GLES (option 1): the core's image copied on the GPU into an
 * AHardwareBuffer, a sync fd from Vulkan, GLES waiting on it on the GPU and
 * drawing the buffer. Included by lrtest.c. Not RetroDS code. */
#include <android/hardware_buffer.h>
#include <media/NdkImageReader.h>
#define VK_USE_PLATFORM_ANDROID_KHR
#include <vulkan/vulkan.h>
#include <pthread.h>
#include "libretro_vulkan.h"

static const struct retro_hw_render_context_negotiation_interface_vulkan* vkNeg;
static struct retro_hw_render_interface_vulkan vkIface;
static VkInstance vkInst;
static VkPhysicalDevice vkGpu;
static VkDevice vkDev;
static VkQueue vkQueue;
static uint32_t vkFamily;
static VkSurfaceKHR vkSurf;
static pthread_mutex_t vkQueueLock = PTHREAD_MUTEX_INITIALIZER;
static bool vkFamilyForeign, vkHaveAhb, vkHaveSemFd;

/* the core's frame */
static struct retro_vulkan_image vkImage;
static bool vkImageSet;
#define VK_SYNC 3
static uint32_t vkSyncIndex;
static VkFence vkFrameFence[VK_SYNC];
static bool vkFrameFenceUsed[VK_SYNC];
static VkCommandPool vkPool;
static VkCommandBuffer vkCmd[VK_SYNC];
static VkSemaphore vkDoneSem;

/* the shared buffer */
static AHardwareBuffer* ahb;
static unsigned ahbW, ahbH;
static VkImage ahbImage;
static VkDeviceMemory ahbMem;
static EGLImageKHR ahbEgl;
static GLuint ahbTex, drawProg, drawVao;
/* a readback of the core's image in the same submission, to compare */
static VkBuffer rbBuf;
static VkDeviceMemory rbMem;
static void* rbPtr;
static bool rbWanted;

static PFN_vkGetAndroidHardwareBufferPropertiesANDROID pGetAhbProps;
static PFN_vkGetSemaphoreFdKHR pGetSemFd;

static void lr_vk_set_image(void* h, const struct retro_vulkan_image* img, uint32_t n, const VkSemaphore* s, uint32_t fam) {
	(void) h, (void) n, (void) s, (void) fam;
	vkImage = *img;
	vkImageSet = true;
}
static uint32_t lr_vk_get_sync_index(void* h) { (void) h; return vkSyncIndex; }
static uint32_t lr_vk_get_sync_index_mask(void* h) { (void) h; return (1u << VK_SYNC) - 1; }
static void lr_vk_set_command_buffers(void* h, uint32_t n, const VkCommandBuffer* c) { (void) h, (void) n, (void) c; }
static void lr_vk_wait_sync_index(void* h) {
	(void) h;
	if (vkFrameFenceUsed[vkSyncIndex]) vkWaitForFences(vkDev, 1, &vkFrameFence[vkSyncIndex], VK_TRUE, UINT64_MAX);
}
static void lr_vk_lock_queue(void* h) { (void) h; pthread_mutex_lock(&vkQueueLock); }
static void lr_vk_unlock_queue(void* h) { (void) h; pthread_mutex_unlock(&vkQueueLock); }
static void lr_vk_set_signal_semaphore(void* h, VkSemaphore s) { (void) h, (void) s; }

static bool has_ext(const VkExtensionProperties* e, uint32_t n, const char* name) {
	for (uint32_t i = 0; i < n; ++i)
		if (!strcmp(e[i].extensionName, name)) return true;
	return false;
}

/* the core's device, with what the bridge needs added */
static VkDevice lr_vk_create_device(VkPhysicalDevice gpu, void* opaque, const VkDeviceCreateInfo* ci) {
	(void) opaque;
	uint32_t n = 0;
	vkEnumerateDeviceExtensionProperties(gpu, NULL, &n, NULL);
	VkExtensionProperties* e = calloc(n, sizeof *e);
	vkEnumerateDeviceExtensionProperties(gpu, NULL, &n, e);
	static const char* want[] = { "VK_ANDROID_external_memory_android_hardware_buffer", "VK_KHR_external_memory", "VK_EXT_queue_family_foreign",
		"VK_KHR_dedicated_allocation", "VK_KHR_get_memory_requirements2", "VK_KHR_bind_memory2", "VK_KHR_sampler_ycbcr_conversion",
		"VK_KHR_maintenance1", "VK_KHR_external_semaphore", "VK_KHR_external_semaphore_fd" };
	const char** exts = calloc(ci->enabledExtensionCount + 16, sizeof(char*));
	uint32_t ne = 0;
	for (uint32_t i = 0; i < ci->enabledExtensionCount; ++i) exts[ne++] = ci->ppEnabledExtensionNames[i];
	for (unsigned w = 0; w < sizeof want / sizeof *want; ++w) {
		bool dup = false;
		for (uint32_t i = 0; i < ne; ++i) dup |= !strcmp(exts[i], want[w]);
		if (!dup && has_ext(e, n, want[w])) exts[ne++] = want[w];
	}
	vkFamilyForeign = has_ext(e, n, "VK_EXT_queue_family_foreign");
	vkHaveAhb = has_ext(e, n, "VK_ANDROID_external_memory_android_hardware_buffer");
	vkHaveSemFd = has_ext(e, n, "VK_KHR_external_semaphore_fd");
	VkDeviceCreateInfo c = *ci;
	c.enabledExtensionCount = ne;
	c.ppEnabledExtensionNames = exts;
	VkDevice dev = VK_NULL_HANDLE;
	VkResult r = vkCreateDevice(gpu, &c, NULL, &dev);
	fprintf(stderr, "vk: device with %u extensions: %d (AHB %d, sync fd %d, foreign %d)\n", ne, r, vkHaveAhb, vkHaveSemFd, vkFamilyForeign);
	free(e);
	return r == VK_SUCCESS ? dev : VK_NULL_HANDLE;
}

static bool lr_vk_init(void) {
	const VkApplicationInfo* app = vkNeg && vkNeg->get_application_info ? vkNeg->get_application_info() : NULL;
	VkApplicationInfo a = app ? *app : (VkApplicationInfo){ VK_STRUCTURE_TYPE_APPLICATION_INFO };
	if (a.apiVersion < VK_API_VERSION_1_1) a.apiVersion = VK_API_VERSION_1_1;
	const char* iexts[] = { "VK_KHR_surface", "VK_KHR_android_surface", "VK_KHR_get_physical_device_properties2",
		"VK_KHR_external_memory_capabilities", "VK_KHR_external_semaphore_capabilities" };
	VkInstanceCreateInfo ici = { VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO, NULL, 0, &a, 0, NULL, 5, iexts };
	if (vkCreateInstance(&ici, NULL, &vkInst) != VK_SUCCESS) return false;
	uint32_t n = 1;
	vkEnumeratePhysicalDevices(vkInst, &n, &vkGpu);
	VkPhysicalDeviceProperties p;
	vkGetPhysicalDeviceProperties(vkGpu, &p);
	fprintf(stderr, "vk: %s, driver %u.%u.%u, API %u.%u\n", p.deviceName, VK_VERSION_MAJOR(p.driverVersion), VK_VERSION_MINOR(p.driverVersion),
	        VK_VERSION_PATCH(p.driverVersion), VK_VERSION_MAJOR(p.apiVersion), VK_VERSION_MINOR(p.apiVersion));
	/* a real surface for the core's swapchain queries: an offscreen AImageReader window */
	AImageReader* reader = NULL;
	if (AImageReader_new(640, 528, AIMAGE_FORMAT_RGBA_8888, 3, &reader) != AMEDIA_OK) return false;
	ANativeWindow* win = NULL;
	AImageReader_getWindow(reader, &win);
	VkAndroidSurfaceCreateInfoKHR sci = { VK_STRUCTURE_TYPE_ANDROID_SURFACE_CREATE_INFO_KHR, NULL, 0, win };
	if (vkCreateAndroidSurfaceKHR(vkInst, &sci, NULL, &vkSurf) != VK_SUCCESS) return false;
	struct retro_vulkan_context ctx = { 0 };
	bool ok = vkNeg->create_device2 ? vkNeg->create_device2(&ctx, vkInst, vkGpu, vkSurf, vkGetInstanceProcAddr, lr_vk_create_device, NULL)
	                                : vkNeg->create_device(&ctx, vkInst, vkGpu, vkSurf, vkGetInstanceProcAddr, NULL, 0, NULL, 0, NULL);
	if (!ok) {
		fprintf(stderr, "vk: the core couldn't create its device\n");
		return false;
	}
	vkDev = ctx.device, vkQueue = ctx.queue, vkFamily = ctx.queue_family_index, vkGpu = ctx.gpu;
	vkIface = (struct retro_hw_render_interface_vulkan){ RETRO_HW_RENDER_INTERFACE_VULKAN, RETRO_HW_RENDER_INTERFACE_VULKAN_VERSION, NULL,
		vkInst, vkGpu, vkDev, vkGetDeviceProcAddr, vkGetInstanceProcAddr, vkQueue, vkFamily, lr_vk_set_image, lr_vk_get_sync_index,
		lr_vk_get_sync_index_mask, lr_vk_set_command_buffers, lr_vk_wait_sync_index, lr_vk_lock_queue, lr_vk_unlock_queue,
		lr_vk_set_signal_semaphore };
	pGetAhbProps = (PFN_vkGetAndroidHardwareBufferPropertiesANDROID) vkGetDeviceProcAddr(vkDev, "vkGetAndroidHardwareBufferPropertiesANDROID");
	pGetSemFd = (PFN_vkGetSemaphoreFdKHR) vkGetDeviceProcAddr(vkDev, "vkGetSemaphoreFdKHR");
	VkCommandPoolCreateInfo pci = { VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO, NULL, VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT, vkFamily };
	vkCreateCommandPool(vkDev, &pci, NULL, &vkPool);
	VkCommandBufferAllocateInfo cai = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO, NULL, vkPool, VK_COMMAND_BUFFER_LEVEL_PRIMARY, VK_SYNC };
	vkAllocateCommandBuffers(vkDev, &cai, vkCmd);
	for (int i = 0; i < VK_SYNC; ++i) {
		VkFenceCreateInfo fci = { VK_STRUCTURE_TYPE_FENCE_CREATE_INFO };
		vkCreateFence(vkDev, &fci, NULL, &vkFrameFence[i]);
	}
	VkExportSemaphoreCreateInfo esci = { VK_STRUCTURE_TYPE_EXPORT_SEMAPHORE_CREATE_INFO, NULL, VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_SYNC_FD_BIT };
	VkSemaphoreCreateInfo sci2 = { VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO, &esci };
	vkCreateSemaphore(vkDev, &sci2, NULL, &vkDoneSem);
	return vkHaveAhb && vkHaveSemFd && pGetAhbProps && pGetSemFd;
}

static uint32_t mem_type(uint32_t bits, VkMemoryPropertyFlags want) {
	VkPhysicalDeviceMemoryProperties mp;
	vkGetPhysicalDeviceMemoryProperties(vkGpu, &mp);
	for (uint32_t i = 0; i < mp.memoryTypeCount; ++i)
		if ((bits >> i & 1) && (mp.memoryTypes[i].propertyFlags & want) == want) return i;
	return 0;
}

static const char* drawVs = "#version 300 es\nout vec2 uv;\nvoid main(){vec2 p=vec2(gl_VertexID&1,gl_VertexID>>1);uv=p;gl_Position=vec4(p*2.0-1.0,0,1);}\n";
static const char* drawFs = "#version 300 es\nprecision mediump float;\nin vec2 uv;\nuniform sampler2D t;\nout vec4 o;\nvoid main(){o=texture(t,uv);}\n";

/* the shared buffer, in Vulkan and in GLES */
static bool ahb_make(unsigned w, unsigned h) {
	AHardwareBuffer_Desc d = { w, h, 1, AHARDWAREBUFFER_FORMAT_R8G8B8A8_UNORM,
		AHARDWAREBUFFER_USAGE_GPU_SAMPLED_IMAGE | AHARDWAREBUFFER_USAGE_GPU_COLOR_OUTPUT, 0, 0, 0 };
	if (AHardwareBuffer_allocate(&d, &ahb)) return false;
	VkAndroidHardwareBufferFormatPropertiesANDROID fp = { VK_STRUCTURE_TYPE_ANDROID_HARDWARE_BUFFER_FORMAT_PROPERTIES_ANDROID };
	VkAndroidHardwareBufferPropertiesANDROID pr = { VK_STRUCTURE_TYPE_ANDROID_HARDWARE_BUFFER_PROPERTIES_ANDROID, &fp };
	if (pGetAhbProps(vkDev, ahb, &pr) != VK_SUCCESS) return false;
	VkExternalMemoryImageCreateInfo emi = { VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_IMAGE_CREATE_INFO, NULL,
		VK_EXTERNAL_MEMORY_HANDLE_TYPE_ANDROID_HARDWARE_BUFFER_BIT_ANDROID };
	VkImageCreateInfo ici = { VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO, &emi, 0, VK_IMAGE_TYPE_2D, VK_FORMAT_R8G8B8A8_UNORM, { w, h, 1 }, 1, 1,
		VK_SAMPLE_COUNT_1_BIT, VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
		VK_SHARING_MODE_EXCLUSIVE, 0, NULL, VK_IMAGE_LAYOUT_UNDEFINED };
	if (vkCreateImage(vkDev, &ici, NULL, &ahbImage) != VK_SUCCESS) return false;
	VkImportAndroidHardwareBufferInfoANDROID imp = { VK_STRUCTURE_TYPE_IMPORT_ANDROID_HARDWARE_BUFFER_INFO_ANDROID, NULL, ahb };
	VkMemoryDedicatedAllocateInfo ded = { VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO, &imp, ahbImage, VK_NULL_HANDLE };
	VkMemoryAllocateInfo mai = { VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, &ded, pr.allocationSize, mem_type(pr.memoryTypeBits, 0) };
	if (vkAllocateMemory(vkDev, &mai, NULL, &ahbMem) != VK_SUCCESS || vkBindImageMemory(vkDev, ahbImage, ahbMem, 0) != VK_SUCCESS) return false;
	/* GLES: the same buffer as a texture */
	PFNEGLGETNATIVECLIENTBUFFERANDROIDPROC getBuf = (PFNEGLGETNATIVECLIENTBUFFERANDROIDPROC) eglGetProcAddress("eglGetNativeClientBufferANDROID");
	PFNEGLCREATEIMAGEKHRPROC mkImage = (PFNEGLCREATEIMAGEKHRPROC) eglGetProcAddress("eglCreateImageKHR");
	void (*target)(GLenum, void*) = (void (*)(GLenum, void*)) eglGetProcAddress("glEGLImageTargetTexture2DOES");
	EGLint ia[] = { EGL_IMAGE_PRESERVED_KHR, EGL_TRUE, EGL_NONE };
	ahbEgl = mkImage(edpy, EGL_NO_CONTEXT, EGL_NATIVE_BUFFER_ANDROID, getBuf(ahb), ia);
	if (ahbEgl == EGL_NO_IMAGE_KHR) return false;
	glGenTextures(1, &ahbTex);
	glBindTexture(GL_TEXTURE_2D, ahbTex);
	target(GL_TEXTURE_2D, ahbEgl);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	if (!drawProg) {
		GLuint vs = glCreateShader(GL_VERTEX_SHADER), fs = glCreateShader(GL_FRAGMENT_SHADER);
		glShaderSource(vs, 1, &drawVs, NULL), glCompileShader(vs);
		glShaderSource(fs, 1, &drawFs, NULL), glCompileShader(fs);
		drawProg = glCreateProgram();
		glAttachShader(drawProg, vs), glAttachShader(drawProg, fs), glLinkProgram(drawProg);
		glGenVertexArrays(1, &drawVao);
	}
	/* a host-visible buffer for reading the core's image back (checks) */
	VkBufferCreateInfo bci = { VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO, NULL, 0, (VkDeviceSize) w * h * 4, VK_BUFFER_USAGE_TRANSFER_DST_BIT };
	vkCreateBuffer(vkDev, &bci, NULL, &rbBuf);
	VkMemoryRequirements mr;
	vkGetBufferMemoryRequirements(vkDev, rbBuf, &mr);
	VkMemoryAllocateInfo bai = { VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, NULL, mr.size,
		mem_type(mr.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) };
	vkAllocateMemory(vkDev, &bai, NULL, &rbMem);
	vkBindBufferMemory(vkDev, rbBuf, rbMem, 0);
	vkMapMemory(vkDev, rbMem, 0, VK_WHOLE_SIZE, 0, &rbPtr);
	ahbW = w, ahbH = h;
	fprintf(stderr, "bridge: %ux%u shared buffer, GLES texture %u\n", w, h, ahbTex);
	return true;
}

/* Timings of the last frame, ms from the end of retro_run:
 * the copy queued, the sync fd handed to GLES, GLES's draw queued, GLES's draw done (when measured) */
static double brQueued, brHanded, brGlQueued, brGlDone;

/* After retro_run: copy the core's frame into the shared buffer, and draw
 * it with GLES into the test FBO (as RetroDS's renderer would). */
static bool lr_vk_bridge(unsigned w, unsigned h, bool measure) {
	if (!vkImageSet) return false;
	double t0 = now_ms();
	if (!ahb && !ahb_make(w, h)) {
		fprintf(stderr, "bridge: couldn't make the shared buffer\n");
		exit(1);
	}
	uint32_t i = vkSyncIndex;
	if (vkFrameFenceUsed[i]) vkWaitForFences(vkDev, 1, &vkFrameFence[i], VK_TRUE, UINT64_MAX);
	vkResetFences(vkDev, 1, &vkFrameFence[i]);
	VkCommandBuffer cb = vkCmd[i];
	vkResetCommandBuffer(cb, 0);
	VkCommandBufferBeginInfo bi = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO, NULL, VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT };
	vkBeginCommandBuffer(cb, &bi);
	VkImage src = vkImage.create_info.image;
	uint32_t foreign = vkFamilyForeign ? VK_QUEUE_FAMILY_FOREIGN_EXT : VK_QUEUE_FAMILY_EXTERNAL;
	VkImageSubresourceRange rng = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
	VkImageMemoryBarrier pre[2] = {
		{ VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER, NULL, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_TRANSFER_READ_BIT,
		  vkImage.image_layout, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_QUEUE_FAMILY_IGNORED, VK_QUEUE_FAMILY_IGNORED, src, rng },
		{ VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER, NULL, 0, VK_ACCESS_TRANSFER_WRITE_BIT, VK_IMAGE_LAYOUT_UNDEFINED,
		  VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, foreign, vkFamily, ahbImage, rng } };
	vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, NULL, 0, NULL, 2, pre);
	VkImageBlit blit = { { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 }, { { 0, 0, 0 }, { (int32_t) w, (int32_t) h, 1 } },
		{ VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 }, { { 0, 0, 0 }, { (int32_t) ahbW, (int32_t) ahbH, 1 } } };
	vkCmdBlitImage(cb, src, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, ahbImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &blit, VK_FILTER_NEAREST);
	if (rbWanted) {
		VkBufferImageCopy bic = { 0, 0, 0, { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 }, { 0, 0, 0 }, { ahbW, ahbH, 1 } };
		/* (the shared buffer as Vulkan wrote it: compared with what GLES draws) */
		VkImageMemoryBarrier mid = { VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER, NULL, VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_TRANSFER_READ_BIT,
			VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_QUEUE_FAMILY_IGNORED, VK_QUEUE_FAMILY_IGNORED, ahbImage, rng };
		vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, NULL, 0, NULL, 1, &mid);
		vkCmdCopyImageToBuffer(cb, ahbImage, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, rbBuf, 1, &bic);
	}
	VkImageMemoryBarrier post[2] = {
		{ VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER, NULL, VK_ACCESS_TRANSFER_READ_BIT, VK_ACCESS_SHADER_READ_BIT, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
		  vkImage.image_layout, VK_QUEUE_FAMILY_IGNORED, VK_QUEUE_FAMILY_IGNORED, src, rng },
		{ VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER, NULL, VK_ACCESS_TRANSFER_WRITE_BIT | VK_ACCESS_TRANSFER_READ_BIT, 0,
		  rbWanted ? VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL : VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_GENERAL, vkFamily, foreign, ahbImage, rng } };
	vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, NULL,
	                     0, NULL, 2, post);
	vkEndCommandBuffer(cb);
	VkSubmitInfo si = { VK_STRUCTURE_TYPE_SUBMIT_INFO, NULL, 0, NULL, NULL, 1, &cb, 1, &vkDoneSem };
	pthread_mutex_lock(&vkQueueLock);
	vkQueueSubmit(vkQueue, 1, &si, vkFrameFence[i]);
	pthread_mutex_unlock(&vkQueueLock);
	vkFrameFenceUsed[i] = true;
	brQueued = now_ms() - t0;
	/* the copy's completion as a sync fd, waited on by GLES on the GPU */
	VkSemaphoreGetFdInfoKHR gfi = { VK_STRUCTURE_TYPE_SEMAPHORE_GET_FD_INFO_KHR, NULL, vkDoneSem, VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_SYNC_FD_BIT };
	int fd = -1;
	if (pGetSemFd(vkDev, &gfi, &fd) != VK_SUCCESS) {
		fprintf(stderr, "bridge: no sync fd\n");
		exit(1);
	}
	static PFNEGLCREATESYNCKHRPROC mkSync;
	static PFNEGLWAITSYNCKHRPROC waitSync;
	static PFNEGLDESTROYSYNCKHRPROC rmSync;
	if (!mkSync) {
		mkSync = (PFNEGLCREATESYNCKHRPROC) eglGetProcAddress("eglCreateSyncKHR");
		waitSync = (PFNEGLWAITSYNCKHRPROC) eglGetProcAddress("eglWaitSyncKHR");
		rmSync = (PFNEGLDESTROYSYNCKHRPROC) eglGetProcAddress("eglDestroySyncKHR");
		fprintf(stderr, "bridge: eglWaitSyncKHR %s\n", waitSync ? "there" : "MISSING");
	}
	EGLint sa[] = { EGL_SYNC_NATIVE_FENCE_FD_ANDROID, fd, EGL_NONE };
	EGLSyncKHR sync = fd >= 0 ? mkSync(edpy, EGL_SYNC_NATIVE_FENCE_ANDROID, sa) : EGL_NO_SYNC_KHR;
	if (sync != EGL_NO_SYNC_KHR) {
		waitSync(edpy, sync, 0); /* (the GPU waits, not the CPU) */
		rmSync(edpy, sync);
	} /* (fd -1: already signalled) */
	brHanded = now_ms() - t0;
	glBindFramebuffer(GL_FRAMEBUFFER, fbo);
	glViewport(0, 0, ahbW, ahbH);
	glUseProgram(drawProg);
	glBindVertexArray(drawVao);
	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_2D, ahbTex);
	glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
	GLsync done = glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
	glFlush();
	brGlQueued = now_ms() - t0;
	if (measure) {
		glClientWaitSync(done, GL_SYNC_FLUSH_COMMANDS_BIT, UINT64_MAX);
		brGlDone = now_ms() - t0;
	}
	glDeleteSync(done);
	vkSyncIndex = (vkSyncIndex + 1) % VK_SYNC;
	hwFrame = true;
	fw = ahbW, fh = ahbH;
	return true;
}
