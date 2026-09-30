/*************************************************************************/
/*  slug_vector.cpp                                                     */
/*************************************************************************/
/* Godot 3.6 module: modules/slug                                       */
/*************************************************************************/

#include "slug_vector.h"

#include "core/class_db.h"
#include "core/os/file_access.h"
#include "core/os/os.h"
#include "core/print_string.h"
#include "scene/resources/shader.h"
#include "slug_shader.h"

// Width, in texels, of each pre-sampled gradient ramp in the gradient texture.
static const int SLUG_GRADIENT_WIDTH = 256;

void SlugVector::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_svg_path", "path"), &SlugVector::set_svg_path);
	ClassDB::bind_method(D_METHOD("get_svg_path"), &SlugVector::get_svg_path);

	ClassDB::bind_method(D_METHOD("set_fill_color", "color"), &SlugVector::set_fill_color);
	ClassDB::bind_method(D_METHOD("get_fill_color"), &SlugVector::get_fill_color);
	ClassDB::bind_method(D_METHOD("set_stroke_color", "color"), &SlugVector::set_stroke_color);
	ClassDB::bind_method(D_METHOD("get_stroke_color"), &SlugVector::get_stroke_color);

	ClassDB::bind_method(D_METHOD("is_valid"), &SlugVector::is_valid);
	ClassDB::bind_method(D_METHOD("get_shape_count"), &SlugVector::get_shape_count);
	ClassDB::bind_method(D_METHOD("get_build_time_usec"), &SlugVector::get_build_time_usec);
	ClassDB::bind_method(D_METHOD("get_max_curves_per_band"), &SlugVector::get_max_curves_per_band);
	ClassDB::bind_method(D_METHOD("get_bounds"), &SlugVector::get_bounds);

	ADD_PROPERTY(PropertyInfo(Variant::STRING, "svg_path", PROPERTY_HINT_FILE, "*.svg"), "set_svg_path", "get_svg_path");
	ADD_PROPERTY(PropertyInfo(Variant::COLOR, "fill_color"), "set_fill_color", "get_fill_color");
	ADD_PROPERTY(PropertyInfo(Variant::COLOR, "stroke_color"), "set_stroke_color", "get_stroke_color");
}

void SlugVector::set_svg_path(const String &p_path) {
	if (svg_path == p_path) {
		return;
	}
	svg_path = p_path;
	fill_custom = false;
	stroke_custom = false;
	_clear_built();
	emit_changed();
}

String SlugVector::get_svg_path() const {
	return svg_path;
}

void SlugVector::set_fill_color(const Color &p_color) {
	if (fill_color == p_color && fill_custom) {
		return;
	}
	fill_color = p_color;
	fill_custom = true;
	_update_gradient_texture();
	emit_changed(); // Only the colors change, not the geometry: no rebuild needed.
}

Color SlugVector::get_fill_color() const {
	return fill_color;
}

void SlugVector::set_stroke_color(const Color &p_color) {
	if (stroke_color == p_color && stroke_custom) {
		return;
	}
	stroke_color = p_color;
	stroke_custom = true;
	_update_gradient_texture();
	emit_changed();
}

Color SlugVector::get_stroke_color() const {
	return stroke_color;
}

void SlugVector::_clear_built() {
	built = false;
	valid = false;
	build_time_usec = 0;
	svg = SlugSvgData();
	atlas = SlugAtlasData();
	paint_tex.unref();
	gradient_tex.unref();
}

void SlugVector::_ensure_built() {
	if (built) {
		return;
	}
	built = true;

	if (svg_path.empty()) {
		return;
	}

	Vector<uint8_t> file_data = FileAccess::get_file_as_array(svg_path);
	if (file_data.empty()) {
		ERR_PRINT("SlugVector: could not read SVG file '" + svg_path + "'.");
		return;
	}

	uint64_t start_usec = OS::get_singleton()->get_ticks_usec();

	String error;
	if (!slug_parse_svg(file_data, svg, error)) {
		ERR_PRINT("SlugVector: '" + svg_path + "': " + error + ".");
		svg = SlugSvgData();
		return;
	}

	if (!fill_custom) {
		fill_color = svg.default_fill;
	}
	if (!stroke_custom) {
		stroke_color = svg.default_stroke;
	}

	atlas = SlugShapeBuilder::build(svg.shapes, slug_vector_shader_code());
	_build_paint_texture();
	_update_gradient_texture();
	valid = true;
	build_time_usec = (int)(OS::get_singleton()->get_ticks_usec() - start_usec);

	print_verbose("SlugVector: " + itos(svg.shapes.size()) + " shapes, " + itos(atlas.curve_rows) + " curve rows, " +
			itos(atlas.band_rows) + " band rows, max " + itos(atlas.max_curves_per_band) + " curves/band, " +
			itos(build_time_usec) + " usec (" + svg_path + ")");
}

bool SlugVector::is_valid() {
	_ensure_built();
	return valid;
}

int SlugVector::get_shape_count() {
	_ensure_built();
	return svg.shapes.size();
}

int SlugVector::get_build_time_usec() {
	_ensure_built();
	return build_time_usec;
}

int SlugVector::get_max_curves_per_band() {
	_ensure_built();
	return atlas.max_curves_per_band;
}

Rect2 SlugVector::get_bounds() {
	_ensure_built();
	return svg.bounds;
}

RID SlugVector::get_material_rid() {
	_ensure_built();
	return atlas.material.is_valid() ? atlas.material->get_rid() : RID();
}

const Vector<SlugShape> &SlugVector::get_shapes() {
	_ensure_built();
	return svg.shapes;
}

Color SlugVector::get_shape_color(int p_index) const {
	if (p_index < 0 || p_index >= svg.paints.size()) {
		return Color(1, 1, 1, 1);
	}
	const SlugPaint &paint = svg.paints[p_index];
	switch (paint.role) {
		case SLUG_PAINT_FILL:
			return fill_color;
		case SLUG_PAINT_STROKE:
			return stroke_color;
		default:
			return paint.color;
	}
}

Color SlugVector::_gradient_sample(int p_gradient, float p_t) const {
	const Vector<SlugGradientStop> &stops = svg.gradients[p_gradient].stops;
	if (stops.size() == 0) {
		return Color(1, 1, 1, 1);
	}
	Color colors[2];
	int idx[2] = { 0, stops.size() - 1 };
	float off[2] = { stops[0].offset, stops[stops.size() - 1].offset };
	for (int k = 0; k < 2; k++) {
		const SlugGradientStop &s = stops[idx[k]];
		switch (s.role) {
			case SLUG_PAINT_FILL:
				colors[k] = fill_color;
				break;
			case SLUG_PAINT_STROKE:
				colors[k] = stroke_color;
				break;
			default:
				colors[k] = s.color;
				break;
		}
	}
	if (stops.size() == 1 || p_t <= off[0]) {
		return colors[0];
	}
	if (p_t >= off[1]) {
		return colors[1];
	}
	for (int i = 0; i < stops.size() - 1; i++) {
		float o0 = stops[i].offset;
		float o1 = stops[i + 1].offset;
		if (p_t <= o1) {
			Color c0 = stops[i].role == SLUG_PAINT_FILL ? fill_color : (stops[i].role == SLUG_PAINT_STROKE ? stroke_color : stops[i].color);
			Color c1 = stops[i + 1].role == SLUG_PAINT_FILL ? fill_color : (stops[i + 1].role == SLUG_PAINT_STROKE ? stroke_color : stops[i + 1].color);
			float f = o1 > o0 ? (p_t - o0) / (o1 - o0) : 0.0f;
			return c0.linear_interpolate(c1, f);
		}
	}
	return colors[1];
}

void SlugVector::_build_paint_texture() {
	if (!atlas.material.is_valid()) {
		return;
	}
	int count = svg.shapes.size();
	Ref<Image> img;
	img.instance();
	img->create(4, MAX(1, count), false, Image::FORMAT_RGBAF);
	img->lock();
	for (int i = 0; i < count; i++) {
		const SlugPaint &p = svg.paints[i];
		float a[4] = { 0.0f, 0.0f, 0.0f, (float)p.fill_rule };
		float b[4] = { 1.0f, 0.0f, 0.0f, 0.0f };
		float c[4] = { 0.0f, 1.0f, 0.0f, 0.0f };
		if (p.gradient >= 0 && p.gradient < svg.gradients.size()) {
			const SlugGradientDef &g = svg.gradients[p.gradient];
			a[0] = (float)g.kind;
			a[1] = (float)p.gradient;
			a[2] = (float)g.spread;
			a[3] = (float)p.fill_rule;
			b[0] = g.m[0];
			b[1] = g.m[1];
			b[2] = g.t[0];
			b[3] = g.t[1];
			c[0] = g.m[2];
			c[1] = g.m[3];
		}
		img->set_pixel(0, i, Color(a[0], a[1], a[2], a[3]));
		img->set_pixel(1, i, Color(b[0], b[1], b[2], b[3]));
		img->set_pixel(2, i, Color(c[0], c[1], c[2], c[3]));
		img->set_pixel(3, i, Color(0, 0, 0, 0));
	}
	img->unlock();
	paint_tex.instance();
	paint_tex->create_from_image(img, 0);
	atlas.material->set_shader_param("paint_tex", paint_tex);
}

void SlugVector::_update_gradient_texture() {
	if (!atlas.material.is_valid()) {
		return;
	}
	int rows = MAX(1, svg.gradients.size());
	Ref<Image> img;
	img.instance();
	img->create(SLUG_GRADIENT_WIDTH, rows, false, Image::FORMAT_RGBAF);
	img->lock();
	for (int gy = 0; gy < rows; gy++) {
		for (int x = 0; x < SLUG_GRADIENT_WIDTH; x++) {
			float t = (float)x / (float)(SLUG_GRADIENT_WIDTH - 1);
			Color c = gy < svg.gradients.size() ? _gradient_sample(gy, t) : Color(1, 1, 1, 1);
			img->set_pixel(x, gy, c);
		}
	}
	img->unlock();
	gradient_tex.instance();
	gradient_tex->create_from_image(img, 0);
	atlas.material->set_shader_param("gradient_tex", gradient_tex);
	atlas.material->set_shader_param("gradient_rows_inv", 1.0f / (float)rows);
}

SlugVector::SlugVector() {
}

SlugVector::~SlugVector() {
}
