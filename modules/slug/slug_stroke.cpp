/*************************************************************************/
/*  slug_stroke.cpp                                                     */
/*************************************************************************/
/* Godot 3.6 module: modules/slug                                       */
/*                                                                      */
/* A stroked polyline is emitted as a union of same-winding contours:   */
/* one quad per edge, a join wedge per vertex and caps at the ends.     */
/* Because every contour winds the same way, their overlaps add up      */
/* under the non-zero fill rule instead of cancelling, which makes the  */
/* union robust without a general polygon clipper. The outlines are     */
/* exact line segments, so the Slug shader still anti-aliases them.     */
/*************************************************************************/

#include "slug_stroke.h"

#include <math.h>

#include "core/math/math_funcs.h"

static float _slug_wrap_pi(float p_a) {
	while (p_a > (float)Math_PI) {
		p_a -= 2.0f * (float)Math_PI;
	}
	while (p_a <= -(float)Math_PI) {
		p_a += 2.0f * (float)Math_PI;
	}
	return p_a;
}

// Appends a closed polygon, dropping repeated points and forcing CCW winding.
static void _slug_append_polygon(const Vector<Vector2> &p_points, Vector<SlugContour> &r_out) {
	Vector<Vector2> pts;
	for (int i = 0; i < p_points.size(); i++) {
		if (pts.size() > 0 && pts[pts.size() - 1].distance_squared_to(p_points[i]) < 1e-12f) {
			continue;
		}
		pts.push_back(p_points[i]);
	}
	while (pts.size() >= 2 && pts[0].distance_squared_to(pts[pts.size() - 1]) < 1e-12f) {
		pts.remove(pts.size() - 1);
	}
	if (pts.size() < 3) {
		return;
	}

	float area2 = 0.0f;
	for (int i = 0; i < pts.size(); i++) {
		const Vector2 &a = pts[i];
		const Vector2 &b = pts[(i + 1) % pts.size()];
		area2 += a.x * b.y - b.x * a.y;
	}
	if (Math::abs(area2) < 1e-12f) {
		return;
	}
	if (area2 < 0.0f) {
		Vector<Vector2> rev;
		rev.resize(pts.size());
		for (int i = 0; i < pts.size(); i++) {
			rev.write[i] = pts[pts.size() - 1 - i];
		}
		pts = rev;
	}

	SlugContour contour;
	for (int i = 0; i < pts.size(); i++) {
		SlugQCurve q;
		q.p1 = pts[i];
		q.p2 = pts[(i + 1) % pts.size()];
		q.p3 = q.p2;
		contour.curves.push_back(q);
	}
	r_out.push_back(contour);
}

// Circular sector (fan from p_center) swept the short way from p_a0 to p_a1.
static void _slug_append_sector(const Vector2 &p_center, float p_a0, float p_a1, float p_radius, Vector<SlugContour> &r_out) {
	float delta = _slug_wrap_pi(p_a1 - p_a0);
	if (Math::abs(delta) < 1e-4f) {
		return;
	}
	int steps = MAX(1, (int)Math::ceil(Math::abs(delta) / 0.35f));
	Vector<Vector2> pts;
	pts.push_back(p_center);
	for (int i = 0; i <= steps; i++) {
		float a = p_a0 + delta * (float)i / steps;
		pts.push_back(p_center + Vector2(Math::cos(a), Math::sin(a)) * p_radius);
	}
	_slug_append_polygon(pts, r_out);
}

// Half-disk centered at p, opening towards p_out_dir.
static void _slug_append_round_cap(const Vector2 &p, const Vector2 &p_out_dir, float p_radius, Vector<SlugContour> &r_out) {
	float base = Math::atan2(p_out_dir.y, p_out_dir.x);
	int steps = 8;
	Vector<Vector2> pts;
	pts.push_back(p);
	for (int i = 0; i <= steps; i++) {
		float a = base - (float)Math_PI * 0.5f + (float)Math_PI * (float)i / steps;
		pts.push_back(p + Vector2(Math::cos(a), Math::sin(a)) * p_radius);
	}
	_slug_append_polygon(pts, r_out);
}

// Full disk (used for 180 degree reversals, where no outer side is defined).
static void _slug_append_disk(const Vector2 &p_center, float p_radius, Vector<SlugContour> &r_out) {
	int steps = 12;
	Vector<Vector2> pts;
	for (int i = 0; i < steps; i++) {
		float a = 2.0f * (float)Math_PI * (float)i / steps;
		pts.push_back(p_center + Vector2(Math::cos(a), Math::sin(a)) * p_radius);
	}
	_slug_append_polygon(pts, r_out);
}

static void _slug_append_join(const Vector2 &p_v, const Vector2 &p_d0, const Vector2 &p_d1, float p_h,
		int p_join, float p_miter_limit, Vector<SlugContour> &r_out) {
	float cross = p_d0.x * p_d1.y - p_d0.y * p_d1.x;
	float dot = p_d0.dot(p_d1);
	if (dot > 0.999999f) {
		return; // Straight: the edge quads already meet.
	}

	Vector2 n0(p_d0.y, -p_d0.x);
	Vector2 n1(p_d1.y, -p_d1.x);

	if (dot < -0.999999f) {
		// 180 degree reversal: cover the whole disk, no outer side is defined.
		_slug_append_disk(p_v, p_h, r_out);
		return;
	}

	// The outer side of the corner is the side the turn opens towards.
	float s = cross > 0.0f ? -1.0f : 1.0f;
	Vector2 o0 = n0 * s;
	Vector2 o1 = n1 * s;
	Vector2 a = p_v + o0 * p_h;
	Vector2 b = p_v + o1 * p_h;

	if (p_join == SLUG_STROKE_JOIN_ROUND) {
		_slug_append_sector(p_v, Math::atan2(o0.y, o0.x), Math::atan2(o1.y, o1.x), p_h, r_out);
		return;
	}
	if (p_join == SLUG_STROKE_JOIN_MITER) {
		float denom = cross;
		if (Math::abs(denom) > 1e-6f) {
			float t = ((b.x - a.x) * p_d1.y - (b.y - a.y) * p_d1.x) / denom;
			Vector2 m = a + p_d0 * t;
			if ((m - p_v).length() <= p_miter_limit * p_h) {
				Vector<Vector2> quad;
				quad.push_back(p_v);
				quad.push_back(a);
				quad.push_back(m);
				quad.push_back(b);
				_slug_append_polygon(quad, r_out);
				return;
			}
		}
	}
	// Bevel (and miter-over-limit fallback).
	Vector<Vector2> tri;
	tri.push_back(p_v);
	tri.push_back(a);
	tri.push_back(b);
	_slug_append_polygon(tri, r_out);
}

void slug_build_stroke(const Vector<Vector2> &p_points, bool p_closed, float p_width,
		int p_join, int p_cap, float p_miter_limit, Vector<SlugContour> &r_out) {
	Vector<Vector2> pts;
	for (int i = 0; i < p_points.size(); i++) {
		if (pts.size() > 0 && pts[pts.size() - 1].distance_squared_to(p_points[i]) < 1e-12f) {
			continue;
		}
		pts.push_back(p_points[i]);
	}
	while (pts.size() >= 2 && pts[0].distance_squared_to(pts[pts.size() - 1]) < 1e-12f) {
		pts.remove(pts.size() - 1);
	}
	int n = pts.size();
	if (n < 2) {
		return;
	}
	float h = Math::abs(p_width) * 0.5f;
	if (h <= 0.0f) {
		return;
	}
	if (p_miter_limit <= 0.0f) {
		p_miter_limit = 4.0f;
	}

	Vector<Vector2> dirs;
	dirs.resize(n - 1);
	for (int i = 0; i < n - 1; i++) {
		Vector2 d = pts[i + 1] - pts[i];
		float len = d.length();
		dirs.write[i] = len > 1e-9f ? d / len : Vector2(1.0f, 0.0f);
	}

	// One quad per edge.
	for (int i = 0; i < n - 1; i++) {
		Vector2 nn(dirs[i].y, -dirs[i].x);
		Vector<Vector2> quad;
		quad.push_back(pts[i] + nn * h);
		quad.push_back(pts[i + 1] + nn * h);
		quad.push_back(pts[i + 1] - nn * h);
		quad.push_back(pts[i] - nn * h);
		_slug_append_polygon(quad, r_out);
	}

	// Joins.
	if (p_closed) {
		// A closed polyline never repeats its first point here: the closing edge
		// is (n-1 -> 0).
		Vector2 d_close = pts[0] - pts[n - 1];
		float len = d_close.length();
		Vector2 dlast = len > 1e-9f ? d_close / len : dirs[n - 2];
		// Closing edge quad.
		Vector2 nn(dlast.y, -dlast.x);
		Vector<Vector2> quad;
		quad.push_back(pts[n - 1] + nn * h);
		quad.push_back(pts[0] + nn * h);
		quad.push_back(pts[0] - nn * h);
		quad.push_back(pts[n - 1] - nn * h);
		_slug_append_polygon(quad, r_out);

		for (int i = 0; i < n; i++) {
			Vector2 d0 = (i == 0) ? dlast : dirs[i - 1];
			Vector2 d1 = (i == n - 1) ? dlast : dirs[i];
			_slug_append_join(pts[i], d0, d1, h, p_join, p_miter_limit, r_out);
		}
	} else {
		for (int i = 1; i < n - 1; i++) {
			_slug_append_join(pts[i], dirs[i - 1], dirs[i], h, p_join, p_miter_limit, r_out);
		}
		if (p_cap == SLUG_STROKE_CAP_ROUND) {
			_slug_append_round_cap(pts[0], -dirs[0], h, r_out);
			_slug_append_round_cap(pts[n - 1], dirs[n - 2], h, r_out);
		} else if (p_cap == SLUG_STROKE_CAP_SQUARE) {
			Vector2 n0(dirs[0].y, -dirs[0].x);
			Vector<Vector2> sq0;
			sq0.push_back(pts[0] + n0 * h);
			sq0.push_back(pts[0] + n0 * h - dirs[0] * h);
			sq0.push_back(pts[0] - n0 * h - dirs[0] * h);
			sq0.push_back(pts[0] - n0 * h);
			_slug_append_polygon(sq0, r_out);

			Vector2 dl = dirs[n - 2];
			Vector2 nl(dl.y, -dl.x);
			Vector<Vector2> sq1;
			sq1.push_back(pts[n - 1] + nl * h);
			sq1.push_back(pts[n - 1] + nl * h + dl * h);
			sq1.push_back(pts[n - 1] - nl * h + dl * h);
			sq1.push_back(pts[n - 1] - nl * h);
			_slug_append_polygon(sq1, r_out);
		}
	}
}
