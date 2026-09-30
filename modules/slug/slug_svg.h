/*************************************************************************/
/*  slug_svg.h                                                          */
/*************************************************************************/
/* Godot 3.6 module: modules/slug                                       */
/* SVG -> SlugShape. Vendors NanoSVG (zlib, Mikko Mononen) for parsing  */
/* and bakes every element/group transform into the points.             */
/*************************************************************************/

#ifndef SLUG_SVG_H
#define SLUG_SVG_H

#include "core/color.h"
#include "core/math/rect2.h"
#include "core/ustring.h"
#include "core/vector.h"
#include "slug_shape.h"

// Parses an SVG document into filled regions. Every shape element with a solid
// fill becomes one SlugShape: all of its subpaths (including holes) end up as
// contours of the same shape, so non-zero winding cancels them as authored.
// Y is flipped so the result is y-up, matching the font/text pipeline.
//
// Returns false and sets p_error when nothing renderable is found.
bool slug_parse_svg(const Vector<uint8_t> &p_bytes, Vector<SlugShape> &r_shapes, Vector<Color> &r_colors, Rect2 &r_bounds, String &r_error);

#endif // SLUG_SVG_H
