/*************************************************************************/
/*  slug_shape.h                                                        */
/*************************************************************************/
/* Godot 3.6 module: modules/slug                                       */
/* Shape model shared by every curve producer (FreeType today, SVG      */
/* paths next): quadratic Bezier contours plus the helpers the builder  */
/* and the label need from them.                                        */
/*************************************************************************/

#ifndef SLUG_SHAPE_H
#define SLUG_SHAPE_H

#include "core/math/rect2.h"
#include "core/math/vector2.h"
#include "core/vector.h"

// One quadratic Bezier in shape space (em for fonts, user units for SVG).
// A straight line is the degenerate {p1, p2, p2} the reference Slug uses.
struct SlugQCurve {
	Vector2 p1, p2, p3;
};

struct SlugContour {
	Vector<SlugQCurve> curves;
};

// A drawable region: a set of contours whose curves share one band grid.
// A font glyph, an SVG path and a single icon all reduce to this.
struct SlugShape {
	Vector<SlugContour> contours;

	bool has_curves() const {
		for (int i = 0; i < contours.size(); i++) {
			if (contours[i].curves.size() > 0) {
				return true;
			}
		}
		return false;
	}

	// Conservative bounds: the convex hull of the control points contains the
	// curve, so expanding over p1/p2/p3 covers the real outline.
	Rect2 compute_bounds() const {
		bool first = true;
		Rect2 bounds;
		for (int ci = 0; ci < contours.size(); ci++) {
			const Vector<SlugQCurve> &curves = contours[ci].curves;
			for (int qi = 0; qi < curves.size(); qi++) {
				const Vector2 pts[3] = { curves[qi].p1, curves[qi].p2, curves[qi].p3 };
				for (int pi = 0; pi < 3; pi++) {
					if (first) {
						bounds = Rect2(pts[pi], Size2());
						first = false;
					} else {
						bounds.expand_to(pts[pi]);
					}
				}
			}
		}
		return bounds;
	}
};

#endif // SLUG_SHAPE_H
