/*************************************************************************/
/*  slug_svg.cpp                                                        */
/*************************************************************************/
/* Godot 3.6 module: modules/slug                                       */
/* NanoSVG output (cubic Beziers, transforms already baked) ->          */
/* SlugShape contours via SlugCurveDecomposer, plus stroke-to-fill for  */
/* stroked elements. Sugar color roles are detected by sentinel colors  */
/* substituted for &fill_color;/&stroke_color; and var(--fill-color)/   */
/* var(--stroke-color). Solid paints and linear/radial gradients are    */
/* supported; gradients are exported as (inverse basis, center, stops). */
/*************************************************************************/

#include "slug_svg.h"

#include <string.h>

#include "core/math/math_funcs.h"
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

static int _slug_svg_role(unsigned int p_abgr) {
	if ((p_abgr & 0xFFFFFFu) == (SLUG_SVG_FILL_SENTINEL & 0xFFFFFFu)) {
		return SLUG_PAINT_FILL;
	}
	if ((p_abgr & 0xFFFFFFu) == (SLUG_SVG_STROKE_SENTINEL & 0xFFFFFFu)) {
		return SLUG_PAINT_STROKE;
	}
	return SLUG_PAINT_LITERAL;
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

// Copies a NanoSVG gradient into a SlugGradientDef in normalized, y-up space,
// deduplicating by the source pointer.
static int _slug_svg_gradient(NSVGgradient *p_g, int p_type, float p_norm,
		Vector<SlugGradientDef> &r_defs, Vector<NSVGgradient *> &r_ptrs) {
	for (int i = 0; i < r_ptrs.size(); i++) {
		if (r_ptrs[i] == p_g) {
			return i;
		}
	}

	SlugGradientDef def;
	def.kind = (p_type == NSVG_PAINT_LINEAR_GRADIENT) ? 1 : 2;
	def.spread = p_g->spread;
	// NanoSVG stores the affine that maps a user point into the gradient's
	// local space. Compose it with S^-1 (our normalization + y flip): a shape
	// point p' corresponds to user p = (p'.x / norm, -p'.y / norm).
	float a = p_g->xform[0];
	float b = p_g->xform[1];
	float c = p_g->xform[2];
	float d = p_g->xform[3];
	def.m[0] = a / p_norm; // m00
	def.m[1] = -c / p_norm; // m01
	def.m[2] = b / p_norm; // m10
	def.m[3] = -d / p_norm; // m11
	def.t[0] = p_g->xform[4];
	def.t[1] = p_g->xform[5];

	for (int i = 0; i < p_g->nstops; i++) {
		SlugGradientStop stop;
		stop.offset = p_g->stops[i].offset;
		stop.role = _slug_svg_role(p_g->stops[i].color);
		if (stop.role == SLUG_PAINT_LITERAL) {
			stop.color = _slug_svg_color(p_g->stops[i].color, 1.0f);
		}
		def.stops.push_back(stop);
	}
	// NanoSVG inserts stops sorted, but keep it robust.
	for (int i = 1; i < def.stops.size(); i++) {
		SlugGradientStop s = def.stops[i];
		int j = i - 1;
		while (j >= 0 && def.stops[j].offset > s.offset) {
			def.stops.write[j + 1] = def.stops[j];
			j--;
		}
		def.stops.write[j + 1] = s;
	}

	r_defs.push_back(def);
	r_ptrs.push_back(p_g);
	return r_defs.size() - 1;
}

static bool _slug_svg_make_paint(const NSVGpaint &p_paint, float p_opacity, float p_norm, int p_fill_rule,
		Vector<SlugGradientDef> &r_defs, Vector<NSVGgradient *> &r_ptrs, SlugPaint &r_paint) {
	if (p_paint.type == NSVG_PAINT_COLOR) {
		r_paint.role = _slug_svg_role(p_paint.color);
		if (r_paint.role == SLUG_PAINT_LITERAL) {
			r_paint.color = _slug_svg_color(p_paint.color, p_opacity);
		}
		r_paint.fill_rule = p_fill_rule;
		return true;
	}
	if (p_paint.type == NSVG_PAINT_LINEAR_GRADIENT || p_paint.type == NSVG_PAINT_RADIAL_GRADIENT) {
		if (p_paint.gradient == nullptr) {
			return false;
		}
		r_paint.gradient = _slug_svg_gradient(p_paint.gradient, p_paint.type, p_norm, r_defs, r_ptrs);
		r_paint.fill_rule = p_fill_rule;
		return true;
	}
	return false;
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

	Vector<NSVGgradient *> grad_ptrs;

	bool first = true;
	for (NSVGshape *ns = image->shapes; ns != nullptr; ns = ns->next) {
		if (!(ns->flags & NSVG_FLAGS_VISIBLE)) {
			continue;
		}

		SlugPaint fill_paint;
		bool fill_active = _slug_svg_make_paint(ns->fill, ns->opacity, norm, ns->fillRule, r_data.gradients, grad_ptrs, fill_paint);
		SlugPaint stroke_paint;
		bool stroke_active = ns->strokeWidth > 0.0f && _slug_svg_make_paint(ns->stroke, ns->opacity, norm, 0, r_data.gradients, grad_ptrs, stroke_paint);

		if ((!fill_active && !stroke_active) || ns->paths == nullptr) {
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
		if (fill_active && fill_shape.has_curves()) {
			Rect2 b = fill_shape.compute_bounds();
			r_data.shapes.push_back(fill_shape);
			r_data.paints.push_back(fill_paint);
			r_data.bounds = first ? b : r_data.bounds.merge(b);
			first = false;
		}
		if (stroke_active) {
			SlugShape stroke_shape;
			for (int li = 0; li < polylines.size(); li++) {
				slug_build_stroke(polylines[li], poly_closed[li], width, ns->strokeLineJoin, ns->strokeLineCap, ns->miterLimit, stroke_shape.contours);
			}
			if (stroke_shape.has_curves()) {
				Rect2 b = stroke_shape.compute_bounds();
				r_data.shapes.push_back(stroke_shape);
				r_data.paints.push_back(stroke_paint);
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

	bool stop_fill = false;
	bool stop_stroke = false;
	for (int gi = 0; gi < r_data.gradients.size(); gi++) {
		for (int si = 0; si < r_data.gradients[gi].stops.size(); si++) {
			if (r_data.gradients[gi].stops[si].role == SLUG_PAINT_FILL) {
				stop_fill = true;
			} else if (r_data.gradients[gi].stops[si].role == SLUG_PAINT_STROKE) {
				stop_stroke = true;
			}
		}
	}
	r_data.has_fill_role = ent_fill || var_fill || stop_fill;
	r_data.has_stroke_role = ent_stroke || var_stroke || stop_stroke;

	if (r_data.bounds.size.x <= 0.0f) {
		r_data.bounds.size.x = 1.0f;
	}
	if (r_data.bounds.size.y <= 0.0f) {
		r_data.bounds.size.y = 1.0f;
	}
	return true;
}
