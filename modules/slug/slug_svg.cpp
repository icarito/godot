/*************************************************************************/
/*  slug_svg.cpp                                                        */
/*************************************************************************/
/* Godot 3.6 module: modules/slug                                       */
/* NanoSVG output (cubic Beziers, transforms already baked) ->          */
/* SlugShape contours via SlugCurveDecomposer, plus stroke-to-fill for  */
/* stroked elements. Sugar color roles are detected by sentinel colors  */
/* substituted for &fill_color;/&stroke_color; and var(--fill-color)/   */
/* var(--stroke-color).                                                 */
/*************************************************************************/

#include "slug_svg.h"

#include <string.h>

#include "core/print_string.h"
#include "slug_decomposer.h"
#include "slug_stroke.h"
#include "thirdparty/nanosvg/nanosvg.h"

// The engine already bundles NanoSVG (zlib) for modules/svg; its
// implementation lives in thirdparty/nanosvg/nanosvg.cc, so this only needs
// the declarations.
#define SLUG_SVG_FILL_SENTINEL 0x0C0B0AU // #0a0b0c
#define SLUG_SVG_STROKE_SENTINEL 0x0D0E0FU // #0d0e0f

static const float SLUG_SVG_FLATTEN_MIN = 0.0015f;

static float _slug_dist_sq_to_line(const Vector2 &p, const Vector2 &a, const Vector2 &b) {
	Vector2 d = b - a;
	float len_sq = d.length_squared();
	if (len_sq < 1e-12f) {
		return (p - a).length_squared();
	}
	float cross = d.x * (a.y - p.y) - d.y * (a.x - p.x);
	return (cross * cross) / len_sq;
}

static void _slug_flatten_cubic(const Vector2 &p0, const Vector2 &p1, const Vector2 &p2, const Vector2 &p3, float p_tol_sq, int p_depth, Vector<Vector2> &r_out) {
	if (p_depth >= 8 || (_slug_dist_sq_to_line(p1, p0, p3) <= p_tol_sq && _slug_dist_sq_to_line(p2, p0, p3) <= p_tol_sq)) {
		r_out.push_back(p3);
		return;
	}
	Vector2 m01 = (p0 + p1) * 0.5f;
	Vector2 m12 = (p1 + p2) * 0.5f;
	Vector2 m23 = (p2 + p3) * 0.5f;
	Vector2 m012 = (m01 + m12) * 0.5f;
	Vector2 m123 = (m12 + m23) * 0.5f;
	Vector2 m0123 = (m012 + m123) * 0.5f;
	_slug_flatten_cubic(p0, m01, m012, m0123, p_tol_sq, p_depth + 1, r_out);
	_slug_flatten_cubic(m0123, m123, m23, p3, p_tol_sq, p_depth + 1, r_out);
}

// NanoSVG packs solid fills as 0xAABBGGRR (little-endian ABGR).
static Color _slug_svg_color(unsigned int p_abgr, float p_opacity) {
	float r = (p_abgr & 0xff) / 255.0f;
	float g = ((p_abgr >> 8) & 0xff) / 255.0f;
	float b = ((p_abgr >> 16) & 0xff) / 255.0f;
	float a = (((p_abgr >> 24) & 0xff) / 255.0f) * p_opacity;
	return Color(r, g, b, a);
}

static int _slug_hex_nibble(CharType c) {
	if (c >= '0' && c <= '9') {
		return c - '0';
	}
	if (c >= 'a' && c <= 'f') {
		return c - 'a' + 10;
	}
	if (c >= 'A' && c <= 'F') {
		return c - 'A' + 10;
	}
	return -1;
}

static bool _slug_svg_parse_color(const String &p_str, Color &r_out) {
	String s = p_str.strip_edges();
	if (!s.begins_with("#")) {
		return false;
	}
	String h = s.substr(1);
	if (h.length() == 3) {
		h = h.substr(0, 1) + h.substr(0, 1) + h.substr(1, 1) + h.substr(1, 1) + h.substr(2, 1) + h.substr(2, 1);
	}
	if (h.length() != 6) {
		return false;
	}
	int comp[3];
	for (int i = 0; i < 3; i++) {
		int hi = _slug_hex_nibble(h[i * 2]);
		int lo = _slug_hex_nibble(h[i * 2 + 1]);
		if (hi < 0 || lo < 0) {
			return false;
		}
		comp[i] = hi * 16 + lo;
	}
	r_out = Color(comp[0] / 255.0f, comp[1] / 255.0f, comp[2] / 255.0f, 1.0f);
	return true;
}

// Finds <!ENTITY name "value">; ignores the &name; references in the body.
static bool _slug_svg_find_entity(const String &p_text, const String &p_name, String &r_out) {
	int idx = p_text.find(p_name);
	while (idx >= 0) {
		int i = idx + p_name.length();
		while (i < p_text.length() && (p_text[i] == ' ' || p_text[i] == '\t' || p_text[i] == '=')) {
			i++;
		}
		if (i < p_text.length() && (p_text[i] == '\"' || p_text[i] == '\'')) {
			CharType quote = p_text[i];
			int e = p_text.find(String::chr(quote), i + 1);
			if (e > i) {
				r_out = p_text.substr(i + 1, e - i - 1);
				return true;
			}
		}
		idx = p_text.find(p_name, idx + 1);
	}
	return false;
}

// Replaces var(--fill-color) / var(--fill-color, fallback) with a sentinel.
static String _slug_svg_replace_var(const String &p_text, const String &p_prefix, const String &p_sentinel, Color &r_default, bool &r_found) {
	String out = p_text;
	int idx = out.find(p_prefix);
	while (idx >= 0) {
		int open = out.find("(", idx);
		int close = open >= 0 ? out.find(")", open) : -1;
		if (open < 0 || close < 0) {
			break;
		}
		String content = out.substr(open + 1, close - open - 1); // --fill-color[, fallback]
		int comma = content.find(",");
		if (comma >= 0) {
			Color c;
			if (_slug_svg_parse_color(content.substr(comma + 1), c)) {
				r_default = c;
			}
		}
		out = out.substr(0, idx) + p_sentinel + out.substr(close + 1);
		r_found = true;
		idx = out.find(p_prefix, idx + p_sentinel.length());
	}
	return out;
}

static String _slug_svg_strip_doctype(const String &p_text) {
	int start = p_text.find("<!DOCTYPE");
	if (start < 0) {
		return p_text;
	}
	int end = p_text.find("]>", start);
	int skip = 2;
	if (end < 0) {
		end = p_text.find(">", start);
		skip = 1;
	}
	if (end < 0) {
		return p_text;
	}
	return p_text.substr(0, start) + p_text.substr(end + skip);
}

bool slug_parse_svg(const Vector<uint8_t> &p_bytes, SlugSvgData &r_data, String &r_error) {
	r_data = SlugSvgData();
	r_error = String();

	if (p_bytes.size() == 0) {
		r_error = "empty SVG";
		return false;
	}

	String text = String::utf8((const char *)p_bytes.ptr(), p_bytes.size());

	Color def_fill(1, 1, 1, 1);
	Color def_stroke(0, 0, 0, 1);
	String entity_value;
	if (_slug_svg_find_entity(text, "fill_color", entity_value)) {
		Color c;
		if (_slug_svg_parse_color(entity_value, c)) {
			def_fill = c;
		}
	}
	if (_slug_svg_find_entity(text, "stroke_color", entity_value)) {
		Color c;
		if (_slug_svg_parse_color(entity_value, c)) {
			def_stroke = c;
		}
	}

	bool var_fill = false;
	bool var_stroke = false;
	text = _slug_svg_replace_var(text, "var(--fill-color", "#0a0b0c", def_fill, var_fill);
	text = _slug_svg_replace_var(text, "var(--stroke-color", "#0d0e0f", def_stroke, var_stroke);

	bool ent_fill = text.find("&fill_color;") >= 0;
	bool ent_stroke = text.find("&stroke_color;") >= 0;
	text = text.replace("&fill_color;", "#0a0b0c");
	text = text.replace("&stroke_color;", "#0d0e0f");
	text = _slug_svg_strip_doctype(text);

	r_data.default_fill = def_fill;
	r_data.default_stroke = def_stroke;
	r_data.has_fill_role = ent_fill || var_fill;
	r_data.has_stroke_role = ent_stroke || var_stroke;

	CharString cs = text.utf8();
	Vector<uint8_t> buffer;
	int text_len = (int)strlen(cs.get_data());
	buffer.resize(text_len + 1);
	memcpy(buffer.ptrw(), cs.get_data(), text_len);
	buffer.ptrw()[text_len] = 0;
	NSVGimage *image = nsvgParse((char *)buffer.ptrw(), "px", 96.0f);
	if (image == nullptr) {
		r_error = "NanoSVG could not parse the document";
		return false;
	}

	// Normalize to ~1 unit so SlugCurveDecomposer::FLATNESS (calibrated in em)
	// subdivides at a sane rate; SlugVector3D rescales to size anyway.
	float extent = MAX(image->width, image->height);
	float norm = extent > 0.0f ? 1.0f / extent : 1.0f;

	bool first = true;
	for (NSVGshape *ns = image->shapes; ns != nullptr; ns = ns->next) {
		if (!(ns->flags & NSVG_FLAGS_VISIBLE)) {
			continue;
		}

		int fill_role = -1;
		Color fill_literal;
		if (ns->fill.type == NSVG_PAINT_COLOR) {
			if ((ns->fill.color & 0xFFFFFFu) == (SLUG_SVG_FILL_SENTINEL & 0xFFFFFFu)) {
				fill_role = SLUG_PAINT_FILL;
			} else {
				fill_role = SLUG_PAINT_LITERAL;
				fill_literal = _slug_svg_color(ns->fill.color, ns->opacity);
			}
		}

		int stroke_role = -1;
		Color stroke_literal;
		if (ns->stroke.type == NSVG_PAINT_COLOR && ns->strokeWidth > 0.0f) {
			if ((ns->stroke.color & 0xFFFFFFu) == (SLUG_SVG_STROKE_SENTINEL & 0xFFFFFFu)) {
				stroke_role = SLUG_PAINT_STROKE;
			} else {
				stroke_role = SLUG_PAINT_LITERAL;
				stroke_literal = _slug_svg_color(ns->stroke.color, ns->opacity);
			}
		}

		if ((fill_role < 0 && stroke_role < 0) || ns->paths == nullptr) {
			continue;
		}

		float width = ns->strokeWidth * norm;
		float tol_sq = MAX(SLUG_SVG_FLATTEN_MIN, width * 0.15f);
		tol_sq *= tol_sq;

		SlugCurveDecomposer dec;
		Vector<Vector<Vector2> > polylines;
		Vector<bool> poly_closed;
		bool any = false;
		for (NSVGpath *np = ns->paths; np != nullptr; np = np->next) {
			if (np->npts < 4) {
				continue;
			}
			const float *pts = np->pts;
			Vector2 start(pts[0] * norm, -pts[1] * norm);
			// One SlugShape per element: each subpath is its own contour, so a
			// hole with opposite winding cancels under the non-zero fill rule.
			dec.move_to(start);
			Vector<Vector2> poly;
			poly.push_back(start);
			for (int i = 0; i + 3 < np->npts; i += 3) {
				const float *q = pts + (i + 1) * 2;
				Vector2 p0(pts[i * 2] * norm, -pts[i * 2 + 1] * norm);
				Vector2 c1(q[0] * norm, -q[1] * norm);
				Vector2 c2(q[2] * norm, -q[3] * norm);
				Vector2 p3(q[4] * norm, -q[5] * norm);
				if (_slug_dist_sq_to_line(c1, p0, p3) < 1e-10f && _slug_dist_sq_to_line(c2, p0, p3) < 1e-10f) {
					// NanoSVG encodes lines as cubics with collinear control points.
					dec.line_to(p3);
					poly.push_back(p3);
				} else {
					dec.cubic_to(c1, c2, p3);
					_slug_flatten_cubic(p0, c1, c2, p3, tol_sq, 0, poly);
				}
				any = true;
			}
			if (np->closed) {
				dec.close();
			}
			polylines.push_back(poly);
			poly_closed.push_back(np->closed != 0);
		}
		if (!any) {
			continue;
		}

		// Fill first, stroke on top: SVG paints fill, then stroke.
		SlugShape fill_shape = dec.get_shape();
		if (fill_role >= 0 && fill_shape.has_curves()) {
			Rect2 b = fill_shape.compute_bounds();
			r_data.shapes.push_back(fill_shape);
			SlugPaint paint;
			paint.role = fill_role;
			paint.color = fill_literal;
			r_data.paints.push_back(paint);
			r_data.bounds = first ? b : r_data.bounds.merge(b);
			first = false;
		}
		if (stroke_role >= 0) {
			SlugShape stroke_shape;
			for (int li = 0; li < polylines.size(); li++) {
				slug_build_stroke(polylines[li], poly_closed[li], width, ns->strokeLineJoin, ns->strokeLineCap, ns->miterLimit, stroke_shape.contours);
			}
			if (stroke_shape.has_curves()) {
				Rect2 b = stroke_shape.compute_bounds();
				r_data.shapes.push_back(stroke_shape);
				SlugPaint paint;
				paint.role = stroke_role;
				paint.color = stroke_literal;
				r_data.paints.push_back(paint);
				r_data.bounds = first ? b : r_data.bounds.merge(b);
				first = false;
			}
		}
	}
	nsvgDelete(image);

	if (r_data.shapes.size() == 0) {
		r_error = "no visible solid-filled paths";
		return false;
	}
	if (r_data.bounds.size.x <= 0.0f) {
		r_data.bounds.size.x = 1.0f;
	}
	if (r_data.bounds.size.y <= 0.0f) {
		r_data.bounds.size.y = 1.0f;
	}
	return true;
}
