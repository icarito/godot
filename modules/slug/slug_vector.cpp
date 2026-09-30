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
#include "slug_svg.h"

void SlugVector::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_svg_path", "path"), &SlugVector::set_svg_path);
	ClassDB::bind_method(D_METHOD("get_svg_path"), &SlugVector::get_svg_path);

	ClassDB::bind_method(D_METHOD("is_valid"), &SlugVector::is_valid);
	ClassDB::bind_method(D_METHOD("get_shape_count"), &SlugVector::get_shape_count);
	ClassDB::bind_method(D_METHOD("get_build_time_usec"), &SlugVector::get_build_time_usec);
	ClassDB::bind_method(D_METHOD("get_max_curves_per_band"), &SlugVector::get_max_curves_per_band);
	ClassDB::bind_method(D_METHOD("get_bounds"), &SlugVector::get_bounds);

	ADD_PROPERTY(PropertyInfo(Variant::STRING, "svg_path", PROPERTY_HINT_FILE, "*.svg"), "set_svg_path", "get_svg_path");
}

void SlugVector::set_svg_path(const String &p_path) {
	if (svg_path == p_path) {
		return;
	}
	svg_path = p_path;
	_clear_built();
	emit_changed();
}

String SlugVector::get_svg_path() const {
	return svg_path;
}

void SlugVector::_clear_built() {
	built = false;
	valid = false;
	build_time_usec = 0;
	bounds = Rect2();
	shapes.clear();
	colors.clear();
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
	if (!slug_parse_svg(file_data, shapes, colors, bounds, error)) {
		ERR_PRINT("SlugVector: '" + svg_path + "': " + error + ".");
		shapes.clear();
		colors.clear();
		return;
	}

	atlas = SlugShapeBuilder::build(shapes);
	valid = true;
	build_time_usec = (int)(OS::get_singleton()->get_ticks_usec() - start_usec);

	print_verbose("SlugVector: " + itos(shapes.size()) + " shapes, " + itos(atlas.curve_rows) + " curve rows, " +
			itos(atlas.band_rows) + " band rows, max " + itos(atlas.max_curves_per_band) + " curves/band, " +
			itos(build_time_usec) + " usec (" + svg_path + ")");
}

bool SlugVector::is_valid() {
	_ensure_built();
	return valid;
}

int SlugVector::get_shape_count() {
	_ensure_built();
	return shapes.size();
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
	return bounds;
}

RID SlugVector::get_material_rid() {
	_ensure_built();
	return atlas.material.is_valid() ? atlas.material->get_rid() : RID();
}

const Vector<SlugShape> &SlugVector::get_shapes() {
	_ensure_built();
	return shapes;
}

Color SlugVector::get_shape_color(int p_index) const {
	if (p_index < 0 || p_index >= colors.size()) {
		return Color(1, 1, 1, 1);
	}
	return colors[p_index];
}

SlugVector::SlugVector() {
}

SlugVector::~SlugVector() {
}
