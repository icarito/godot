/*************************************************************************/
/*  slug_decomposer.cpp                                                 */
/*************************************************************************/
/* Godot 3.6 module: modules/slug                                       */
/* Adaptive cubic -> quadratic decomposition via De Casteljau           */
/* subdivision. Derived from slughorn's CurveDecomposer                 */
/* (AlphaPixel/SlugHorn, MIT license).                                  */
/*************************************************************************/

#include "slug_decomposer.h"

// 1/4096 em: tighter than slughorn's "balanced" (1e-3) because the fork
// targets large text and grazing perspective, where cubic faceting shows.
// Recursion is depth-capped, so the worst case stays bounded.
const float SlugCurveDecomposer::FLATNESS = 1.0f / 4096.0f;

static const int SLUG_CUBIC_MAX_DEPTH = 8;

// Squared distance from p to the line through a->b (no sqrt in the hot path).
static float _slug_point_to_line_dist_sq(const Vector2 &p, const Vector2 &a, const Vector2 &b) {
	Vector2 d = b - a;
	float len_sq = d.length_squared();
	if (len_sq < 1e-12f) {
		return (p - a).length_squared();
	}
	float cross = d.x * (a.y - p.y) - d.y * (a.x - p.x);
	return (cross * cross) / len_sq;
}

// Both interior control points within FLATNESS of the p0->p3 chord.
static bool _slug_cubic_flat_enough(const Vector2 &p0, const Vector2 &p1, const Vector2 &p2, const Vector2 &p3) {
	float tol_sq = SlugCurveDecomposer::FLATNESS * SlugCurveDecomposer::FLATNESS;
	return _slug_point_to_line_dist_sq(p1, p0, p3) <= tol_sq &&
			_slug_point_to_line_dist_sq(p2, p0, p3) <= tol_sq;
}

SlugCurveDecomposer::SlugCurveDecomposer() :
		cur(0.0f, 0.0f),
		start(0.0f, 0.0f) {
}

void SlugCurveDecomposer::reset() {
	shape = SlugShape();
	cur = Vector2(0.0f, 0.0f);
	start = Vector2(0.0f, 0.0f);
	started = false;
}

Vector<SlugQCurve> &SlugCurveDecomposer::_contour() {
	if (shape.contours.size() == 0) {
		shape.contours.push_back(SlugContour());
	}
	return shape.contours.write[shape.contours.size() - 1].curves;
}

void SlugCurveDecomposer::_push(const Vector2 &p1, const Vector2 &p2, const Vector2 &p3) {
	SlugQCurve c;
	c.p1 = p1;
	c.p2 = p2;
	c.p3 = p3;
	_contour().push_back(c);
}

void SlugCurveDecomposer::move_to(const Vector2 &p_to) {
	// Every subpath is its own contour; the packer keeps a contour in one row.
	shape.contours.push_back(SlugContour());
	cur = p_to;
	start = p_to;
	started = true;
}

void SlugCurveDecomposer::line_to(const Vector2 &p_to) {
	if (cur != p_to) { // Skip a zero-length {p1, p2, p2}.
		_push(cur, p_to, p_to);
	}
	cur = p_to;
}

void SlugCurveDecomposer::quad_to(const Vector2 &p_control, const Vector2 &p_to) {
	if (!(cur == p_control && p_control == p_to)) { // Skip a fully degenerate curve.
		_push(cur, p_control, p_to);
	}
	cur = p_to;
}

void SlugCurveDecomposer::cubic_to(const Vector2 &p_c1, const Vector2 &p_c2, const Vector2 &p_to) {
	_cubic_adaptive(cur, p_c1, p_c2, p_to, 0);
	cur = p_to;
}

void SlugCurveDecomposer::close() {
	if (started && cur != start) {
		line_to(start);
	}
}

void SlugCurveDecomposer::_cubic_adaptive(const Vector2 &p0, const Vector2 &p1, const Vector2 &p2, const Vector2 &p3, int p_depth) {
	if (p_depth >= SLUG_CUBIC_MAX_DEPTH || _slug_cubic_flat_enough(p0, p1, p2, p3)) {
		// Emit the leaf cubic as two quadratics split at its midpoint (t=0.5).
		Vector2 mid = (p0 + p1 * 3.0f + p2 * 3.0f + p3) * 0.125f;
		_push(p0, (p0 + p1 * 3.0f) * 0.25f, mid);
		_push(mid, (p2 * 3.0f + p3) * 0.25f, p3);
		return;
	}

	// De Casteljau split at t=0.5.
	Vector2 m01 = (p0 + p1) * 0.5f;
	Vector2 m12 = (p1 + p2) * 0.5f;
	Vector2 m23 = (p2 + p3) * 0.5f;
	Vector2 m012 = (m01 + m12) * 0.5f;
	Vector2 m123 = (m12 + m23) * 0.5f;
	Vector2 m0123 = (m012 + m123) * 0.5f;

	_cubic_adaptive(p0, m01, m012, m0123, p_depth + 1);
	_cubic_adaptive(m0123, m123, m23, p3, p_depth + 1);
}
