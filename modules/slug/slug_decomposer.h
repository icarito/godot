/*************************************************************************/
/*  slug_decomposer.h                                                   */
/*************************************************************************/
/* Godot 3.6 module: modules/slug                                       */
/* Builds a SlugShape from path commands, splitting cubic Beziers into  */
/* quadratics. Port of slughorn's CurveDecomposer (AlphaPixel/SlugHorn, */
/* MIT); see docs/slug-vector-spec.md.                                  */
/*************************************************************************/

#ifndef SLUG_DECOMPOSER_H
#define SLUG_DECOMPOSER_H

#include "slug_shape.h"

class SlugCurveDecomposer {
public:
	// Max chord deviation, in shape units, at which a cubic is emitted as two
	// quadratics without further subdivision. Smaller splits more.
	static const float FLATNESS;

	SlugCurveDecomposer();

	void reset();

	const SlugShape &get_shape() const { return shape; }
	SlugShape &get_shape() { return shape; }

	void move_to(const Vector2 &p_to);
	void line_to(const Vector2 &p_to);
	void quad_to(const Vector2 &p_control, const Vector2 &p_to);
	void cubic_to(const Vector2 &p_c1, const Vector2 &p_c2, const Vector2 &p_to);
	void close();

private:
	SlugShape shape;
	Vector2 cur;
	Vector2 start;
	bool started = false;

	Vector<SlugQCurve> &_contour();
	void _push(const Vector2 &p1, const Vector2 &p2, const Vector2 &p3);
	void _cubic_adaptive(const Vector2 &p0, const Vector2 &p1, const Vector2 &p2, const Vector2 &p3, int p_depth);
};

#endif // SLUG_DECOMPOSER_H
