/*************************************************************************/
/*  register_types.cpp                                                  */
/*************************************************************************/
/* Godot 3.6 module: modules/slug                                       */
/*************************************************************************/

#include "register_types.h"

#include "core/class_db.h"
#include "slug_font.h"
#include "slug_label_3d.h"

void register_slug_types() {
	ClassDB::register_class<SlugFont>();
	ClassDB::register_class<SlugLabel3D>();
}

void unregister_slug_types() {
}
