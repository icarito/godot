/*************************************************************************/
/*  slug_svg.h                                                          */
/*************************************************************************/
/* Godot 3.6 module: modules/slug                                       */
/* SVG -> SlugShape. Vendors NanoSVG (zlib, Mikko Mononen) for parsing  */
/* and bakes every element/group transform into the points.             */
/* Sugar-style color roles (fill_color / stroke_color entities or the   */
/* --fill-color / --stroke-color CSS variables) are resolved to roles   */
/* so the host can recolor them at runtime. Solid paints and linear/    */
/* radial gradients are supported.                                      */
/*************************************************************************/

#ifndef SLUG_SVG_H
#define SLUG_SVG_H

#include "core/color.h"
#include "core/math/rect2.h"
#include "core/ustring.h"
#include "core/vector.h"
#include "slug_shape.h"

// Where a shape's (or gradient stop's) color comes from.
enum SlugPaintRole {
	SLUG_PAINT_LITERAL = 0, // A color authored in the SVG.
	SLUG_PAINT_FILL = 1, // The Sugar fill_color role.
	SLUG_PAINT_STROKE = 2, // The Sugar stroke_color role.
};

struct SlugGradientStop {
	float offset = 0.0f;
	int role = SLUG_PAINT_LITERAL;
	Color color; // Valid when role == SLUG_PAINT_LITERAL.
};

// A linear (kind 1) or radial (kind 2) gradient, already in normalized, y-up
// shape space. `m`/`t` are the affine that maps a shape-space point into the
// gradient's local space (NanoSVG bakes the inverse basis there); the local
// point's `.y` is the linear parameter and its length is the radial one.
struct SlugGradientDef {
	int kind = 1;
	int spread = 0; // 0 pad, 1 reflect, 2 repeat
	float m[4] = { 1.0f, 0.0f, 0.0f, 1.0f }; // row-major m00, m01, m10, m11
	float t[2] = { 0.0f, 0.0f }; // translation
	Vector<SlugGradientStop> stops;
};

struct SlugPaint {
	int role = SLUG_PAINT_LITERAL; // For solid paints.
	Color color; // Valid for solid literal paints.
	int gradient = -1; // Index into SlugSvgData::gradients, or -1.
	int fill_rule = 0; // 0 non-zero, 1 even-odd.
};

struct SlugSvgData {
	Vector<SlugShape> shapes;
	Vector<SlugPaint> paints;
	Vector<SlugGradientDef> gradients;
	Rect2 bounds;
	// Defaults declared by the SVG (entity value or var() fallback).
	Color default_fill = Color(1, 1, 1, 1);
	Color default_stroke = Color(0, 0, 0, 1);
	bool has_fill_role = false;
	bool has_stroke_role = false;
};

// Parses an SVG document into filled regions, one SlugShape per painted
// element (fill and stroke become separate shapes; stroke is converted to a
// fill outline). Y is flipped so the result is y-up. Returns false and sets
// p_error when nothing renderable is found.
bool slug_parse_svg(const Vector<uint8_t> &p_bytes, SlugSvgData &r_data, String &r_error);

#endif // SLUG_SVG_H
