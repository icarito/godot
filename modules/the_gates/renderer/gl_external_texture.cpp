#include "gl_external_texture.h"

#include "../ipc/zmq_runtime.h"
#include "../tg_vformat.h"
#include "core/error_macros.h"
#include "core/print_string.h"
#include "core/variant.h"

#include "zmq.hpp"

#include GLES3_INCLUDE_H

#ifdef OSX_ENABLED
#include <IOSurface/IOSurface.h>

// Declared rather than pulling <OpenGL/OpenGL.h>, whose gltypes.h redefines the
// GL types glad already provides.
extern "C" void *CGLGetCurrentContext(void);
extern "C" int CGLTexImageIOSurface2D(void *ctx, GLenum target, GLenum internal_format, GLsizei width, GLsizei height,
		GLenum format, GLenum type, IOSurfaceRef io_surface, GLuint plane);
#elif WINDOWS_ENABLED
#include "vulkan_memory_probe.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#else
#include "flingfd.h"
#include "vulkan_memory_probe.h"

#include <unistd.h>

#if defined(FRT_ENABLED)
// FRT drives GL through SDL2, so no GLX entry points are in the process.
extern "C" void *SDL_GL_GetProcAddress(const char *);
#else
// Declared rather than pulling <GL/glx.h>, which redefines the GL types glad
// already provides.
extern "C" void *glXGetProcAddressARB(const unsigned char *procName);
#endif
#endif

namespace {

#ifdef OSX_ENABLED
// kCVPixelFormatType_32BGRA and kCVPixelFormatType_32RGBA.
const OSType IOSURFACE_PIXEL_FORMAT_BGRA = 0x42475241;
const OSType IOSURFACE_PIXEL_FORMAT_RGBA = 0x52474241;
#else
// GL_EXT_memory_object with its _fd and _win32 variants. Not in Godot 3's glad.
const GLenum GL_HANDLE_TYPE_OPAQUE_FD = 0x9586;
const GLenum GL_HANDLE_TYPE_OPAQUE_WIN32 = 0x9587;
const GLenum GL_DEDICATED_MEMORY_OBJECT = 0x9581;
const GLenum GL_DEVICE_UUID = 0x9597;
const int GL_UUID_SIZE = 16;

typedef void (*CreateMemoryObjectsFunc)(GLsizei, GLuint *);
typedef void (*MemoryObjectParameterivFunc)(GLuint, GLenum, const GLint *);
typedef void (*ImportMemoryFdFunc)(GLuint, GLuint64, GLenum, GLint);
typedef void (*ImportMemoryWin32HandleFunc)(GLuint, GLuint64, GLenum, void *);
typedef void (*TexStorageMem2DFunc)(GLenum, GLsizei, GLenum, GLsizei, GLsizei, GLuint, GLuint64);
typedef void (*DeleteMemoryObjectsFunc)(GLsizei, const GLuint *);
typedef void (*GetUnsignedByteivIndexedFunc)(GLenum, GLuint, GLubyte *);

CreateMemoryObjectsFunc gl_create_memory_objects = nullptr;
MemoryObjectParameterivFunc gl_memory_object_parameteriv = nullptr;
ImportMemoryFdFunc gl_import_memory_fd = nullptr;
ImportMemoryWin32HandleFunc gl_import_memory_win32_handle = nullptr;
TexStorageMem2DFunc gl_tex_storage_mem_2d = nullptr;
DeleteMemoryObjectsFunc gl_delete_memory_objects = nullptr;
GetUnsignedByteivIndexedFunc gl_get_unsigned_bytei_v = nullptr;

#ifdef WINDOWS_ENABLED
const char *MISSING_EXTENSION_MESSAGE =
		"GL_EXT_memory_object_win32 is unavailable; this GPU driver cannot import the launcher's Vulkan allocation";

void *load_gl_proc(const char *p_name) {
	// wglGetProcAddress reports failure as any of 0, 1, 2, 3 or -1 depending on the driver.
	const intptr_t proc = (intptr_t)wglGetProcAddress(p_name);
	return (proc >= -1 && proc <= 3) ? nullptr : (void *)proc;
}
#else
const char *MISSING_EXTENSION_MESSAGE =
		"GL_EXT_memory_object_fd is unavailable; this GPU driver cannot import the launcher's Vulkan allocation. "
		"Mesa has exposed it since 17.3 (2017) and the NVIDIA proprietary driver since R515 (2022), "
		"so the driver here is older than either, or the context is indirect";

void *load_gl_proc(const char *p_name) {
#if defined(FRT_ENABLED)
	return SDL_GL_GetProcAddress(p_name);
#else
	return glXGetProcAddressARB(reinterpret_cast<const unsigned char *>(p_name));
#endif
}
#endif

bool load_memory_object_extension() {
	if (gl_tex_storage_mem_2d != nullptr) {
		return true;
	}

	gl_create_memory_objects = (CreateMemoryObjectsFunc)load_gl_proc("glCreateMemoryObjectsEXT");
	gl_memory_object_parameteriv = (MemoryObjectParameterivFunc)load_gl_proc("glMemoryObjectParameterivEXT");
#ifdef WINDOWS_ENABLED
	gl_import_memory_win32_handle = (ImportMemoryWin32HandleFunc)load_gl_proc("glImportMemoryWin32HandleEXT");
	const bool has_import = gl_import_memory_win32_handle != nullptr;
#else
	gl_import_memory_fd = (ImportMemoryFdFunc)load_gl_proc("glImportMemoryFdEXT");
	const bool has_import = gl_import_memory_fd != nullptr;
#endif
	gl_tex_storage_mem_2d = (TexStorageMem2DFunc)load_gl_proc("glTexStorageMem2DEXT");
	gl_delete_memory_objects = (DeleteMemoryObjectsFunc)load_gl_proc("glDeleteMemoryObjectsEXT");
	gl_get_unsigned_bytei_v = (GetUnsignedByteivIndexedFunc)load_gl_proc("glGetUnsignedBytei_vEXT");

	return gl_create_memory_objects != nullptr && gl_memory_object_parameteriv != nullptr && has_import &&
			gl_tex_storage_mem_2d != nullptr && gl_delete_memory_objects != nullptr && gl_get_unsigned_bytei_v != nullptr;
}
#endif

} // namespace

#ifdef OSX_ENABLED
bool TGGLExternalTexture::recv_filehandle(const String &p_path) {
	zmq::socket_t sock(tg_zmq_context(), zmq::socket_type::pair);
	sock.bind(p_path.utf8().get_data());

	zmq::message_t msg;
	const bool received = sock.recv(msg).has_value(); // WARNING: BLOCKING COMMAND
	sock.close();
	ERR_FAIL_COND_V_MSG(!received || msg.size() != sizeof(uint32_t), false, "Receive IOSurface id failed");

	uint32_t surface_id = 0;
	memcpy(&surface_id, msg.data(), sizeof(uint32_t));
	surface = IOSurfaceLookup(surface_id);
	ERR_FAIL_COND_V_MSG(surface == nullptr, false, "IOSurfaceLookup failed for id " + itos(surface_id));

	const OSType pixel_format = IOSurfaceGetPixelFormat((IOSurfaceRef)surface);
	ERR_FAIL_COND_V_MSG(pixel_format != IOSURFACE_PIXEL_FORMAT_BGRA && pixel_format != IOSURFACE_PIXEL_FORMAT_RGBA, false,
			tg_vformat("Shared IOSurface has unsupported pixel format 0x%x", (int64_t)pixel_format));
	bgra = pixel_format == IOSURFACE_PIXEL_FORMAT_BGRA;
	return true;
}

bool TGGLExternalTexture::import_platform_texture() {
	ERR_FAIL_COND_V_MSG(surface == nullptr, false, "Receive IOSurface first");
	const IOSurfaceRef io_surface = (IOSurfaceRef)surface;
	ERR_FAIL_COND_V_MSG((int)IOSurfaceGetWidth(io_surface) != width || (int)IOSurfaceGetHeight(io_surface) != height, false,
			tg_vformat("Shared IOSurface is %dx%d, expected %dx%d", (int64_t)IOSurfaceGetWidth(io_surface),
					(int64_t)IOSurfaceGetHeight(io_surface), width, height));

	// macOS GL binds an IOSurface only as a rectangle texture.
	texture_target = GL_TEXTURE_RECTANGLE;
	glGenTextures(1, &texture);
	glBindTexture(GL_TEXTURE_RECTANGLE, texture);
	const int cgl_error = CGLTexImageIOSurface2D(CGLGetCurrentContext(), GL_TEXTURE_RECTANGLE, GL_RGBA, width, height,
			bgra ? GL_BGRA : GL_RGBA, GL_UNSIGNED_INT_8_8_8_8_REV, io_surface, 0);
	glBindTexture(GL_TEXTURE_RECTANGLE, 0);
	ERR_FAIL_COND_V_MSG(cgl_error != 0, false, "CGLTexImageIOSurface2D failed with CGL error " + itos(cgl_error));

	print_line(tg_vformat("[EXT-TEXTURE] imported %dx%d IOSurface (%s)", width, height, bgra ? "BGRA" : "RGBA"));
	return true;
}
#else
#ifdef WINDOWS_ENABLED
bool TGGLExternalTexture::recv_filehandle(const String &p_path) {
	zmq::socket_t sock(tg_zmq_context(), zmq::socket_type::pair);
	sock.bind(p_path.utf8().get_data());

	zmq::message_t msg;
	const bool received = sock.recv(msg).has_value(); // WARNING: BLOCKING COMMAND
	sock.close();
	ERR_FAIL_COND_V_MSG(!received || msg.size() != sizeof(int64_t), false, "Receive Win32 handle failed");

	int64_t data = 0;
	memcpy(&data, msg.data(), sizeof(int64_t));
	win32_handle = (void *)(intptr_t)data;
	return true;
}
#else
bool TGGLExternalTexture::recv_filehandle(const String &p_path) {
	filehandle = flingfd_simple_recv(p_path.utf8().get_data()); // WARNING: BLOCKING COMMAND
	ERR_FAIL_COND_V_MSG(filehandle < 0, false, "Receive filehandle failed");

	// Vulkan hands out its external memory as a dma-buf, and dma-buf implements
	// llseek precisely so importers can size it. Other exports are measured at import.
	const off_t probed = lseek(filehandle, 0, SEEK_END);
	lseek(filehandle, 0, SEEK_SET);
	alloc_size = probed > 0 ? (uint64_t)probed : 0;
	return true;
}
#endif

bool TGGLExternalTexture::import_platform_texture() {
#ifdef WINDOWS_ENABLED
	ERR_FAIL_COND_V_MSG(win32_handle == nullptr, false, "Receive Win32 handle first");
#else
	ERR_FAIL_COND_V_MSG(filehandle < 0, false, "Receive filehandle first");
#endif
	ERR_FAIL_COND_V_MSG(!load_memory_object_extension(), false, MISSING_EXTENSION_MESSAGE);

	if (alloc_size == 0) {
		GLubyte device_uuid[GL_UUID_SIZE] = {};
		gl_get_unsigned_bytei_v(GL_DEVICE_UUID, 0, device_uuid);
		alloc_size = tg_vulkan_shared_image_allocation_size(width, height, device_uuid);
		ERR_FAIL_COND_V_MSG(alloc_size == 0, false,
				"Cannot size the shared allocation: the export reports no length and Vulkan could not measure it");
	}

	gl_create_memory_objects(1, &memory_object);

	// The launcher allocates with VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT,
	// so the handle backs one whole VkDeviceMemory and GL must treat it the same.
	const GLint dedicated = GL_TRUE;
	gl_memory_object_parameteriv(memory_object, GL_DEDICATED_MEMORY_OBJECT, &dedicated);

#ifdef WINDOWS_ENABLED
	// Unlike the fd variant, importing a Win32 handle does not take ownership of it.
	gl_import_memory_win32_handle(memory_object, (GLuint64)alloc_size, GL_HANDLE_TYPE_OPAQUE_WIN32, win32_handle);
	CloseHandle((HANDLE)win32_handle);
	win32_handle = nullptr;
#else
	// glImportMemoryFdEXT takes ownership of the fd.
	gl_import_memory_fd(memory_object, (GLuint64)alloc_size, GL_HANDLE_TYPE_OPAQUE_FD, (GLint)filehandle);
	filehandle = -1;
#endif

	texture_target = GL_TEXTURE_2D;
	glGenTextures(1, &texture);
	glBindTexture(GL_TEXTURE_2D, texture);
	gl_tex_storage_mem_2d(GL_TEXTURE_2D, 1, GL_RGBA8, width, height, memory_object, 0);
	glBindTexture(GL_TEXTURE_2D, 0);

	const GLenum err = glGetError();
	ERR_FAIL_COND_V_MSG(err != GL_NO_ERROR, false,
			"Importing the shared allocation failed with GL error " + itos(err));

	print_line(tg_vformat("[EXT-TEXTURE] imported %dx%d from %d bytes of shared Vulkan memory",
			width, height, (int64_t)alloc_size));
	return true;
}
#endif

bool TGGLExternalTexture::import(int p_width, int p_height) {
	width = p_width;
	height = p_height;

	if (!import_platform_texture()) {
		return false;
	}

	glGenFramebuffers(1, &read_fbo);
	glGenFramebuffers(1, &draw_fbo);

	glBindFramebuffer(GL_DRAW_FRAMEBUFFER, draw_fbo);
	glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, texture_target, texture, 0);
	const GLenum status = glCheckFramebufferStatus(GL_DRAW_FRAMEBUFFER);
	glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
	ERR_FAIL_COND_V_MSG(status != GL_FRAMEBUFFER_COMPLETE, false,
			"Shared texture is not framebuffer-complete, status " + itos(status));

	imported = true;
	return true;
}

bool TGGLExternalTexture::copy_from_texture(unsigned int p_src_texture, int p_src_width, int p_src_height) {
	if (!imported || p_src_texture == 0 || p_src_width <= 0 || p_src_height <= 0) {
		return false;
	}

	glBindFramebuffer(GL_READ_FRAMEBUFFER, read_fbo);
	glFramebufferTexture2D(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, p_src_texture, 0);
	glBindFramebuffer(GL_DRAW_FRAMEBUFFER, draw_fbo);

	// Source rect is flipped in Y: GL's framebuffer origin is bottom-left and
	// the launcher samples the shared image with Vulkan's top-left origin.
	glBlitFramebuffer(0, p_src_height, p_src_width, 0, 0, 0, width, height, GL_COLOR_BUFFER_BIT, GL_LINEAR);

	glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
	glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
	return true;
}

void TGGLExternalTexture::close() {
	if (read_fbo != 0) {
		glDeleteFramebuffers(1, &read_fbo);
		read_fbo = 0;
	}
	if (draw_fbo != 0) {
		glDeleteFramebuffers(1, &draw_fbo);
		draw_fbo = 0;
	}
	if (texture != 0) {
		glDeleteTextures(1, &texture);
		texture = 0;
	}
#ifdef OSX_ENABLED
	if (surface != nullptr) {
		CFRelease((IOSurfaceRef)surface);
		surface = nullptr;
	}
#else
#ifdef WINDOWS_ENABLED
	if (win32_handle != nullptr) {
		CloseHandle((HANDLE)win32_handle);
		win32_handle = nullptr;
	}
#endif
	if (memory_object != 0 && gl_delete_memory_objects != nullptr) {
		gl_delete_memory_objects(1, &memory_object);
		memory_object = 0;
	}
#endif
	imported = false;
}

TGGLExternalTexture::TGGLExternalTexture() {
}

TGGLExternalTexture::~TGGLExternalTexture() {
	close();
}
