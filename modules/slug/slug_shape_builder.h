/*************************************************************************/
/*  slug_shape_builder.h                                                */
/*************************************************************************/
/* Godot 3.6 module: modules/slug                                       */
/* Packs SlugShape curves into the curve/band/glyph textures that       */
/* slug_shader.h reads. Backend-agnostic: fonts and SVG paths both go   */
/* through here, one glyph_tex row per shape.                            */
/*************************************************************************/

#ifndef SLUG_SHAPE_BUILDER_H
#define SLUG_SHAPE_BUILDER_H

#include "scene/resources/material.h"
#include "scene/resources/texture.h"
#include "slug_shape.h"

struct SlugAtlasData {
	Ref<ImageTexture> curve_tex;
	Ref<ImageTexture> band_tex;
	Ref<ImageTexture> glyph_tex;
	Ref<ShaderMaterial> material;
	int curve_rows = 0;
	int band_rows = 0;
	int max_curves_per_band = 0;
};

class SlugShapeBuilder {
public:
	// Shape i lives in glyph_tex row i, so callers keep their own index map.
	static SlugAtlasData build(const Vector<SlugShape> &p_shapes);
};

#endif // SLUG_SHAPE_BUILDER_H
