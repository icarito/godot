/*************************************************************************/
/*  slug_backend.h                                                      */
/*************************************************************************/
/* Godot 3.6 module: modules/slug                                       */
/* Capacidad real del backend activo, no el enum que reporta OS.        */
/*************************************************************************/

#ifndef SLUG_BACKEND_H
#define SLUG_BACKEND_H

#include "servers/visual_server.h"

// Slug necesita un backend de clase GLES3: texelFetch, floatBitsToUint y
// texturas float (RGBAF/RGF) con filtro nearest. En este fork ese backend es
// exactamente el rasterizador GLES3: `is_low_end()` devuelve false solo para
// RasterizerGLES3, y true para GLES2 y el dummy (ver rasterizer_gles3.h,
// rasterizer_gles2.h y rasterizer_dummy.h).
//
// Se consulta al VisualServer en vez de a OS::get_current_video_driver()
// porque ese enum puede venir mal reportado por la plataforma (FRT/arm64
// devolvia VIDEO_DRIVER_GLES2 con un contexto ES 3 activo) y apagaba Slug de
// mas. La capacidad del backend no depende del reporte de OS.
static inline bool slug_backend_supports_float_textures() {
	VisualServer *vs = VisualServer::get_singleton();
	return vs && !vs->is_low_end();
}

#endif // SLUG_BACKEND_H
