/*************************************************************************/
/*  slug_svg.cpp                                                        */
/*************************************************************************/
/* Godot 3.6 module: modules/slug                                       */
/* NanoSVG output (cubic Beziers, transforms already baked) ->          */
/* SlugShape contours via SlugCurveDecomposer.                          */
/*************************************************************************/

#include "slug_svg.h"

#include <string.h>

#include "core/print_string.h"
#include "slug_decomposer.h"

// The engine already bundles NanoSVG (zlib) for modules/svg; its
// implementation lives in thirdparty/nanosvg/nanosvg.cc, so include the
// declarations only and link against that object.
#include "thirdparty/nanosvg/nanosvg.h"

// NanoSVG packs solid fills as 0xAABBGGRR (little-endian ABGR).
static Color _slug_svg_color(unsigned int p_abgr, float p_opacity) {
	float r = (p_abgr & 0xff) / 255.0f;
	float g = ((p_abgr >> 8) & 0xff) / 255.0f;
	float b = ((p_abgr >> 16) & 0xff) / 255.0f;
	float a = (((p_abgr >> 24) & 0xff) / 255.0f) * p_opacity;
	return Color(r, g, b, a);
}

// A cubic whose control points sit on the p0->p3 chord traces that straight
// segment (NanoSVG encodes lines this way), so a single quadratic suffices.
static bool _slug_svg_segment_is_line(const Vector2 &p0, const Vector2 &c1, const Vector2 &c2, const Vector2 &p3) {
	Vector2 d = p3 - p0;
	float len = d.length();
	if (len < 1e-6f) {
		return false;
	}
	Vector2 n(d.y / len, -d.x / len);
	return ABS((c1 - p0).dot(n)) < 1e-4f && ABS((c2 - p0).dot(n)) < 1e-4f;
}

bool slug_parse_svg(const Vector<uint8_t> &p_bytes, Vector<SlugShape> &r_shapes, Vector<Color> &r_colors, Rect2 &r_bounds, String &r_error) {
	r_shapes.clear();
	r_colors.clear();
	r_bounds = Rect2();
	r_error = String();

	if (p_bytes.size() == 0) {
		r_error = "empty SVG";
		return false;
	}

	// nsvgParse wants a mutable, NUL-terminated buffer.
	Vector<uint8_t> buffer;
	buffer.resize(p_bytes.size() + 1);
	memcpy(buffer.ptrw(), p_bytes.ptr(), p_bytes.size());
	buffer.ptrw()[p_bytes.size()] = 0;

	NSVGimage *image = nsvgParse((char *)buffer.ptr(), "px", 96.0f);
	if (image == nullptr) {
		r_error = "NanoSVG could not parse the document";
		return false;
	}

	// Normalize to ~1 unit so SlugCurveDecomposer::FLATNESS (calibrated in em)
	// subdivides at a sane rate: SVG user units can be in the hundreds, and a
	// fixed absolute tolerance would explode the curve count. SlugVector3D
	// rescales to [member size] anyway, so this is invisible to the user.
	float extent = MAX(image->width, image->height);
	float norm = extent > 0.0f ? 1.0f / extent : 1.0f;

	bool first = true;
	for (NSVGshape *ns = image->shapes; ns != nullptr; ns = ns->next) {
		if (!(ns->flags & NSVG_FLAGS_VISIBLE)) {
			continue;
		}
		// Gradients and strokes are not rendered yet; only solid fills.
		if (ns->fill.type != NSVG_PAINT_COLOR || ns->paths == nullptr) {
			continue;
		}

		SlugCurveDecomposer dec;
		bool any = false;
		for (NSVGpath *np = ns->paths; np != nullptr; np = np->next) {
			if (np->npts < 4) {
				continue;
			}
			const float *pts = np->pts;
			// One SlugShape per element: each subpath is its own contour, so a
			// hole with opposite winding cancels under the non-zero fill rule.
			dec.move_to(Vector2(pts[0] * norm, -pts[1] * norm));
			for (int i = 0; i + 3 < np->npts; i += 3) {
				const float *q = pts + (i + 1) * 2;
				Vector2 p0(pts[i * 2] * norm, -pts[i * 2 + 1] * norm);
				Vector2 c1(q[0] * norm, -q[1] * norm);
				Vector2 c2(q[2] * norm, -q[3] * norm);
				Vector2 p3(q[4] * norm, -q[5] * norm);
				if (_slug_svg_segment_is_line(p0, c1, c2, p3)) {
					dec.line_to(p3);
				} else {
					dec.cubic_to(c1, c2, p3);
				}
				any = true;
			}
			if (np->closed) {
				dec.close();
			}
		}

		SlugShape shape = dec.get_shape();
		if (!any || !shape.has_curves()) {
			continue;
		}

		r_shapes.push_back(shape);
		r_colors.push_back(_slug_svg_color(ns->fill.color, ns->opacity));

		Rect2 b = shape.compute_bounds();
		if (first) {
			r_bounds = b;
			first = false;
		} else {
			r_bounds = r_bounds.merge(b);
		}
	}
	nsvgDelete(image);

	if (r_shapes.size() == 0) {
		r_error = "no visible solid-filled paths";
		return false;
	}
	if (r_bounds.size.x <= 0.0f) {
		r_bounds.size.x = 1.0f;
	}
	if (r_bounds.size.y <= 0.0f) {
		r_bounds.size.y = 1.0f;
	}
	return true;
}
