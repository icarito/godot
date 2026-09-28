/*************************************************************************/
/*  slug_shader.h                                                        */
/*************************************************************************/
/* Port to Godot 3 shading language of the Slug reference shaders       */
/* (github.com/EricLengyel/Slug, SlugPixelShader.hlsl and               */
/* SlugVertexShader.hlsl). Slug shader code Copyright 2017 by Eric      */
/* Lengyel, MIT OR Apache-2.0; see SLUG_LICENSE.txt.                    */
/*************************************************************************/

#ifndef SLUG_SHADER_H
#define SLUG_SHADER_H

// Vertex attributes (surface built with compress_flags = 0):
//   VERTEX.xy  corner in the label's local z = 0 plane
//   NORMAL.xy  corner dilation direction, (+-1, +-1); NORMAL.z = 1 billboard
//   UV         em-space coordinates of the corner, in the glyph's frame
//   UV2        (glyph index, em per local unit)
//   COLOR      linear color
// glyph_tex, one row per glyph: texel 0 = band scale.xy, band offset.zw;
// texel 1 = glyph data location in band_tex .xy, band max .zw.
// curve_tex, band_tex: see slug_font.cpp. Both 4096 texels wide; a glyph's
// band headers and curve lists never cross a row.
static const char *SLUG_SHADER_CODE = R"(
shader_type spatial;
render_mode unshaded, blend_mix, cull_disabled, depth_draw_opaque;

uniform sampler2D curve_tex;
uniform sampler2D band_tex;
uniform sampler2D glyph_tex;

varying flat vec4 v_band;
varying flat vec4 v_glyph;

void vertex() {
	if (NORMAL.z > 0.5) {
		MODELVIEW_MATRIX = INV_CAMERA_MATRIX * mat4(CAMERA_MATRIX[0], CAMERA_MATRIX[1], CAMERA_MATRIX[2], WORLD_MATRIX[3]);
		MODELVIEW_MATRIX = MODELVIEW_MATRIX * mat4(vec4(length(WORLD_MATRIX[0].xyz), 0.0, 0.0, 0.0), vec4(0.0, length(WORLD_MATRIX[1].xyz), 0.0, 0.0), vec4(0.0, 0.0, length(WORLD_MATRIX[2].xyz), 0.0), vec4(0.0, 0.0, 0.0, 1.0));
	}

	int gi = int(UV2.x + 0.5);
	v_band = texelFetch(glyph_tex, ivec2(0, gi), 0);
	v_glyph = texelFetch(glyph_tex, ivec2(1, gi), 0);

	// SlugDilate: push the corner out along its normal so the quad covers
	// half a pixel more on screen, and move the em coordinate with it.
	mat4 mvp = PROJECTION_MATRIX * MODELVIEW_MATRIX;
	vec4 m0 = vec4(mvp[0][0], mvp[1][0], mvp[2][0], mvp[3][0]);
	vec4 m1 = vec4(mvp[0][1], mvp[1][1], mvp[2][1], mvp[3][1]);
	vec4 m3 = vec4(mvp[0][3], mvp[1][3], mvp[2][3], mvp[3][3]);
	vec2 p = VERTEX.xy;
	vec2 n = normalize(NORMAL.xy);
	float s = dot(m3.xy, p) + m3.w;
	float t = dot(m3.xy, n);
	float u = (s * dot(m0.xy, n) - t * (dot(m0.xy, p) + m0.w)) * VIEWPORT_SIZE.x;
	float v = (s * dot(m1.xy, n) - t * (dot(m1.xy, p) + m1.w)) * VIEWPORT_SIZE.y;
	float s2 = s * s;
	float st = s * t;
	float uv = u * u + v * v;
	float den = uv - st * st;
	// Edge-on or behind the camera the dilation diverges; skip it there.
	vec2 d = vec2(0.0);
	if (s > 0.0 && den > 1e-12) {
		d = NORMAL.xy * (s2 * (st + sqrt(uv)) / den);
	}
	VERTEX.xy += d;
	UV += d * UV2.y;
}

uint calc_root_code(float y1, float y2, float y3) {
	// Root eligibility from the signs of the three y coordinates; bits 0 and 8.
	uint i1 = floatBitsToUint(y1) >> 31u;
	uint i2 = floatBitsToUint(y2) >> 30u;
	uint i3 = floatBitsToUint(y3) >> 29u;
	uint shift = i1 | (i2 & 2u) | (i3 & 4u);
	// 0x2E74, 0x0101: the 3.6 tokenizer rejects a u suffix on hex literals.
	return (11892u >> shift) & 257u;
}

vec2 solve_horiz_poly(vec4 p12, vec2 p3) {
	vec2 a = p12.xy - p12.zw * 2.0 + p3;
	vec2 b = p12.xy - p12.zw;
	float ra = 1.0 / a.y;
	float rb = 0.5 / b.y;
	float d = sqrt(max(b.y * b.y - a.y * p12.y, 0.0));
	float t1 = (b.y - d) * ra;
	float t2 = (b.y + d) * ra;
	if (abs(a.y) < 1.0 / 65536.0) {
		t1 = p12.y * rb;
		t2 = t1;
	}
	return vec2((a.x * t1 - b.x * 2.0) * t1 + p12.x, (a.x * t2 - b.x * 2.0) * t2 + p12.x);
}

vec2 solve_vert_poly(vec4 p12, vec2 p3) {
	vec2 a = p12.xy - p12.zw * 2.0 + p3;
	vec2 b = p12.xy - p12.zw;
	float ra = 1.0 / a.x;
	float rb = 0.5 / b.x;
	float d = sqrt(max(b.x * b.x - a.x * p12.x, 0.0));
	float t1 = (b.x - d) * ra;
	float t2 = (b.x + d) * ra;
	if (abs(a.x) < 1.0 / 65536.0) {
		t1 = p12.x * rb;
		t2 = t1;
	}
	return vec2((a.y * t1 - b.y * 2.0) * t1 + p12.y, (a.y * t2 - b.y * 2.0) * t2 + p12.y);
}

void fragment() {
	vec2 rc = UV;
	vec2 pixels_per_em = 1.0 / fwidth(rc);
	ivec2 band_max = ivec2(v_glyph.zw);
	ivec2 band_index = clamp(ivec2(rc * v_band.xy + v_band.zw), ivec2(0), band_max);
	ivec2 gloc = ivec2(v_glyph.xy);

	float xcov = 0.0;
	float xwgt = 0.0;
	vec2 hband = texelFetch(band_tex, ivec2(gloc.x + band_index.y, gloc.y), 0).xy;
	int hstart = gloc.x + int(hband.y);
	int hcount = int(hband.x);
	for (int i = 0; i < hcount; i++) {
		ivec2 cl = ivec2(texelFetch(band_tex, ivec2(hstart + i, gloc.y), 0).xy);
		vec4 p12 = texelFetch(curve_tex, cl, 0) - vec4(rc, rc);
		vec2 p3 = texelFetch(curve_tex, ivec2(cl.x + 1, cl.y), 0).xy - rc;
		// Curves are sorted by descending max x: nothing further can reach this pixel.
		if (max(max(p12.x, p12.z), p3.x) * pixels_per_em.x < -0.5) {
			break;
		}
		uint code = calc_root_code(p12.y, p12.w, p3.y);
		if (code != 0u) {
			vec2 r = solve_horiz_poly(p12, p3) * pixels_per_em.x;
			if ((code & 1u) != 0u) {
				xcov += clamp(r.x + 0.5, 0.0, 1.0);
				xwgt = max(xwgt, clamp(1.0 - abs(r.x) * 2.0, 0.0, 1.0));
			}
			if (code > 1u) {
				xcov -= clamp(r.y + 0.5, 0.0, 1.0);
				xwgt = max(xwgt, clamp(1.0 - abs(r.y) * 2.0, 0.0, 1.0));
			}
		}
	}

	float ycov = 0.0;
	float ywgt = 0.0;
	vec2 vband = texelFetch(band_tex, ivec2(gloc.x + band_max.y + 1 + band_index.x, gloc.y), 0).xy;
	int vstart = gloc.x + int(vband.y);
	int vcount = int(vband.x);
	for (int i = 0; i < vcount; i++) {
		ivec2 cl = ivec2(texelFetch(band_tex, ivec2(vstart + i, gloc.y), 0).xy);
		vec4 p12 = texelFetch(curve_tex, cl, 0) - vec4(rc, rc);
		vec2 p3 = texelFetch(curve_tex, ivec2(cl.x + 1, cl.y), 0).xy - rc;
		if (max(max(p12.y, p12.w), p3.y) * pixels_per_em.y < -0.5) {
			break;
		}
		uint code = calc_root_code(p12.x, p12.z, p3.x);
		if (code != 0u) {
			vec2 r = solve_vert_poly(p12, p3) * pixels_per_em.y;
			if ((code & 1u) != 0u) {
				ycov -= clamp(r.x + 0.5, 0.0, 1.0);
				ywgt = max(ywgt, clamp(1.0 - abs(r.x) * 2.0, 0.0, 1.0));
			}
			if (code > 1u) {
				ycov += clamp(r.y + 0.5, 0.0, 1.0);
				ywgt = max(ywgt, clamp(1.0 - abs(r.y) * 2.0, 0.0, 1.0));
			}
		}
	}

	// Nonzero fill rule; abs() accepts either winding direction.
	float coverage = max(abs(xcov * xwgt + ycov * ywgt) / max(xwgt + ywgt, 1.0 / 65536.0), min(abs(xcov), abs(ycov)));
	coverage = clamp(coverage, 0.0, 1.0);

	ALBEDO = COLOR.rgb;
	ALPHA = COLOR.a * coverage;
}
)";

#endif // SLUG_SHADER_H
