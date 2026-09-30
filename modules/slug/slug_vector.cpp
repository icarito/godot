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

	atlas = SlugShapeBuilder::build(svg.shapes);
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

SlugVector::SlugVector() {
}

SlugVector::~SlugVector() {
}
