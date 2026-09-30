/*************************************************************************/
/*  slug_stroke.h                                                       */
/*************************************************************************/
/* Godot 3.6 module: modules/slug                                       */
/* Stroke-to-fill: builds the filled outline of a stroked polyline so   */
/* the Slug shader can draw strokes with the same AA and                 */
/* resolution-independence as fills.                                    */
/*************************************************************************/

#ifndef SLUG_STROKE_H
#define SLUG_STROKE_H

#include "slug_shape.h"

// Match the NSVG_JOIN_*/NSVG_CAP_* values so callers can forward them.
enum SlugStrokeJoin {
	SLUG_STROKE_JOIN_MITER = 0,
	SLUG_STROKE_JOIN_ROUND = 1,
	SLUG_STROKE_JOIN_BEVEL = 2,
};

enum SlugStrokeCap {
	SLUG_STROKE_CAP_BUTT = 0,
	SLUG_STROKE_CAP_ROUND = 1,
	SLUG_STROKE_CAP_SQUARE = 2,
};

// Appends the stroke of one flattened subpath to r_out as filled contours
// (one outline of p_width centered on the polyline). Every contour is emitted
// with the same winding, so overlaps union under the non-zero fill rule.
// p_points must be in shape units; p_closed paths get no caps and join at the
// seam. miter defaults to 4 when <= 0.
void slug_build_stroke(const Vector<Vector2> &p_points, bool p_closed, float p_width,
		int p_join, int p_cap, float p_miter_limit, Vector<SlugContour> &r_out);

#endif // SLUG_STROKE_H
