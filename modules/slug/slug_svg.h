/*************************************************************************/
/*  slug_svg.h                                                          */
/*************************************************************************/
/* Godot 3.6 module: modules/slug                                       */
/* SVG -> SlugShape. Vendors NanoSVG (zlib, Mikko Mononen) for parsing  */
/* and bakes every element/group transform into the points.             */
/* Sugar-style color roles (fill_color / stroke_color entities or the   */
/* --fill-color / --stroke-color CSS variables) are resolved to roles   */
/* so the host can recolor them at runtime.                             */
/*************************************************************************/

#ifndef SLUG_SVG_H
#define SLUG_SVG_H

#include "core/color.h"
#include "core/math/rect2.h"
#include "core/ustring.h"
#include "core/vector.h"
#include "slug_shape.h"

// Where a shape's color comes from.
enum SlugPaintRole {
	SLUG_PAINT_LITERAL = 0, // A color authored in the SVG.
	SLUG_PAINT_FILL = 1, // The Sugar fill_color role.
	SLUG_PAINT_STROKE = 2, // The Sugar stroke_color role.
};

struct SlugPaint {
	int role = SLUG_PAINT_LITERAL;
	Color color; // Valid when role == SLUG_PAINT_LITERAL.
};

struct SlugSvgData {
	Vector<SlugShape> shapes;
	Vector<SlugPaint> paints;
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
