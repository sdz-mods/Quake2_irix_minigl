#include "../ref_gl/gl_local.h"
#include "minigl_irix.h"
#include <glideutl.h>

#include <float.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define MGL_MAX_TEXTURES      4096
#define MGL_MAX_STACK_DEPTH   32
#define MGL_MAX_PRIM_VERTS    4096
#define MGL_CLIP_EPSILON      1.0e-5f
#define MGL_SAFE_COORD_LIMIT  8192.0f
#define MGL_SAFE_OOW_LIMIT    1048576.0f
#define MGL_SAFE_STW_LIMIT    1048576.0f
#define MGL_TEX_BANK_SIZE     0x200000u
#define MGL_CLIPPED_MAX_VERTS 16
#define MGL_CLIPPED_POLY_MAX_VERTS 256
#define MGL_SAFE_TRIANGLE_AREA 2.0f
#define MGL_DUPLICATE_CLIP_EPSILON 1.0e-4f
#define MGL_SAFE_TMU_GRADIENT_LIMIT 16384.0f
#define MGL_SAFE_OOW_GRADIENT_LIMIT 256.0f
#define MGL_SAFE_WORLD_TMU_GRADIENT_LIMIT 4096.0f
#define MGL_SAFE_WORLD_REPEAT_SPAN 32.0f

typedef struct {
	GLfloat clip[4];
	GLfloat color[4];
	GLfloat tex[2];    /* TMU0 texture coordinates */
	GLfloat tex2[2];   /* TMU1 texture coordinates (dual-TMU) */
} mgl_vertex_t;

typedef struct {
	GLuint              name;
	qboolean            defined;
	GLint               width;
	GLint               height;
	GLenum              min_filter;
	GLenum              mag_filter;
	GLenum              wrap_s;
	GLenum              wrap_t;
	GrLOD_t             lod;
	GrAspectRatio_t     aspect;
	GrTextureFormat_t   format;
	void               *data;
	size_t              data_size;
	FxU32               resident_addr;
	unsigned            resident_generation;
	FxU32               resident_addr_tmu1;
	unsigned            resident_generation_tmu1;
	int                 dirty_t_min;   /* first dirty row (from glTexSubImage2D) */
	int                 dirty_t_max;   /* last dirty row  (from glTexSubImage2D) */
	qboolean            dirty;
	void               *clean_data;   /* clean copy of tex->data before any dlight writes */
} mgl_texture_t;

typedef struct {
	const GLfloat *pointer;
	GLint          size;
	GLenum         type;
	GLsizei        stride;
	qboolean       enabled;
} mgl_array_state_t;

typedef struct {
	qboolean             render_buffer_valid;
	int                  render_buffer;
	qboolean             clip_window_valid;
	FxU32                clip_minx;
	FxU32                clip_miny;
	FxU32                clip_maxx;
	FxU32                clip_maxy;
	qboolean             stw_hint_valid;
	FxU32                stw_hint;
	qboolean             cull_mode_valid;
	int                  cull_mode;
	qboolean             depth_mode_valid;
	int                  depth_mode;
	qboolean             depth_function_valid;
	int                  depth_function;
	qboolean             depth_mask_valid;
	int                  depth_mask;
	qboolean             alpha_test_valid;
	int                  alpha_test_function;
	int                  alpha_test_reference;
	qboolean             alpha_blend_valid;
	int                  alpha_blend_src_rgb;
	int                  alpha_blend_dst_rgb;
	int                  alpha_blend_src_alpha;
	int                  alpha_blend_dst_alpha;
	qboolean             color_combine_valid;
	int                  color_combine_function;
	int                  color_combine_factor;
	int                  color_combine_local;
	int                  color_combine_other;
	int                  color_combine_invert;
	qboolean             alpha_combine_valid;
	int                  alpha_combine_function;
	int                  alpha_combine_factor;
	int                  alpha_combine_local;
	int                  alpha_combine_other;
	int                  alpha_combine_invert;
	qboolean             color_mask_valid;
	int                  color_mask_rgb;
	int                  color_mask_alpha;
	qboolean             tex_filter_valid;
	int                  tex_min_filter;
	int                  tex_mag_filter;
	qboolean             tex_clamp_valid;
	int                  tex_wrap_s;
	int                  tex_wrap_t;
	qboolean             tex_mipmap_valid;
	int                  tex_mipmap_mode;
	int                  tex_mipmap_dither;
	qboolean             tex_source_valid;
	FxU32                tex_source_addr;
	int                  tex_source_small_lod;
	int                  tex_source_large_lod;
	int                  tex_source_aspect;
	int                  tex_source_format;
	qboolean             tex_combine_valid;
	int                  tex_combine;
	/* TMU1 texture state (dual-TMU only) */
	qboolean             tex1_filter_valid;
	int                  tex1_min_filter;
	int                  tex1_mag_filter;
	qboolean             tex1_clamp_valid;
	int                  tex1_wrap_s;
	int                  tex1_wrap_t;
	qboolean             tex1_mipmap_valid;
	int                  tex1_mipmap_mode;
	int                  tex1_mipmap_dither;
	qboolean             tex1_source_valid;
	FxU32                tex1_source_addr;
	int                  tex1_source_small_lod;
	int                  tex1_source_large_lod;
	int                  tex1_source_aspect;
	int                  tex1_source_format;
	qboolean             tex1_combine_valid;
} mgl_hw_state_t;

typedef struct {
	qboolean             active;
	qboolean             glide_initialized;
	int                  video_width;
	int                  video_height;
	GLenum               draw_buffer;
	GLint                viewport[4];
	GLint                scissor[4];
	qboolean             texture_2d_enabled;
	qboolean             alpha_test_enabled;
	qboolean             depth_test_enabled;
	qboolean             cull_enabled;
	qboolean             blend_enabled;
	qboolean             scissor_enabled;
	qboolean             shared_texture_palette_enabled;
	GLenum               blend_src;
	GLenum               blend_dst;
	GLenum               depth_func;
	GLboolean            depth_mask;
	GLclampd             depth_range_near;
	GLclampd             depth_range_far;
	GLenum               alpha_func;
	GLclampf             alpha_ref;
	GLenum               cull_face;
	GLenum               shade_model;
	GLenum               polygon_mode;
	GLenum               tex_env_mode;
	GLfloat              point_size;
	GLfloat              clear_color[4];
	GLfloat              current_color[4];
	GLfloat              current_texcoord[2];
	GLenum               matrix_mode;
	GLfloat              modelview[16];
	GLfloat              projection[16];
	GLfloat              modelview_stack[MGL_MAX_STACK_DEPTH][16];
	GLfloat              projection_stack[MGL_MAX_STACK_DEPTH][16];
	int                  modelview_depth;
	int                  projection_depth;
	qboolean             in_begin;
	GLenum               primitive_mode;
	int                  primitive_count;
	GLuint               bound_texture;
	mgl_vertex_t         primitive[MGL_MAX_PRIM_VERTS];
	mgl_array_state_t    vertex_array;
	mgl_array_state_t    color_array;
	FxU32                tex_min_addr;
	FxU32                tex_max_addr;
	FxU32                tex_next_addr;
	FxU32                tex_high_addr;
	FxU32                dynamic_lightmap_addr;
	size_t               dynamic_lightmap_size;
	GuTexPalette         shared_palette;
	qboolean             shared_palette_dirty;
	unsigned             texture_generation;
	GLenum               current_error;
	mgl_hw_state_t       hw;
	/* Cached per-flush projection constants (computed once in mgl_flush_primitive) */
	GLfloat              proj_view_top;
	GLfloat              proj_screen_bias;
	GLfloat              proj_tex_s_scale;
	GLfloat              proj_tex_t_scale;
	/* Dual-TMU state (TMU1) */
	int                  num_tmus;               /* 1 or 2, detected at SetMode */
	int                  active_texture_unit;    /* 0 or 1, set by glSelectTextureSGIS */
	GLuint               bound_texture2;         /* texture bound to TMU1 */
	qboolean             texture_2d_enabled2;    /* GL_TEXTURE_2D enabled on TMU1 */
	GLenum               tex_env_mode2;          /* tex env mode for TMU1 */
	GLfloat              current_texcoord2[2];   /* current texcoord for TMU1 */
	FxU32                tex1_min_addr;
	FxU32                tex1_max_addr;
	FxU32                tex1_next_addr;
	FxU32                tex1_high_addr;
	unsigned             tex1_generation;        /* TMU1 TRAM eviction counter */
	GLfloat              proj_tex1_s_scale;
	GLfloat              proj_tex1_t_scale;
} mgl_state_t;

static mgl_state_t mgl;
static mgl_texture_t mgl_textures[MGL_MAX_TEXTURES];
static qboolean mgl_reserve_dynamic_lightmap(size_t required);
static const char *mgl_debug_texture_label(const mgl_texture_t *tex);

static void mgl_invalidate_hw_state(void)
{
	memset(&mgl.hw, 0, sizeof(mgl.hw));
}

static void mgl_set_render_buffer(int buffer)
{
	if (!mgl.hw.render_buffer_valid || mgl.hw.render_buffer != buffer)
	{
		grRenderBuffer(buffer);
		mgl.hw.render_buffer = buffer;
		mgl.hw.render_buffer_valid = true;
	}
}

static void mgl_set_clip_window(FxU32 minx, FxU32 miny, FxU32 maxx, FxU32 maxy)
{
	if (!mgl.hw.clip_window_valid ||
		mgl.hw.clip_minx != minx ||
		mgl.hw.clip_miny != miny ||
		mgl.hw.clip_maxx != maxx ||
		mgl.hw.clip_maxy != maxy)
	{
		grClipWindow(minx, miny, maxx, maxy);
		mgl.hw.clip_minx = minx;
		mgl.hw.clip_miny = miny;
		mgl.hw.clip_maxx = maxx;
		mgl.hw.clip_maxy = maxy;
		mgl.hw.clip_window_valid = true;
	}
}

static void mgl_set_stw_hint(FxU32 hint)
{
	if (!mgl.hw.stw_hint_valid || mgl.hw.stw_hint != hint)
	{
		grHints(GR_HINT_STWHINT, hint);
		mgl.hw.stw_hint = hint;
		mgl.hw.stw_hint_valid = true;
	}
}

static void mgl_set_cull_mode(int mode)
{
	if (!mgl.hw.cull_mode_valid || mgl.hw.cull_mode != mode)
	{
		grCullMode(mode);
		mgl.hw.cull_mode = mode;
		mgl.hw.cull_mode_valid = true;
	}
}

static void mgl_set_depth_buffer_mode(int mode)
{
	if (!mgl.hw.depth_mode_valid || mgl.hw.depth_mode != mode)
	{
		grDepthBufferMode(mode);
		mgl.hw.depth_mode = mode;
		mgl.hw.depth_mode_valid = true;
	}
}

static void mgl_set_depth_buffer_function(int function)
{
	if (!mgl.hw.depth_function_valid || mgl.hw.depth_function != function)
	{
		grDepthBufferFunction(function);
		mgl.hw.depth_function = function;
		mgl.hw.depth_function_valid = true;
	}
}

static void mgl_set_depth_mask(int mask)
{
	if (!mgl.hw.depth_mask_valid || mgl.hw.depth_mask != mask)
	{
		grDepthMask(mask);
		mgl.hw.depth_mask = mask;
		mgl.hw.depth_mask_valid = true;
	}
}

static void mgl_set_alpha_test(int function, int reference)
{
	if (!mgl.hw.alpha_test_valid ||
		mgl.hw.alpha_test_function != function ||
		mgl.hw.alpha_test_reference != reference)
	{
		grAlphaTestFunction(function);
		grAlphaTestReferenceValue((GrAlpha_t)reference);
		mgl.hw.alpha_test_function = function;
		mgl.hw.alpha_test_reference = reference;
		mgl.hw.alpha_test_valid = true;
	}
}

static void mgl_set_alpha_blend(int src_rgb, int dst_rgb, int src_alpha, int dst_alpha)
{
	if (!mgl.hw.alpha_blend_valid ||
		mgl.hw.alpha_blend_src_rgb != src_rgb ||
		mgl.hw.alpha_blend_dst_rgb != dst_rgb ||
		mgl.hw.alpha_blend_src_alpha != src_alpha ||
		mgl.hw.alpha_blend_dst_alpha != dst_alpha)
	{
		grAlphaBlendFunction(src_rgb, dst_rgb, src_alpha, dst_alpha);
		mgl.hw.alpha_blend_src_rgb = src_rgb;
		mgl.hw.alpha_blend_dst_rgb = dst_rgb;
		mgl.hw.alpha_blend_src_alpha = src_alpha;
		mgl.hw.alpha_blend_dst_alpha = dst_alpha;
		mgl.hw.alpha_blend_valid = true;
	}
}

static void mgl_set_color_combine(int function, int factor, int local, int other, int invert)
{
	if (!mgl.hw.color_combine_valid ||
		mgl.hw.color_combine_function != function ||
		mgl.hw.color_combine_factor != factor ||
		mgl.hw.color_combine_local != local ||
		mgl.hw.color_combine_other != other ||
		mgl.hw.color_combine_invert != invert)
	{
		grColorCombine(function, factor, local, other, invert);
		mgl.hw.color_combine_function = function;
		mgl.hw.color_combine_factor = factor;
		mgl.hw.color_combine_local = local;
		mgl.hw.color_combine_other = other;
		mgl.hw.color_combine_invert = invert;
		mgl.hw.color_combine_valid = true;
	}
}

static void mgl_set_alpha_combine(int function, int factor, int local, int other, int invert)
{
	if (!mgl.hw.alpha_combine_valid ||
		mgl.hw.alpha_combine_function != function ||
		mgl.hw.alpha_combine_factor != factor ||
		mgl.hw.alpha_combine_local != local ||
		mgl.hw.alpha_combine_other != other ||
		mgl.hw.alpha_combine_invert != invert)
	{
		grAlphaCombine(function, factor, local, other, invert);
		mgl.hw.alpha_combine_function = function;
		mgl.hw.alpha_combine_factor = factor;
		mgl.hw.alpha_combine_local = local;
		mgl.hw.alpha_combine_other = other;
		mgl.hw.alpha_combine_invert = invert;
		mgl.hw.alpha_combine_valid = true;
	}
}

static void mgl_set_texture_filter(int min_filter, int mag_filter)
{
	if (!mgl.hw.tex_filter_valid ||
		mgl.hw.tex_min_filter != min_filter ||
		mgl.hw.tex_mag_filter != mag_filter)
	{
		grTexFilterMode(GR_TMU0, min_filter, mag_filter);
		mgl.hw.tex_min_filter = min_filter;
		mgl.hw.tex_mag_filter = mag_filter;
		mgl.hw.tex_filter_valid = true;
	}
}

static void mgl_set_texture_clamp(int wrap_s, int wrap_t)
{
	if (!mgl.hw.tex_clamp_valid ||
		mgl.hw.tex_wrap_s != wrap_s ||
		mgl.hw.tex_wrap_t != wrap_t)
	{
		grTexClampMode(GR_TMU0, wrap_s, wrap_t);
		mgl.hw.tex_wrap_s = wrap_s;
		mgl.hw.tex_wrap_t = wrap_t;
		mgl.hw.tex_clamp_valid = true;
	}
}

static void mgl_set_texture_mipmap(int mode, int dither)
{
	if (!mgl.hw.tex_mipmap_valid ||
		mgl.hw.tex_mipmap_mode != mode ||
		mgl.hw.tex_mipmap_dither != dither)
	{
		grTexMipMapMode(GR_TMU0, mode, dither);
		mgl.hw.tex_mipmap_mode = mode;
		mgl.hw.tex_mipmap_dither = dither;
		mgl.hw.tex_mipmap_valid = true;
	}
}

static void mgl_set_texture_source(FxU32 address, int lod, int aspect, int format)
{
	if (!mgl.hw.tex_source_valid ||
		mgl.hw.tex_source_addr != address ||
		mgl.hw.tex_source_small_lod != lod ||
		mgl.hw.tex_source_large_lod != lod ||
		mgl.hw.tex_source_aspect != aspect ||
		mgl.hw.tex_source_format != format)
	{
		GrTexInfo info;

		info.smallLod = lod;
		info.largeLod = lod;
		info.aspectRatio = aspect;
		info.format = format;
		info.data = NULL;

		grTexSource(GR_TMU0, address, GR_MIPMAPLEVELMASK_BOTH, &info);
		mgl.hw.tex_source_addr = address;
		mgl.hw.tex_source_small_lod = lod;
		mgl.hw.tex_source_large_lod = lod;
		mgl.hw.tex_source_aspect = aspect;
		mgl.hw.tex_source_format = format;
		mgl.hw.tex_source_valid = true;
	}
}

/* Sentinel for dual-TMU combine mode (not a GrTextureCombineFnc_t value) */
#define MGL_TMU0_COMBINE_MULTITEX (-1)

static void mgl_set_texture_combine(int combine)
{
	/* Force re-issue if previously set in multitex mode */
	if (mgl.hw.tex_combine == MGL_TMU0_COMBINE_MULTITEX)
		mgl.hw.tex_combine_valid = false;
	if (!mgl.hw.tex_combine_valid || mgl.hw.tex_combine != combine)
	{
		grTexCombineFunction(GR_TMU0, combine);
		mgl.hw.tex_combine = combine;
		mgl.hw.tex_combine_valid = true;
	}
}

/* TMU0 combine for dual-TMU: result = TMU0_tex * TMU1_output */
static void mgl_set_texture_combine_multitex(void)
{
	if (mgl.hw.tex_combine_valid && mgl.hw.tex_combine == MGL_TMU0_COMBINE_MULTITEX)
		return;
	grTexCombine(GR_TMU0,
		GR_COMBINE_FUNCTION_SCALE_OTHER, GR_COMBINE_FACTOR_LOCAL,
		GR_COMBINE_FUNCTION_SCALE_OTHER, GR_COMBINE_FACTOR_LOCAL,
		FXFALSE, FXFALSE);
	mgl.hw.tex_combine = MGL_TMU0_COMBINE_MULTITEX;
	mgl.hw.tex_combine_valid = true;
}

static void mgl_set_texture_filter_tmu1(int min_filter, int mag_filter)
{
	if (!mgl.hw.tex1_filter_valid ||
		mgl.hw.tex1_min_filter != min_filter ||
		mgl.hw.tex1_mag_filter != mag_filter)
	{
		grTexFilterMode(GR_TMU1, min_filter, mag_filter);
		mgl.hw.tex1_min_filter = min_filter;
		mgl.hw.tex1_mag_filter = mag_filter;
		mgl.hw.tex1_filter_valid = true;
	}
}

static void mgl_set_texture_clamp_tmu1(int wrap_s, int wrap_t)
{
	if (!mgl.hw.tex1_clamp_valid ||
		mgl.hw.tex1_wrap_s != wrap_s ||
		mgl.hw.tex1_wrap_t != wrap_t)
	{
		grTexClampMode(GR_TMU1, wrap_s, wrap_t);
		mgl.hw.tex1_wrap_s = wrap_s;
		mgl.hw.tex1_wrap_t = wrap_t;
		mgl.hw.tex1_clamp_valid = true;
	}
}

static void mgl_set_texture_mipmap_tmu1(int mode, int dither)
{
	if (!mgl.hw.tex1_mipmap_valid ||
		mgl.hw.tex1_mipmap_mode != mode ||
		mgl.hw.tex1_mipmap_dither != dither)
	{
		grTexMipMapMode(GR_TMU1, mode, dither);
		mgl.hw.tex1_mipmap_mode = mode;
		mgl.hw.tex1_mipmap_dither = dither;
		mgl.hw.tex1_mipmap_valid = true;
	}
}

static void mgl_set_texture_source_tmu1(FxU32 address, int lod, int aspect, int format)
{
	if (!mgl.hw.tex1_source_valid ||
		mgl.hw.tex1_source_addr != address ||
		mgl.hw.tex1_source_small_lod != lod ||
		mgl.hw.tex1_source_large_lod != lod ||
		mgl.hw.tex1_source_aspect != aspect ||
		mgl.hw.tex1_source_format != format)
	{
		GrTexInfo info;

		info.smallLod = lod;
		info.largeLod = lod;
		info.aspectRatio = aspect;
		info.format = format;
		info.data = NULL;

		grTexSource(GR_TMU1, address, GR_MIPMAPLEVELMASK_BOTH, &info);
		mgl.hw.tex1_source_addr = address;
		mgl.hw.tex1_source_small_lod = lod;
		mgl.hw.tex1_source_large_lod = lod;
		mgl.hw.tex1_source_aspect = aspect;
		mgl.hw.tex1_source_format = format;
		mgl.hw.tex1_source_valid = true;
	}
}

static void mgl_set_texture_combine_tmu1(void)
{
	if (mgl.hw.tex1_combine_valid)
		return;
	grTexCombineFunction(GR_TMU1, GR_TEXTURECOMBINE_DECAL);
	mgl.hw.tex1_combine_valid = true;
}

static GLfloat *mgl_current_matrix(void)
{
	return (mgl.matrix_mode == GL_PROJECTION) ? mgl.projection : mgl.modelview;
}

static GLfloat (*mgl_current_stack(void))[16]
{
	return (mgl.matrix_mode == GL_PROJECTION) ? mgl.projection_stack : mgl.modelview_stack;
}

static int *mgl_current_stack_depth(void)
{
	return (mgl.matrix_mode == GL_PROJECTION) ? &mgl.projection_depth : &mgl.modelview_depth;
}

static GLfloat mgl_clampf(GLfloat value, GLfloat min_value, GLfloat max_value)
{
	if (value < min_value)
		return min_value;
	if (value > max_value)
		return max_value;
	return value;
}

static GLint mgl_clampi(GLint value, GLint min_value, GLint max_value)
{
	if (value < min_value)
		return min_value;
	if (value > max_value)
		return max_value;
	return value;
}

static void mgl_set_error(GLenum error)
{
	if (mgl.current_error == GL_NO_ERROR)
		mgl.current_error = error;
}

static qboolean mgl_is_finite_float(GLfloat value)
{
	return (value == value) && (fabsf(value) <= FLT_MAX);
}

static void mgl_mat_identity(GLfloat *m)
{
	memset(m, 0, sizeof(GLfloat) * 16);
	m[0] = 1.0f;
	m[5] = 1.0f;
	m[10] = 1.0f;
	m[15] = 1.0f;
}

static void mgl_mat_copy(GLfloat *dst, const GLfloat *src)
{
	memcpy(dst, src, sizeof(GLfloat) * 16);
}

static qboolean mgl_matrix_is_identity(const GLfloat *m)
{
	static const GLfloat identity[16] = {
		1.0f, 0.0f, 0.0f, 0.0f,
		0.0f, 1.0f, 0.0f, 0.0f,
		0.0f, 0.0f, 1.0f, 0.0f,
		0.0f, 0.0f, 0.0f, 1.0f
	};
	int i;

	for (i = 0; i < 16; ++i)
	{
		if ((GLfloat)fabs(m[i] - identity[i]) > 1.0e-6f)
			return false;
	}

	return true;
}

static qboolean mgl_is_ortho_2d_path(void)
{
	return mgl_matrix_is_identity(mgl.modelview) &&
		(fabsf(mgl.projection[3]) < 1.0e-6f) &&
		(fabsf(mgl.projection[7]) < 1.0e-6f) &&
		(fabsf(mgl.projection[11]) < 1.0e-6f) &&
		(fabsf(mgl.projection[15] - 1.0f) < 1.0e-6f);
}

static void mgl_mat_mul(GLfloat *dst, const GLfloat *a, const GLfloat *b)
{
	GLfloat out[16];
	int row, col, k;

	for (col = 0; col < 4; ++col)
	{
		for (row = 0; row < 4; ++row)
		{
			GLfloat sum = 0.0f;
			for (k = 0; k < 4; ++k)
				sum += a[k * 4 + row] * b[col * 4 + k];
			out[col * 4 + row] = sum;
		}
	}

	memcpy(dst, out, sizeof(out));
}

static void mgl_post_multiply(GLfloat *current, const GLfloat *rhs)
{
	mgl_mat_mul(current, current, rhs);
}

static void mgl_mat_translate(GLfloat *m, GLfloat x, GLfloat y, GLfloat z)
{
	GLfloat t[16];

	mgl_mat_identity(t);
	t[12] = x;
	t[13] = y;
	t[14] = z;
	mgl_post_multiply(m, t);
}

static void mgl_mat_scale(GLfloat *m, GLfloat x, GLfloat y, GLfloat z)
{
	GLfloat s[16];

	mgl_mat_identity(s);
	s[0] = x;
	s[5] = y;
	s[10] = z;
	mgl_post_multiply(m, s);
}

static void mgl_mat_rotate(GLfloat *m, GLfloat angle_deg, GLfloat x, GLfloat y, GLfloat z)
{
	GLfloat r[16];
	GLfloat len;
	GLfloat c;
	GLfloat s;
	GLfloat t;

	len = (GLfloat)sqrt((x * x) + (y * y) + (z * z));
	if (len < MGL_CLIP_EPSILON)
		return;

	x /= len;
	y /= len;
	z /= len;

	c = (GLfloat)cos(angle_deg * M_PI / 180.0);
	s = (GLfloat)sin(angle_deg * M_PI / 180.0);
	t = 1.0f - c;

	mgl_mat_identity(r);
	r[0] = (t * x * x) + c;
	r[1] = (t * x * y) + (s * z);
	r[2] = (t * x * z) - (s * y);
	r[4] = (t * x * y) - (s * z);
	r[5] = (t * y * y) + c;
	r[6] = (t * y * z) + (s * x);
	r[8] = (t * x * z) + (s * y);
	r[9] = (t * y * z) - (s * x);
	r[10] = (t * z * z) + c;

	mgl_post_multiply(m, r);
}

static void mgl_mat_frustum(GLfloat *m,
	GLdouble left, GLdouble right,
	GLdouble bottom, GLdouble top,
	GLdouble z_near, GLdouble z_far)
{
	GLfloat f[16];

	mgl_mat_identity(f);
	f[0] = (GLfloat)((2.0 * z_near) / (right - left));
	f[5] = (GLfloat)((2.0 * z_near) / (top - bottom));
	f[8] = (GLfloat)((right + left) / (right - left));
	f[9] = (GLfloat)((top + bottom) / (top - bottom));
	f[10] = (GLfloat)(-(z_far + z_near) / (z_far - z_near));
	f[11] = -1.0f;
	f[14] = (GLfloat)(-(2.0 * z_far * z_near) / (z_far - z_near));
	f[15] = 0.0f;

	mgl_post_multiply(m, f);
}

static void mgl_mat_ortho(GLfloat *m,
	GLdouble left, GLdouble right,
	GLdouble bottom, GLdouble top,
	GLdouble z_near, GLdouble z_far)
{
	GLfloat o[16];

	mgl_mat_identity(o);
	o[0] = (GLfloat)(2.0 / (right - left));
	o[5] = (GLfloat)(2.0 / (top - bottom));
	o[10] = (GLfloat)(-2.0 / (z_far - z_near));
	o[12] = (GLfloat)(-(right + left) / (right - left));
	o[13] = (GLfloat)(-(top + bottom) / (top - bottom));
	o[14] = (GLfloat)(-(z_far + z_near) / (z_far - z_near));

	mgl_post_multiply(m, o);
}

static void mgl_vec4_mul(GLfloat *out, const GLfloat *m, const GLfloat *v)
{
	out[0] = (m[0] * v[0]) + (m[4] * v[1]) + (m[8] * v[2]) + (m[12] * v[3]);
	out[1] = (m[1] * v[0]) + (m[5] * v[1]) + (m[9] * v[2]) + (m[13] * v[3]);
	out[2] = (m[2] * v[0]) + (m[6] * v[1]) + (m[10] * v[2]) + (m[14] * v[3]);
	out[3] = (m[3] * v[0]) + (m[7] * v[1]) + (m[11] * v[2]) + (m[15] * v[3]);
}

static qboolean mgl_is_power_of_two(int value)
{
	return (value > 0) && ((value & (value - 1)) == 0);
}

static qboolean mgl_lod_from_size(int size, GrLOD_t *lod)
{
	switch (size)
	{
	case 1:   *lod = GR_LOD_1; return true;
	case 2:   *lod = GR_LOD_2; return true;
	case 4:   *lod = GR_LOD_4; return true;
	case 8:   *lod = GR_LOD_8; return true;
	case 16:  *lod = GR_LOD_16; return true;
	case 32:  *lod = GR_LOD_32; return true;
	case 64:  *lod = GR_LOD_64; return true;
	case 128: *lod = GR_LOD_128; return true;
	case 256: *lod = GR_LOD_256; return true;
	default: break;
	}

	return false;
}

static qboolean mgl_aspect_from_size(int width, int height, GrAspectRatio_t *aspect)
{
	if (width == height)
	{
		*aspect = GR_ASPECT_1x1;
		return true;
	}

	if (width > height)
	{
		switch (width / height)
		{
		case 2: *aspect = GR_ASPECT_2x1; return true;
		case 4: *aspect = GR_ASPECT_4x1; return true;
		case 8: *aspect = GR_ASPECT_8x1; return true;
		default: break;
		}
	}
	else
	{
		switch (height / width)
		{
		case 2: *aspect = GR_ASPECT_1x2; return true;
		case 4: *aspect = GR_ASPECT_1x4; return true;
		case 8: *aspect = GR_ASPECT_1x8; return true;
		default: break;
		}
	}

	return false;
}

static void mgl_texture_coord_scales(GrAspectRatio_t aspect, GLfloat *s_scale, GLfloat *t_scale)
{
	GLfloat s = 256.0f;
	GLfloat t = 256.0f;

	switch (aspect)
	{
	case GR_ASPECT_8x1:
		t = 256.0f / 8.0f;
		break;
	case GR_ASPECT_4x1:
		t = 256.0f / 4.0f;
		break;
	case GR_ASPECT_2x1:
		t = 256.0f / 2.0f;
		break;
	case GR_ASPECT_1x2:
		s = 256.0f / 2.0f;
		break;
	case GR_ASPECT_1x4:
		s = 256.0f / 4.0f;
		break;
	case GR_ASPECT_1x8:
		s = 256.0f / 8.0f;
		break;
	case GR_ASPECT_1x1:
	default:
		break;
	}

	if (s_scale)
		*s_scale = s;
	if (t_scale)
		*t_scale = t;
}

static mgl_texture_t *mgl_get_texture(GLuint name)
{
	if (name >= MGL_MAX_TEXTURES)
	{
		mgl_set_error(GL_INVALID_VALUE);
		return NULL;
	}

	mgl_textures[name].name = name;
	return &mgl_textures[name];
}

static void mgl_reset_texture_cache(void)
{
	unsigned i;

	mgl.texture_generation++;
	if (mgl.texture_generation == 0)
	{
		mgl.texture_generation = 1;
		for (i = 0; i < MGL_MAX_TEXTURES; ++i)
			mgl_textures[i].resident_generation = 0;
	}

	mgl.tex_next_addr = mgl.tex_min_addr;
	mgl.tex_high_addr = mgl.tex_max_addr;
	if (mgl.dynamic_lightmap_size > 0)
		mgl_reserve_dynamic_lightmap(mgl.dynamic_lightmap_size);
}

static void mgl_reset_texture_cache_tmu1(void)
{
	unsigned i;

	mgl.tex1_generation++;
	if (mgl.tex1_generation == 0)
	{
		mgl.tex1_generation = 1;
		for (i = 0; i < MGL_MAX_TEXTURES; ++i)
			mgl_textures[i].resident_generation_tmu1 = 0;
	}

	mgl.tex1_next_addr = mgl.tex1_min_addr;
	mgl.tex1_high_addr = mgl.tex1_max_addr;
}

static size_t mgl_texture_mem_required(const mgl_texture_t *tex)
{
	return (size_t)grTexCalcMemRequired(tex->lod, tex->lod, tex->aspect, tex->format);
}

static qboolean mgl_texture_uses_upper_bank(void)
{
	return (mgl.tex_max_addr > MGL_TEX_BANK_SIZE);
}

static qboolean mgl_texture_is_lightmap(const mgl_texture_t *tex)
{
	return tex &&
		(tex->name >= TEXNUM_LIGHTMAPS) &&
		(tex->name < TEXNUM_SCRAPS);
}

static qboolean mgl_texture_is_dynamic_lightmap(const mgl_texture_t *tex)
{
	return tex && (tex->name == TEXNUM_LIGHTMAPS);
}

static const char *mgl_debug_texture_label(const mgl_texture_t *tex)
{
	int i;

	if (!tex)
		return "unknown";

	if (mgl_texture_is_dynamic_lightmap(tex))
		return "dynamic-lightmap";

	if (mgl_texture_is_lightmap(tex))
		return "lightmap";

	for (i = 0; i < numgltextures; ++i)
	{
		if (gltextures[i].registration_sequence == 0)
			continue;
		if (gltextures[i].texnum == (int)tex->name)
			return gltextures[i].name;
	}

	return "unregistered";
}

static qboolean mgl_texture_is_world_base(const mgl_texture_t *tex)
{
	return tex &&
		currentmodel &&
		(currentmodel->type == mod_brush) &&
		!mgl_texture_is_lightmap(tex);
}

static qboolean mgl_texture_crosses_bank(FxU32 start, size_t required)
{
	return (start < MGL_TEX_BANK_SIZE) &&
		((start + required) > MGL_TEX_BANK_SIZE);
}

static size_t mgl_texture_bytes_per_texel(GrTextureFormat_t format)
{
	switch (format)
	{
	case GR_TEXFMT_P_8:
	case GR_TEXFMT_INTENSITY_8:
		return sizeof(FxU8);
	default:
		return sizeof(FxU16);
	}
}

static int mgl_texture_storage_x(int logical_x, int row_width, GrTextureFormat_t format)
{
	/*
	 * On IRIX, Glide's texture downloader already compensates for the O2 MACE
	 * bridge at the 32-bit write level. Reordering texels here duplicates that
	 * compensation and shows up as swapped columns in fonts and menu textures.
	 */
	(void)row_width;
	(void)format;
	return logical_x;
}

static FxU16 mgl_texture_pack_word(FxU16 value)
{
	return value;
}

static qboolean mgl_allocate_low_texture_address(size_t required, FxU32 *address)
{
	FxU32 candidate;

	if (!address || required == 0)
		return false;

	candidate = mgl.tex_next_addr;
	if (mgl_texture_uses_upper_bank() &&
		mgl_texture_crosses_bank(candidate, required))
		candidate = MGL_TEX_BANK_SIZE;

	if ((candidate + required) > mgl.tex_high_addr)
		return false;

	*address = candidate;
	mgl.tex_next_addr = candidate + (FxU32)required;
	return true;
}

static qboolean mgl_allocate_high_texture_address(size_t required, FxU32 *address)
{
	FxU32 high;
	FxU32 candidate;

	if (!address || required == 0)
		return false;

	high = mgl.tex_high_addr;
	if (high <= mgl.tex_next_addr)
		return false;

	if (required > (size_t)(high - mgl.tex_next_addr))
		return false;

	candidate = high - (FxU32)required;

	if (mgl_texture_uses_upper_bank() &&
		(high > MGL_TEX_BANK_SIZE) &&
		(candidate < MGL_TEX_BANK_SIZE))
	{
		if (mgl.tex_next_addr >= MGL_TEX_BANK_SIZE)
			return false;
		if (required > (size_t)(MGL_TEX_BANK_SIZE - mgl.tex_next_addr))
			return false;

		candidate = MGL_TEX_BANK_SIZE - (FxU32)required;
	}

	if (candidate < mgl.tex_next_addr)
		return false;

	*address = candidate;
	mgl.tex_high_addr = candidate;
	return true;
}

static qboolean mgl_allocate_texture_address(const mgl_texture_t *tex, size_t required, FxU32 *address)
{
	if (mgl_texture_is_lightmap(tex))
		return mgl_allocate_high_texture_address(required, address);

	return mgl_allocate_low_texture_address(required, address);
}

static qboolean mgl_reserve_dynamic_lightmap(size_t required)
{
	FxU32 candidate;

	if (required == 0)
		return false;
	if ((mgl.tex_max_addr <= mgl.tex_min_addr) ||
		(required > (size_t)(mgl.tex_max_addr - mgl.tex_min_addr)))
		return false;

	candidate = mgl.tex_max_addr - (FxU32)required;

	if (mgl_texture_uses_upper_bank() &&
		(mgl.tex_max_addr > MGL_TEX_BANK_SIZE) &&
		(candidate < MGL_TEX_BANK_SIZE))
	{
		candidate = MGL_TEX_BANK_SIZE - (FxU32)required;
	}

	if (candidate < mgl.tex_min_addr)
		return false;

	mgl.dynamic_lightmap_addr = candidate;
	mgl.dynamic_lightmap_size = required;
	if (mgl.tex_high_addr > candidate)
		mgl.tex_high_addr = candidate;

	return true;
}

static qboolean mgl_allocate_tmu1_address(size_t required, FxU32 *address)
{
	FxU32 candidate;
	unsigned i;

	if (!address || required == 0)
		return false;

	candidate = mgl.tex1_next_addr;

	/* Wrap around to start of TRAM when we reach the end. */
	if ((candidate + (FxU32)required) > mgl.tex1_high_addr)
		candidate = mgl.tex1_min_addr;

	/* If a single texture is larger than all of TRAM1, give up. */
	if ((candidate + (FxU32)required) > mgl.tex1_high_addr)
		return false;

	/* Evict any textures currently occupying [candidate, candidate+required).
	 * This is a circular-buffer eviction: oldest slot gives way to newest. */
	for (i = 0; i < MGL_MAX_TEXTURES; ++i)
	{
		if (mgl_textures[i].resident_generation_tmu1 != mgl.tex1_generation)
			continue;
		{
			FxU32 tex_start = mgl_textures[i].resident_addr_tmu1;
			FxU32 tex_end   = tex_start + (FxU32)mgl_texture_mem_required(&mgl_textures[i]);
			if (tex_start < (candidate + (FxU32)required) && tex_end > candidate)
				mgl_textures[i].resident_generation_tmu1 = 0;  /* mark stale */
		}
	}

	*address = candidate;
	mgl.tex1_next_addr = candidate + (FxU32)required;
	return true;
}

static qboolean mgl_ensure_texture_resident_tmu1(mgl_texture_t *tex)
{
	GrTexInfo info;
	size_t required;
	FxU32 tex_addr;

	if (!tex || !tex->defined || !tex->data)
		return false;

	required = mgl_texture_mem_required(tex);
	if (required == 0)
		return false;

	if (tex->resident_generation_tmu1 == mgl.tex1_generation)
	{
		if (!tex->dirty)
			return true;  /* fully cached */

		/* Dirty but still allocated at a valid address.
		 * GL_RenderLightmappedPoly calls glTexSubImage2D once per dynamic
		 * surface (e.g. animated lightstyles, dlights), then renders it
		 * immediately.  Each call updates only a small region (smax×tmax,
		 * typically 8–32 rows) of the 128×128 atlas.  Use a partial
		 * download — only the rows that actually changed — instead of
		 * re-DMA'ing the entire 32 KB atlas every time. */
		tex_addr = tex->resident_addr_tmu1;
		{
			/* grTexDownloadMipMapLevelPartial expects 'data' to point at the
			 * FIRST row being downloaded (row dirty_t_min), not at row 0. */
			size_t bpr = (size_t)tex->width * mgl_texture_bytes_per_texel(tex->format);
			const void *partial_data = (const GLubyte *)tex->data + (size_t)tex->dirty_t_min * bpr;
			grTexDownloadMipMapLevelPartial(GR_TMU1,
				tex_addr,
				tex->lod,
				tex->lod,
				tex->aspect,
				tex->format,
				GR_MIPMAPLEVELMASK_BOTH,
				partial_data,
				tex->dirty_t_min,
				tex->dirty_t_max);
		}

		/* Restore the dlight-written rows back to clean static data so
		 * that a future full re-upload (after eviction) uses clean data. */
		if (tex->clean_data)
		{
			size_t bpr = (size_t)tex->width * mgl_texture_bytes_per_texel(tex->format);
			memcpy((GLubyte *)tex->data + (size_t)tex->dirty_t_min * bpr,
			       (GLubyte *)tex->clean_data + (size_t)tex->dirty_t_min * bpr,
			       (size_t)(tex->dirty_t_max - tex->dirty_t_min + 1) * bpr);
		}

		tex->dirty = false;
		tex->dirty_t_min = 0;
		tex->dirty_t_max = -1;
		/* No grTexSource needed: address unchanged, hw still points to it. */
		return true;
	}

	/* Evicted (generation stale): allocate a fresh address and upload.
	 * Never auto-reset the TRAM cache here — that would evict ALL resident
	 * lightmaps and cause a cascade (re-upload → fill → reset → re-upload…).
	 * If TRAM is full, render this surface without the lightmap instead. */
	if (!mgl_allocate_tmu1_address(required, &tex_addr))
		return false;

	/* Restore tex->data to clean static lightmap before uploading, so
	 * TRAM sees only the original baked lightmap, not stale dlight data. */
	if (tex->clean_data)
		memcpy(tex->data, tex->clean_data, tex->data_size);

	grTexDownloadMipMapLevel(GR_TMU1,
		tex_addr,
		tex->lod,
		tex->lod,
		tex->aspect,
		tex->format,
		GR_MIPMAPLEVELMASK_BOTH,
		tex->data);

	info.smallLod = tex->lod;
	info.largeLod = tex->lod;
	info.aspectRatio = tex->aspect;
	info.format = tex->format;
	info.data = NULL;

	grTexSource(GR_TMU1, tex_addr, GR_MIPMAPLEVELMASK_BOTH, &info);

	tex->resident_addr_tmu1 = tex_addr;
	tex->resident_generation_tmu1 = mgl.tex1_generation;
	tex->dirty = false;

	return true;
}

static mgl_texture_t *mgl_prepare_texture_tmu1(void)
{
	mgl_texture_t *tex;

	if (!mgl.texture_2d_enabled2 || mgl.num_tmus < 2)
		return NULL;

	tex = mgl_get_texture(mgl.bound_texture2);
	if (!tex || !tex->defined || !mgl_ensure_texture_resident_tmu1(tex))
		return NULL;

	mgl_set_texture_filter_tmu1(
		mgl_filter_mode(tex->min_filter),
		mgl_filter_mode(tex->mag_filter));
	mgl_set_texture_clamp_tmu1(
		mgl_clamp_mode(tex->wrap_s),
		mgl_clamp_mode(tex->wrap_t));
	mgl_set_texture_mipmap_tmu1(GR_MIPMAP_DISABLE, FXFALSE);
	mgl_set_texture_source_tmu1(tex->resident_addr_tmu1, tex->lod, tex->aspect, tex->format);
	mgl_set_texture_combine_tmu1();

	return tex;
}

static GrTextureFormat_t mgl_choose_texture_format(GLint internalformat)
{
	switch (internalformat)
	{
	case GL_COLOR_INDEX:
#if defined(GL_COLOR_INDEX8_EXT) && (GL_COLOR_INDEX8_EXT != GL_COLOR_INDEX)
	case GL_COLOR_INDEX8_EXT:
#endif
		return GR_TEXFMT_P_8;
	case 3:
	case GL_RGB:
		return GR_TEXFMT_RGB_565;
	case GL_INTENSITY8:
	case GL_LUMINANCE8:
		return GR_TEXFMT_INTENSITY_8;
	case GL_RGBA4:
		return GR_TEXFMT_ARGB_4444;
	case GL_RGB5_A1:
	case 4:
	case GL_RGBA:
	case GL_RGBA8:
	default:
		return GR_TEXFMT_ARGB_1555;
	}
}

static FxU16 mgl_convert_rgba_to_argb4444(const GLubyte *rgba)
{
	FxU16 packed = (FxU16)((((FxU16)rgba[3] >> 4) << 12) |
		(((FxU16)rgba[0] >> 4) << 8) |
		(((FxU16)rgba[1] >> 4) << 4) |
		((FxU16)rgba[2] >> 4));

	return mgl_texture_pack_word(packed);
}

static FxU16 mgl_convert_rgba_to_argb1555(const GLubyte *rgba)
{
	FxU16 packed = (FxU16)((((FxU16)rgba[3] >> 7) << 15) |
		(((FxU16)rgba[0] >> 3) << 10) |
		(((FxU16)rgba[1] >> 3) << 5) |
		((FxU16)rgba[2] >> 3));

	return mgl_texture_pack_word(packed);
}

static FxU16 mgl_convert_rgba_to_rgb565(const GLubyte *rgba)
{
	FxU16 packed = (FxU16)((((FxU16)rgba[0] >> 3) << 11) |
		(((FxU16)rgba[1] >> 2) << 5) |
		((FxU16)rgba[2] >> 3));

	return packed;
}

static FxU8 mgl_convert_rgba_to_intensity8(const GLubyte *rgba)
{
	return rgba[0];
}

static qboolean mgl_convert_index_row(void *dst_row, int dst_width, int xoffset, const GLubyte *src, int width)
{
	int x;

	if (!dst_row || !src || width < 0 || dst_width <= 0 || xoffset < 0 || (xoffset + width) > dst_width)
		return false;

	for (x = 0; x < width; ++x)
	{
		int dst_x = mgl_texture_storage_x(xoffset + x, dst_width, GR_TEXFMT_P_8);
		((FxU8 *)dst_row)[dst_x] = src[x];
	}

	return true;
}

static qboolean mgl_convert_rgba_row(void *dst_row, int dst_width, int xoffset,
	const GLubyte *src, int width, GrTextureFormat_t format)
{
	int x;

	if (!dst_row || !src || width < 0 || dst_width <= 0 || xoffset < 0 || (xoffset + width) > dst_width)
		return false;

	for (x = 0; x < width; ++x)
	{
		const GLubyte *rgba = src + (x * 4);
		int dst_x = mgl_texture_storage_x(xoffset + x, dst_width, format);

		switch (format)
		{
		case GR_TEXFMT_INTENSITY_8:
			((FxU8 *)dst_row)[dst_x] = mgl_convert_rgba_to_intensity8(rgba);
			break;
		case GR_TEXFMT_RGB_565:
			((FxU16 *)dst_row)[dst_x] = mgl_convert_rgba_to_rgb565(rgba);
			break;
		case GR_TEXFMT_ARGB_1555:
			((FxU16 *)dst_row)[dst_x] = mgl_convert_rgba_to_argb1555(rgba);
			break;
		case GR_TEXFMT_ARGB_4444:
		default:
			((FxU16 *)dst_row)[dst_x] = mgl_convert_rgba_to_argb4444(rgba);
			break;
		}
	}

	return true;
}

static qboolean mgl_texture_define_image(mgl_texture_t *tex, GLint internalformat, GLint width, GLint height, const GLvoid *pixels)
{
	size_t data_size;
	const GLubyte *src_rows;
	GrTextureFormat_t format;
	int row;

	if (!tex)
		return false;

	format = mgl_choose_texture_format(internalformat);

	if (width <= 0 || height <= 0 || !mgl_is_power_of_two(width) || !mgl_is_power_of_two(height))
	{
		mgl_set_error(GL_INVALID_VALUE);
		return false;
	}

	if (!mgl_lod_from_size((width > height) ? width : height, &tex->lod) ||
		!mgl_aspect_from_size(width, height, &tex->aspect))
	{
		ri.Con_Printf(PRINT_ALL, "miniGL: unsupported SST-1 texture size %dx%d\n", width, height);
		mgl_set_error(GL_INVALID_VALUE);
		return false;
	}

	data_size = (size_t)width * (size_t)height * mgl_texture_bytes_per_texel(format);
	if (!tex->data || tex->data_size != data_size)
	{
		void *new_data = malloc(data_size);
		if (!new_data)
		{
			mgl_set_error(GL_OUT_OF_MEMORY);
			return false;
		}

		if (tex->data)
			free(tex->data);
		tex->data = new_data;
		tex->data_size = data_size;

		/* Texture dimensions changed — old clean copy is invalid. */
		if (tex->clean_data)
		{
			free(tex->clean_data);
			tex->clean_data = NULL;
		}
	}

	memset(tex->data, 0, data_size);
	src_rows = (const GLubyte *)pixels;

	if (pixels)
	{
		for (row = 0; row < height; ++row)
		{
			const GLubyte *src = src_rows + (row * width * 4);
			GLubyte *dst = ((GLubyte *)tex->data) + ((size_t)row * (size_t)width * mgl_texture_bytes_per_texel(format));
			mgl_convert_rgba_row(dst, width, 0, src, width, format);
		}
	}

	tex->defined = true;
	tex->width = width;
	tex->height = height;
	tex->format = format;
	tex->resident_generation = 0;
	tex->resident_addr = 0;
	tex->dirty_t_min = 0;
	tex->dirty_t_max = height - 1;
	tex->dirty = true;

	/* Dual-TMU: snapshot the baked static data into clean_data at upload
	 * time so the partial-TRAM-upload path can restore tex->data without
	 * a per-frame full-atlas scan.  Always refresh (don't guard with
	 * clean_data != NULL) so that a map reload picks up the new data. */
	if (mgl_texture_is_lightmap(tex) && mgl.num_tmus >= 2)
	{
		if (tex->clean_data)
		{
			free(tex->clean_data);
			tex->clean_data = NULL;
		}
		tex->clean_data = malloc(data_size);
		if (tex->clean_data)
			memcpy(tex->clean_data, tex->data, data_size);
	}

	if (mgl_texture_is_dynamic_lightmap(tex))
	{
		size_t required = mgl_texture_mem_required(tex);
		if (required > 0)
			mgl_reserve_dynamic_lightmap(required);
	}

	return true;
}

static qboolean mgl_texture_define_paletted_image(mgl_texture_t *tex, GLint internalformat, GLint width, GLint height, const GLvoid *pixels)
{
	size_t data_size;
	const GLubyte *src_rows;
	GrTextureFormat_t format;
	int row;

	if (!tex)
		return false;

	format = mgl_choose_texture_format(internalformat);

	if (width <= 0 || height <= 0 || !mgl_is_power_of_two(width) || !mgl_is_power_of_two(height))
	{
		mgl_set_error(GL_INVALID_VALUE);
		return false;
	}

	if (!mgl_lod_from_size((width > height) ? width : height, &tex->lod) ||
		!mgl_aspect_from_size(width, height, &tex->aspect))
	{
		ri.Con_Printf(PRINT_ALL, "miniGL: unsupported SST-1 texture size %dx%d\n", width, height);
		mgl_set_error(GL_INVALID_VALUE);
		return false;
	}

	data_size = (size_t)width * (size_t)height * mgl_texture_bytes_per_texel(format);
	if (!tex->data || tex->data_size != data_size)
	{
		void *new_data = malloc(data_size);
		if (!new_data)
		{
			mgl_set_error(GL_OUT_OF_MEMORY);
			return false;
		}

		if (tex->data)
			free(tex->data);
		tex->data = new_data;
		tex->data_size = data_size;

		/* Texture dimensions changed — old clean copy is invalid. */
		if (tex->clean_data)
		{
			free(tex->clean_data);
			tex->clean_data = NULL;
		}
	}

	memset(tex->data, 0, data_size);
	src_rows = (const GLubyte *)pixels;

	if (pixels)
	{
		for (row = 0; row < height; ++row)
		{
			const GLubyte *src = src_rows + (row * width);
			GLubyte *dst = ((GLubyte *)tex->data) + ((size_t)row * (size_t)width);
			mgl_convert_index_row(dst, width, 0, src, width);
		}
	}

	tex->defined = true;
	tex->width = width;
	tex->height = height;
	tex->format = format;
	tex->resident_generation = 0;
	tex->resident_addr = 0;
	tex->dirty = true;

	return true;
}

static qboolean mgl_texture_sub_image(mgl_texture_t *tex,
	GLint xoffset, GLint yoffset,
	GLsizei width, GLsizei height,
	const GLvoid *pixels)
{
	const GLubyte *src_rows;
	int dst_y;
	int row;

	if (!tex || !tex->defined || !tex->data)
	{
		mgl_set_error(GL_INVALID_OPERATION);
		return false;
	}

	if (!pixels ||
		xoffset < 0 || yoffset < 0 ||
		width < 0 || height < 0 ||
		(xoffset + width) > tex->width ||
		(yoffset + height) > tex->height)
	{
		mgl_set_error(GL_INVALID_VALUE);
		return false;
	}

	src_rows = (const GLubyte *)pixels;
	dst_y = yoffset;

	for (row = 0; row < height; ++row)
	{
		const GLubyte *src = src_rows + (row * width * 4);
		GLubyte *dst = ((GLubyte *)tex->data) + (((size_t)(dst_y + row) * (size_t)tex->width) * mgl_texture_bytes_per_texel(tex->format));
		mgl_convert_rgba_row(dst, tex->width, xoffset, src, width, tex->format);
	}

	/* Accumulate dirty row range so TMU1 can do a partial re-download
	 * instead of re-uploading the whole 128x128 atlas every time. */
	if (!tex->dirty)
	{
		tex->dirty_t_min = yoffset;
		tex->dirty_t_max = yoffset + height - 1;
	}
	else
	{
		if (yoffset              < tex->dirty_t_min) tex->dirty_t_min = yoffset;
		if (yoffset + height - 1 > tex->dirty_t_max) tex->dirty_t_max = yoffset + height - 1;
	}
	tex->dirty = true;
	tex->resident_generation = 0;

	return true;
}

static GrTextureFilterMode_t mgl_filter_mode(GLenum filter)
{
	switch (filter)
	{
	case GL_NEAREST:
	case GL_NEAREST_MIPMAP_NEAREST:
	case GL_NEAREST_MIPMAP_LINEAR:
		return GR_TEXTUREFILTER_POINT_SAMPLED;
	default:
		return GR_TEXTUREFILTER_BILINEAR;
	}
}

static GrTextureClampMode_t mgl_clamp_mode(GLenum wrap)
{
	return (wrap == GL_CLAMP) ? GR_TEXTURECLAMP_CLAMP : GR_TEXTURECLAMP_WRAP;
}

static GrAlphaBlendFnc_t mgl_blend_factor(GLenum factor)
{
	switch (factor)
	{
	case GL_ZERO: return GR_BLEND_ZERO;
	case GL_ONE: return GR_BLEND_ONE;
	case GL_SRC_ALPHA: return GR_BLEND_SRC_ALPHA;
	case GL_ONE_MINUS_SRC_ALPHA: return GR_BLEND_ONE_MINUS_SRC_ALPHA;
	case GL_SRC_COLOR: return GR_BLEND_SRC_COLOR;
	default: return GR_BLEND_ONE;
	}
}

static GrCmpFnc_t mgl_depth_compare(GLenum func)
{
	switch (func)
	{
	case GL_LESS:    return GR_CMP_GREATER;
	case GL_LEQUAL:  return GR_CMP_GEQUAL;
	case GL_EQUAL:   return GR_CMP_EQUAL;
	case GL_GREATER: return GR_CMP_LESS;
	case GL_GEQUAL:  return GR_CMP_LEQUAL;
	case GL_NEVER:   return GR_CMP_NEVER;
	case GL_ALWAYS:  return GR_CMP_ALWAYS;
	default:         return GR_CMP_GEQUAL;
	}
}

static GrCmpFnc_t mgl_alpha_compare(GLenum func)
{
	switch (func)
	{
	case GL_LESS:    return GR_CMP_LESS;
	case GL_LEQUAL:  return GR_CMP_LEQUAL;
	case GL_EQUAL:   return GR_CMP_EQUAL;
	case GL_GREATER: return GR_CMP_GREATER;
	case GL_GEQUAL:  return GR_CMP_GEQUAL;
	case GL_NEVER:   return GR_CMP_NEVER;
	case GL_ALWAYS:  return GR_CMP_ALWAYS;
	default:         return GR_CMP_ALWAYS;
	}
}

static void mgl_apply_clip_window(void)
{
	int minx = 0;
	int miny = 0;
	int maxx = mgl.video_width;
	int maxy = mgl.video_height;

	if (mgl.scissor_enabled)
	{
		minx = mgl_clampi(mgl.scissor[0], 0, mgl.video_width);
		maxx = mgl_clampi(mgl.scissor[0] + mgl.scissor[2], 0, mgl.video_width);
		miny = mgl.video_height - mgl.scissor[1] - mgl.scissor[3];
		maxy = miny + mgl.scissor[3];
		miny = mgl_clampi(miny, 0, mgl.video_height);
		maxy = mgl_clampi(maxy, 0, mgl.video_height);
	}

	mgl_set_clip_window((FxU32)minx, (FxU32)miny, (FxU32)maxx, (FxU32)maxy);
}

static qboolean mgl_ensure_texture_resident(mgl_texture_t *tex)
{
	GrTexInfo info;
	size_t required;
	FxU32 tex_addr;

	if (!tex || !tex->defined || !tex->data)
		return false;

	required = mgl_texture_mem_required(tex);
	if (required == 0)
		return false;

	if (mgl_texture_is_dynamic_lightmap(tex) &&
		(tex->resident_addr == mgl.dynamic_lightmap_addr) &&
		!tex->dirty)
		return true;

	if ((tex->resident_generation == mgl.texture_generation) && !tex->dirty)
		return true;

	if (mgl_texture_is_dynamic_lightmap(tex))
	{
		if ((mgl.dynamic_lightmap_size != required) &&
			!mgl_reserve_dynamic_lightmap(required))
		{
			return false;
		}

		tex_addr = mgl.dynamic_lightmap_addr;
	}
	else
	{
		if (!mgl_allocate_texture_address(tex, required, &tex_addr))
			mgl_reset_texture_cache();

		if (!mgl_allocate_texture_address(tex, required, &tex_addr))
		{
			return false;
		}
	}

	grTexDownloadMipMapLevel(GR_TMU0,
		tex_addr,
		tex->lod,
		tex->lod,
		tex->aspect,
		tex->format,
		GR_MIPMAPLEVELMASK_BOTH,
		tex->data);

	info.smallLod = tex->lod;
	info.largeLod = tex->lod;
	info.aspectRatio = tex->aspect;
	info.format = tex->format;
	info.data = NULL;

	if (gl_v1_log_uploads && gl_v1_log_uploads->value)
	{
		ri.Con_Printf(PRINT_ALL,
			"miniGL: upload tex %u (%s) %dx%d fmt=%d addr=0x%x bytes=%u dirty=%d\n",
			tex->name,
			mgl_debug_texture_label(tex),
			tex->width,
			tex->height,
			(int)tex->format,
			(unsigned)tex_addr,
			(unsigned)required,
			tex->dirty ? 1 : 0);
	}

	grTexSource(GR_TMU0, tex_addr, GR_MIPMAPLEVELMASK_BOTH, &info);

	tex->resident_addr = tex_addr;
	tex->resident_generation = mgl.texture_generation;
	tex->dirty = false;

	return true;
}

static void mgl_ensure_shared_palette_resident(const mgl_texture_t *tex)
{
	if (!tex || tex->format != GR_TEXFMT_P_8)
		return;
	if (!mgl.shared_texture_palette_enabled || !mgl.shared_palette_dirty)
		return;

	grTexDownloadTable(GR_TMU0, GR_TEXTABLE_PALETTE, &mgl.shared_palette);
	mgl.shared_palette_dirty = false;
}

static mgl_texture_t *mgl_prepare_texture(void)
{
	mgl_texture_t *tex;

	if (!mgl.texture_2d_enabled)
		return NULL;

	tex = mgl_get_texture(mgl.bound_texture);
	if (!tex || !tex->defined || !mgl_ensure_texture_resident(tex))
		return NULL;

	mgl_ensure_shared_palette_resident(tex);

	mgl_set_texture_filter(
		mgl_filter_mode(tex->min_filter),
		mgl_filter_mode(tex->mag_filter));
	mgl_set_texture_clamp(
		mgl_clamp_mode(tex->wrap_s),
		mgl_clamp_mode(tex->wrap_t));
	mgl_set_texture_mipmap(GR_MIPMAP_DISABLE, FXFALSE);
	mgl_set_texture_source(tex->resident_addr, tex->lod, tex->aspect, tex->format);
	mgl_set_texture_combine(GR_TEXTURECOMBINE_DECAL);

	return tex;
}

static void mgl_apply_state(qboolean textured)
{
	int depth_mode;
	int depth_function;
	int alpha_function;
	int alpha_reference;
	int blend_src;
	int blend_dst;

	mgl_set_render_buffer((mgl.draw_buffer == GL_FRONT) ? GR_BUFFER_FRONTBUFFER : GR_BUFFER_BACKBUFFER);
	mgl_apply_clip_window();
	mgl_set_stw_hint(0);
	mgl_set_cull_mode(GR_CULL_DISABLE);

	if (mgl.depth_test_enabled)
	{
		depth_mode = GR_DEPTHBUFFER_ZBUFFER;
		depth_function = mgl_depth_compare(mgl.depth_func);
	}
	else
	{
		depth_mode = GR_DEPTHBUFFER_DISABLE;
		depth_function = GR_CMP_ALWAYS;
	}

	mgl_set_depth_buffer_mode(depth_mode);
	mgl_set_depth_buffer_function(depth_function);
	mgl_set_depth_mask(mgl.depth_mask ? FXTRUE : FXFALSE);

	if (mgl.alpha_test_enabled)
	{
		alpha_function = mgl_alpha_compare(mgl.alpha_func);
		alpha_reference = (int)(mgl_clampf(mgl.alpha_ref, 0.0f, 1.0f) * 255.0f);
	}
	else
	{
		alpha_function = GR_CMP_ALWAYS;
		alpha_reference = 0;
	}

	mgl_set_alpha_test(alpha_function, alpha_reference);

	if (mgl.blend_enabled)
	{
		blend_src = mgl_blend_factor(mgl.blend_src);
		blend_dst = mgl_blend_factor(mgl.blend_dst);
	}
	else
	{
		blend_src = GR_BLEND_ONE;
		blend_dst = GR_BLEND_ZERO;
	}

	mgl_set_alpha_blend(blend_src, blend_dst, blend_src, blend_dst);

	if (textured)
	{
		if (mgl.tex_env_mode == GL_MODULATE)
		{
			mgl_set_color_combine(GR_COMBINE_FUNCTION_SCALE_OTHER,
				GR_COMBINE_FACTOR_LOCAL,
				GR_COMBINE_LOCAL_ITERATED,
				GR_COMBINE_OTHER_TEXTURE,
				FXFALSE);
			mgl_set_alpha_combine(GR_COMBINE_FUNCTION_SCALE_OTHER,
				GR_COMBINE_FACTOR_LOCAL,
				GR_COMBINE_LOCAL_ITERATED,
				GR_COMBINE_OTHER_TEXTURE,
				FXFALSE);
		}
		else
		{
			mgl_set_color_combine(GR_COMBINE_FUNCTION_SCALE_OTHER,
				GR_COMBINE_FACTOR_ONE,
				GR_COMBINE_LOCAL_NONE,
				GR_COMBINE_OTHER_TEXTURE,
				FXFALSE);
			mgl_set_alpha_combine(GR_COMBINE_FUNCTION_SCALE_OTHER,
				GR_COMBINE_FACTOR_ONE,
				GR_COMBINE_LOCAL_NONE,
				GR_COMBINE_OTHER_TEXTURE,
				FXFALSE);
		}
	}
	else
	{
		mgl_set_color_combine(GR_COMBINE_FUNCTION_LOCAL,
			GR_COMBINE_FACTOR_NONE,
			GR_COMBINE_LOCAL_ITERATED,
			GR_COMBINE_OTHER_NONE,
			FXFALSE);
		mgl_set_alpha_combine(GR_COMBINE_FUNCTION_LOCAL,
			GR_COMBINE_FACTOR_NONE,
			GR_COMBINE_LOCAL_ITERATED,
			GR_COMBINE_OTHER_NONE,
			FXFALSE);
	}
}

static void mgl_restore_after_clear(void)
{
	grColorMask(FXTRUE, FXFALSE);

	if (mgl.depth_test_enabled)
	{
		grDepthBufferMode(GR_DEPTHBUFFER_ZBUFFER);
		grDepthBufferFunction(mgl_depth_compare(mgl.depth_func));
	}
	else
	{
		grDepthBufferMode(GR_DEPTHBUFFER_DISABLE);
		grDepthBufferFunction(GR_CMP_ALWAYS);
	}

	grDepthMask(mgl.depth_mask ? FXTRUE : FXFALSE);
}

static void mgl_make_clear_vertex(GrVertex *dst, GLfloat x, GLfloat y, GLfloat depth)
{
	memset(dst, 0, sizeof(*dst));
	dst->x = x;
	dst->y = y;
	dst->z = depth;
	dst->ooz = (1.0f - depth) * 65535.0f;
	dst->oow = 1.0f;
	dst->r = mgl_clampf(mgl.clear_color[0], 0.0f, 1.0f) * 255.0f;
	dst->g = mgl_clampf(mgl.clear_color[1], 0.0f, 1.0f) * 255.0f;
	dst->b = mgl_clampf(mgl.clear_color[2], 0.0f, 1.0f) * 255.0f;
	dst->a = mgl_clampf(mgl.clear_color[3], 0.0f, 1.0f) * 255.0f;
	dst->tmuvtx[0].oow = 1.0f;
}

static void mgl_draw_clear_quad(qboolean clear_color, qboolean clear_depth)
{
	GrVertex v0;
	GrVertex v1;
	GrVertex v2;
	GrVertex v3;
	GLfloat max_x;
	GLfloat max_y;

	max_x = (GLfloat)mgl.video_width - 0.5f;
	max_y = (GLfloat)mgl.video_height - 0.5f;

	mgl_set_stw_hint(0);
	grCullMode(GR_CULL_DISABLE);
	grAlphaBlendFunction(GR_BLEND_ONE, GR_BLEND_ZERO, GR_BLEND_ONE, GR_BLEND_ZERO);
	grAlphaTestFunction(GR_CMP_ALWAYS);
	grAlphaTestReferenceValue(0);
	grColorMask(clear_color ? FXTRUE : FXFALSE, FXFALSE);

	if (clear_depth)
	{
		grDepthBufferMode(GR_DEPTHBUFFER_ZBUFFER);
		grDepthBufferFunction(GR_CMP_ALWAYS);
		grDepthMask(FXTRUE);
	}
	else
	{
		grDepthBufferMode(GR_DEPTHBUFFER_DISABLE);
		grDepthBufferFunction(GR_CMP_ALWAYS);
		grDepthMask(FXFALSE);
	}

	grColorCombine(GR_COMBINE_FUNCTION_LOCAL,
		GR_COMBINE_FACTOR_NONE,
		GR_COMBINE_LOCAL_ITERATED,
		GR_COMBINE_OTHER_NONE,
		FXFALSE);
	grAlphaCombine(GR_COMBINE_FUNCTION_LOCAL,
		GR_COMBINE_FACTOR_NONE,
		GR_COMBINE_LOCAL_ITERATED,
		GR_COMBINE_OTHER_NONE,
		FXFALSE);

	mgl_make_clear_vertex(&v0, -0.5f, -0.5f, 1.0f);
	mgl_make_clear_vertex(&v1, max_x, -0.5f, 1.0f);
	mgl_make_clear_vertex(&v2, max_x, max_y, 1.0f);
	mgl_make_clear_vertex(&v3, -0.5f, max_y, 1.0f);

	grDrawTriangle(&v0, &v1, &v2);
	grDrawTriangle(&v0, &v2, &v3);
}

static void mgl_do_clear(GLbitfield mask)
{
	qboolean clear_color;
	qboolean clear_depth;
	GrColor_t color;

	clear_color = ((mask & GL_COLOR_BUFFER_BIT) != 0);
	clear_depth = ((mask & GL_DEPTH_BUFFER_BIT) != 0);

	if (!clear_color && !clear_depth)
		return;

	mgl_apply_clip_window();
	grRenderBuffer((mgl.draw_buffer == GL_FRONT) ? GR_BUFFER_FRONTBUFFER : GR_BUFFER_BACKBUFFER);

	color =
		(((GrColor_t)(mgl_clampf(mgl.clear_color[0], 0.0f, 1.0f) * 255.0f)) << 16) |
		(((GrColor_t)(mgl_clampf(mgl.clear_color[1], 0.0f, 1.0f) * 255.0f)) << 8) |
		((GrColor_t)(mgl_clampf(mgl.clear_color[2], 0.0f, 1.0f) * 255.0f));

	if (mgl.scissor_enabled)
	{
		mgl_draw_clear_quad(clear_color, clear_depth);
	}
	else
	{
		grColorMask(clear_color ? FXTRUE : FXFALSE, FXFALSE);

		if (clear_depth)
		{
			grDepthBufferMode(GR_DEPTHBUFFER_ZBUFFER);
			grDepthBufferFunction(GR_CMP_ALWAYS);
			grDepthMask(FXTRUE);
		}
		else
		{
			grDepthBufferMode(GR_DEPTHBUFFER_DISABLE);
			grDepthBufferFunction(GR_CMP_ALWAYS);
			grDepthMask(FXFALSE);
		}

		grBufferClear(color, 0, GR_ZDEPTHVALUE_FARTHEST);
	}

	mgl_restore_after_clear();
	mgl_invalidate_hw_state();
}

static GLfloat mgl_clip_distance(const mgl_vertex_t *v, int plane)
{
	switch (plane)
	{
	case 0: return v->clip[0] + v->clip[3];
	case 1: return v->clip[3] - v->clip[0];
	case 2: return v->clip[1] + v->clip[3];
	case 3: return v->clip[3] - v->clip[1];
	case 4: return v->clip[2] + v->clip[3];
	case 5: return v->clip[3] - v->clip[2];
	default: return 0.0f;
	}
}

static void mgl_lerp_vertex(mgl_vertex_t *out, const mgl_vertex_t *a, const mgl_vertex_t *b, GLfloat t)
{
	int i;

	for (i = 0; i < 4; ++i)
	{
		out->clip[i] = a->clip[i] + ((b->clip[i] - a->clip[i]) * t);
		out->color[i] = a->color[i] + ((b->color[i] - a->color[i]) * t);
	}

	out->tex[0] = a->tex[0] + ((b->tex[0] - a->tex[0]) * t);
	out->tex[1] = a->tex[1] + ((b->tex[1] - a->tex[1]) * t);
	out->tex2[0] = a->tex2[0] + ((b->tex2[0] - a->tex2[0]) * t);
	out->tex2[1] = a->tex2[1] + ((b->tex2[1] - a->tex2[1]) * t);
}

static qboolean mgl_project_vertex(const mgl_vertex_t *src, GrVertex *dst, const mgl_texture_t *tex)
{
	GLfloat inv_w;
	GLfloat ndc_x;
	GLfloat ndc_y;
	GLfloat ndc_z;
	GLfloat depth;

	if (fabsf(src->clip[3]) < MGL_CLIP_EPSILON)
		return false;

	inv_w = 1.0f / src->clip[3];
	ndc_x = src->clip[0] * inv_w;
	ndc_y = src->clip[1] * inv_w;
	ndc_z = src->clip[2] * inv_w;
	depth = (GLfloat)(mgl.depth_range_near +
		(((ndc_z + 1.0f) * 0.5f) * (mgl.depth_range_far - mgl.depth_range_near)));
	depth = mgl_clampf(depth, 0.0f, 1.0f);

	dst->x = mgl.viewport[0] + ((ndc_x + 1.0f) * 0.5f * mgl.viewport[2]) + mgl.proj_screen_bias;
	dst->y = mgl.proj_view_top + ((1.0f - ndc_y) * 0.5f * mgl.viewport[3]) + mgl.proj_screen_bias;
	dst->z = depth;
	dst->ooz = (1.0f - depth) * 65535.0f;
	dst->oow = inv_w;
	dst->r = mgl_clampf(src->color[0], 0.0f, 1.0f) * 255.0f;
	dst->g = mgl_clampf(src->color[1], 0.0f, 1.0f) * 255.0f;
	dst->b = mgl_clampf(src->color[2], 0.0f, 1.0f) * 255.0f;
	dst->a = mgl_clampf(src->color[3], 0.0f, 1.0f) * 255.0f;

	dst->tmuvtx[0].oow = inv_w;
	dst->tmuvtx[0].sow = src->tex[0] * mgl.proj_tex_s_scale * inv_w;
	dst->tmuvtx[0].tow = src->tex[1] * mgl.proj_tex_t_scale * inv_w;

	if (mgl.num_tmus >= 2)
	{
		dst->tmuvtx[1].oow = inv_w;
		dst->tmuvtx[1].sow = src->tex2[0] * mgl.proj_tex1_s_scale * inv_w;
		dst->tmuvtx[1].tow = src->tex2[1] * mgl.proj_tex1_t_scale * inv_w;
	}

	return true;
}

static qboolean mgl_is_front_facing(const GrVertex *a, const GrVertex *b, const GrVertex *c)
{
	GLfloat area = ((b->x - a->x) * (c->y - a->y)) - ((b->y - a->y) * (c->x - a->x));
	return (area < 0.0f);
}

static qboolean mgl_projected_vertex_is_safe(const GrVertex *v, qboolean textured)
{
	if (!mgl_is_finite_float(v->x) ||
		!mgl_is_finite_float(v->y) ||
		!mgl_is_finite_float(v->oow) ||
		!mgl_is_finite_float(v->ooz))
		return false;

	if ((GLfloat)fabs(v->x) > MGL_SAFE_COORD_LIMIT ||
		(GLfloat)fabs(v->y) > MGL_SAFE_COORD_LIMIT)
		return false;

	if (v->oow <= MGL_CLIP_EPSILON || (GLfloat)fabs(v->oow) > MGL_SAFE_OOW_LIMIT)
		return false;

	if (textured)
	{
		if (!mgl_is_finite_float(v->tmuvtx[0].sow) ||
			!mgl_is_finite_float(v->tmuvtx[0].tow) ||
			!mgl_is_finite_float(v->tmuvtx[0].oow))
			return false;

		if ((GLfloat)fabs(v->tmuvtx[0].sow) > MGL_SAFE_STW_LIMIT ||
			(GLfloat)fabs(v->tmuvtx[0].tow) > MGL_SAFE_STW_LIMIT ||
			(GLfloat)fabs(v->tmuvtx[0].oow) > MGL_SAFE_OOW_LIMIT)
			return false;
	}

	return true;
}

static qboolean mgl_projected_triangle_is_safe(const GrVertex *a, const GrVertex *b, const GrVertex *c, qboolean textured)
{
	GLfloat area;

	if (!mgl_projected_vertex_is_safe(a, textured) ||
		!mgl_projected_vertex_is_safe(b, textured) ||
		!mgl_projected_vertex_is_safe(c, textured))
		return false;

	/*
	 * Match the SST-1 cube test's zero-area protection. cube.c computes
	 * 0.5f * cross and rejects abs(area) < 1.0f, which is equivalent to
	 * rejecting abs(cross) < 2.0f here.
	 */
	area = ((b->x - a->x) * (c->y - a->y)) - ((b->y - a->y) * (c->x - a->x));
	if ((GLfloat)fabs(area) < MGL_SAFE_TRIANGLE_AREA)
		return false;

	if (textured)
	{
		GLfloat ds1 = b->tmuvtx[0].sow - a->tmuvtx[0].sow;
		GLfloat ds2 = c->tmuvtx[0].sow - a->tmuvtx[0].sow;
		GLfloat dt1 = b->tmuvtx[0].tow - a->tmuvtx[0].tow;
		GLfloat dt2 = c->tmuvtx[0].tow - a->tmuvtx[0].tow;
		GLfloat dw1 = b->tmuvtx[0].oow - a->tmuvtx[0].oow;
		GLfloat dw2 = c->tmuvtx[0].oow - a->tmuvtx[0].oow;
		GLfloat dy1 = b->y - a->y;
		GLfloat dy2 = c->y - a->y;
		GLfloat dx1 = b->x - a->x;
		GLfloat dx2 = c->x - a->x;
		GLfloat dsdx = ((ds1 * dy2) - (ds2 * dy1)) / area;
		GLfloat dsdy = ((dx1 * ds2) - (dx2 * ds1)) / area;
		GLfloat dtdx = ((dt1 * dy2) - (dt2 * dy1)) / area;
		GLfloat dtdy = ((dx1 * dt2) - (dx2 * dt1)) / area;
		GLfloat dwdx = ((dw1 * dy2) - (dw2 * dy1)) / area;
		GLfloat dwdy = ((dx1 * dw2) - (dx2 * dw1)) / area;

		if (!mgl_is_finite_float(dsdx) || !mgl_is_finite_float(dsdy) ||
			!mgl_is_finite_float(dtdx) || !mgl_is_finite_float(dtdy) ||
			!mgl_is_finite_float(dwdx) || !mgl_is_finite_float(dwdy))
			return false;

		if ((GLfloat)fabs(dsdx) > MGL_SAFE_TMU_GRADIENT_LIMIT ||
			(GLfloat)fabs(dsdy) > MGL_SAFE_TMU_GRADIENT_LIMIT ||
			(GLfloat)fabs(dtdx) > MGL_SAFE_TMU_GRADIENT_LIMIT ||
			(GLfloat)fabs(dtdy) > MGL_SAFE_TMU_GRADIENT_LIMIT ||
			(GLfloat)fabs(dwdx) > MGL_SAFE_OOW_GRADIENT_LIMIT ||
			(GLfloat)fabs(dwdy) > MGL_SAFE_OOW_GRADIENT_LIMIT)
			return false;
	}

	return true;
}

static qboolean mgl_world_textured_triangle_is_safe(const mgl_vertex_t *sa,
	const mgl_vertex_t *sb,
	const mgl_vertex_t *sc,
	const GrVertex *a,
	const GrVertex *b,
	const GrVertex *c,
	const mgl_texture_t *tex)
{
	GLfloat smin;
	GLfloat smax;
	GLfloat tmin;
	GLfloat tmax;
	GLfloat area;
	GLfloat ds1;
	GLfloat ds2;
	GLfloat dt1;
	GLfloat dt2;
	GLfloat dy1;
	GLfloat dy2;
	GLfloat dx1;
	GLfloat dx2;
	GLfloat dsdx;
	GLfloat dsdy;
	GLfloat dtdx;
	GLfloat dtdy;

	if (!mgl_texture_is_world_base(tex))
		return true;

	smin = smax = sa->tex[0];
	tmin = tmax = sa->tex[1];

	if (sb->tex[0] < smin) smin = sb->tex[0];
	if (sb->tex[0] > smax) smax = sb->tex[0];
	if (sc->tex[0] < smin) smin = sc->tex[0];
	if (sc->tex[0] > smax) smax = sc->tex[0];

	if (sb->tex[1] < tmin) tmin = sb->tex[1];
	if (sb->tex[1] > tmax) tmax = sb->tex[1];
	if (sc->tex[1] < tmin) tmin = sc->tex[1];
	if (sc->tex[1] > tmax) tmax = sc->tex[1];

	if (!mgl_is_finite_float(smin) || !mgl_is_finite_float(smax) ||
		!mgl_is_finite_float(tmin) || !mgl_is_finite_float(tmax))
	{
		return false;
	}

	if (((GLfloat)fabs(smax - smin) > MGL_SAFE_WORLD_REPEAT_SPAN) ||
		((GLfloat)fabs(tmax - tmin) > MGL_SAFE_WORLD_REPEAT_SPAN))
	{
		return false;
	}

	area = ((b->x - a->x) * (c->y - a->y)) - ((b->y - a->y) * (c->x - a->x));
	if ((GLfloat)fabs(area) < MGL_SAFE_TRIANGLE_AREA)
	{
		return false;
	}

	ds1 = b->tmuvtx[0].sow - a->tmuvtx[0].sow;
	ds2 = c->tmuvtx[0].sow - a->tmuvtx[0].sow;
	dt1 = b->tmuvtx[0].tow - a->tmuvtx[0].tow;
	dt2 = c->tmuvtx[0].tow - a->tmuvtx[0].tow;
	dy1 = b->y - a->y;
	dy2 = c->y - a->y;
	dx1 = b->x - a->x;
	dx2 = c->x - a->x;
	dsdx = ((ds1 * dy2) - (ds2 * dy1)) / area;
	dsdy = ((dx1 * ds2) - (dx2 * ds1)) / area;
	dtdx = ((dt1 * dy2) - (dt2 * dy1)) / area;
	dtdy = ((dx1 * dt2) - (dx2 * dt1)) / area;

	if (!mgl_is_finite_float(dsdx) || !mgl_is_finite_float(dsdy) ||
		!mgl_is_finite_float(dtdx) || !mgl_is_finite_float(dtdy))
	{
		return false;
	}

	if ((GLfloat)fabs(dsdx) > MGL_SAFE_WORLD_TMU_GRADIENT_LIMIT ||
		(GLfloat)fabs(dsdy) > MGL_SAFE_WORLD_TMU_GRADIENT_LIMIT ||
		(GLfloat)fabs(dtdx) > MGL_SAFE_WORLD_TMU_GRADIENT_LIMIT ||
		(GLfloat)fabs(dtdy) > MGL_SAFE_WORLD_TMU_GRADIENT_LIMIT)
	{
		return false;
	}

	return true;
}

static qboolean mgl_clip_vertices_match(const mgl_vertex_t *a, const mgl_vertex_t *b)
{
	int i;

	for (i = 0; i < 4; ++i)
	{
		if (!mgl_is_finite_float(a->clip[i]) || !mgl_is_finite_float(b->clip[i]))
			return true;
		if ((GLfloat)fabs(a->clip[i] - b->clip[i]) > MGL_DUPLICATE_CLIP_EPSILON)
			return false;
	}

	return true;
}

static qboolean mgl_source_triangle_is_safe(const mgl_vertex_t *a, const mgl_vertex_t *b, const mgl_vertex_t *c)
{
	if (mgl_clip_vertices_match(a, b) ||
		mgl_clip_vertices_match(b, c) ||
		mgl_clip_vertices_match(a, c))
		return false;

	return true;
}

static qboolean mgl_vertex_inside_clip(const mgl_vertex_t *v)
{
	int plane;

	for (plane = 0; plane < 6; ++plane)
	{
		if (mgl_clip_distance(v, plane) < 0.0f)
			return false;
	}

	return true;
}

static qboolean mgl_triangle_inside_clip(const mgl_vertex_t *a, const mgl_vertex_t *b, const mgl_vertex_t *c)
{
	return mgl_vertex_inside_clip(a) &&
		mgl_vertex_inside_clip(b) &&
		mgl_vertex_inside_clip(c);
}

static qboolean mgl_polygon_inside_clip(const mgl_vertex_t *poly, int count)
{
	int i;

	for (i = 0; i < count; ++i)
	{
		if (!mgl_vertex_inside_clip(&poly[i]))
			return false;
	}

	return true;
}

static void mgl_rebase_repeat_triangle_texcoords(mgl_vertex_t *a, mgl_vertex_t *b, mgl_vertex_t *c, const mgl_texture_t *tex)
{
	GLfloat base_s = 0.0f;
	GLfloat base_t = 0.0f;

	if (!tex)
		return;

	if (tex->wrap_s == GL_REPEAT && mgl_is_finite_float(a->tex[0]))
		base_s = (GLfloat)floor((double)a->tex[0]);

	if (tex->wrap_t == GL_REPEAT && mgl_is_finite_float(a->tex[1]))
		base_t = (GLfloat)floor((double)a->tex[1]);

	if (base_s != 0.0f)
	{
		a->tex[0] -= base_s;
		b->tex[0] -= base_s;
		c->tex[0] -= base_s;
	}

	if (base_t != 0.0f)
	{
		a->tex[1] -= base_t;
		b->tex[1] -= base_t;
		c->tex[1] -= base_t;
	}
}

static void mgl_rebase_repeat_polygon_texcoords(mgl_vertex_t *poly, int count, const mgl_texture_t *tex)
{
	GLfloat base_s = 0.0f;
	GLfloat base_t = 0.0f;
	int i;

	if (!tex || count <= 0)
		return;

	if (tex->wrap_s == GL_REPEAT && mgl_is_finite_float(poly[0].tex[0]))
		base_s = (GLfloat)floor((double)poly[0].tex[0]);

	if (tex->wrap_t == GL_REPEAT && mgl_is_finite_float(poly[0].tex[1]))
		base_t = (GLfloat)floor((double)poly[0].tex[1]);

	if (base_s != 0.0f)
	{
		for (i = 0; i < count; ++i)
			poly[i].tex[0] -= base_s;
	}

	if (base_t != 0.0f)
	{
		for (i = 0; i < count; ++i)
			poly[i].tex[1] -= base_t;
	}
}

static int mgl_compact_clipped_polygon(mgl_vertex_t *poly, int count)
{
	int write = 0;
	int i;

	for (i = 0; i < count; ++i)
	{
		if (write > 0 && mgl_clip_vertices_match(&poly[write - 1], &poly[i]))
			continue;
		if (write != i)
			poly[write] = poly[i];
		++write;
	}

	if (write > 1 && mgl_clip_vertices_match(&poly[0], &poly[write - 1]))
		--write;

	return write;
}

static int mgl_compact_projected_polygon(mgl_vertex_t *poly, int count, const mgl_texture_t *tex)
{
	qboolean changed;

	if (count < 3)
		return count;

	do
	{
		int i;

		changed = false;

		for (i = 0; i < count; ++i)
		{
			int prev = (i + count - 1) % count;
			int next = (i + 1) % count;
			GrVertex gprev;
			GrVertex gcur;
			GrVertex gnext;
			GLfloat area;
			int move;

			if (!mgl_project_vertex(&poly[prev], &gprev, tex) ||
				!mgl_project_vertex(&poly[i], &gcur, tex) ||
				!mgl_project_vertex(&poly[next], &gnext, tex))
				continue;

			area = ((gcur.x - gprev.x) * (gnext.y - gprev.y)) -
				((gcur.y - gprev.y) * (gnext.x - gprev.x));

			if ((GLfloat)fabs(area) >= MGL_SAFE_TRIANGLE_AREA)
				continue;

			for (move = i; move + 1 < count; ++move)
				poly[move] = poly[move + 1];

			--count;
			changed = true;
			break;
		}
	}
	while (changed && count >= 3);

	return count;
}

static qboolean mgl_should_cull(const GrVertex *a, const GrVertex *b, const GrVertex *c)
{
	qboolean front;

	if (!mgl.cull_enabled)
		return false;

	front = mgl_is_front_facing(a, b, c);
	if (mgl.cull_face == GL_FRONT)
		return front;
	if (mgl.cull_face == GL_BACK)
		return !front;

	return false;
}

static void mgl_draw_triangle_core(const mgl_vertex_t *a, const mgl_vertex_t *b, const mgl_vertex_t *c,
	const GrVertex *pa, const GrVertex *pb, const GrVertex *pc, const mgl_texture_t *tex)
{
	GrVertex ga;
	GrVertex gb;
	GrVertex gc;

	ga = *pa;
	gb = *pb;
	gc = *pc;

	if (mgl.shade_model == GL_FLAT)
	{
		gb.r = gc.r = ga.r;
		gb.g = gc.g = ga.g;
		gb.b = gc.b = ga.b;
		gb.a = gc.a = ga.a;
	}

	if (!mgl_projected_triangle_is_safe(&ga, &gb, &gc, (tex != NULL)))
	{
		return;
	}

	if (!mgl_world_textured_triangle_is_safe(a, b, c, &ga, &gb, &gc, tex))
		return;

	if (mgl_should_cull(&ga, &gb, &gc))
		return;

	if (mgl.polygon_mode == GL_LINE)
	{
		grDrawLine(&ga, &gb);
		grDrawLine(&gb, &gc);
		grDrawLine(&gc, &ga);
	}
	else
	{
		grDrawTriangle(&ga, &gb, &gc);
	}
}

static void mgl_draw_projected_triangle(const mgl_vertex_t *a, const mgl_vertex_t *b, const mgl_vertex_t *c, const mgl_texture_t *tex)
{
	GrVertex ga;
	GrVertex gb;
	GrVertex gc;

	if (!mgl_project_vertex(a, &ga, tex) ||
		!mgl_project_vertex(b, &gb, tex) ||
		!mgl_project_vertex(c, &gc, tex))
		return;

	mgl_draw_triangle_core(a, b, c, &ga, &gb, &gc, tex);
}

static void mgl_emit_triangle(const mgl_vertex_t *a, const mgl_vertex_t *b, const mgl_vertex_t *c, const mgl_texture_t *tex)
{
	mgl_vertex_t src[MGL_CLIPPED_MAX_VERTS];
	mgl_vertex_t dst[MGL_CLIPPED_MAX_VERTS];
	mgl_vertex_t ta;
	mgl_vertex_t tb;
	mgl_vertex_t tc;
	mgl_vertex_t *in_poly = src;
	mgl_vertex_t *out_poly = dst;
	int in_count = 3;
	int plane;
	int i;

	if (!mgl_source_triangle_is_safe(a, b, c))
		return;

	ta = *a;
	tb = *b;
	tc = *c;
	mgl_rebase_repeat_triangle_texcoords(&ta, &tb, &tc, tex);

	if (mgl_triangle_inside_clip(&ta, &tb, &tc))
	{
		mgl_draw_projected_triangle(&ta, &tb, &tc, tex);
		return;
	}

	in_poly[0] = ta;
	in_poly[1] = tb;
	in_poly[2] = tc;

	for (plane = 0; plane < 6; ++plane)
	{
		int out_count = 0;
		mgl_vertex_t previous = in_poly[in_count - 1];
		GLfloat previous_distance = mgl_clip_distance(&previous, plane);
		qboolean previous_inside = (previous_distance >= 0.0f);

		for (i = 0; i < in_count; ++i)
		{
			mgl_vertex_t current = in_poly[i];
			GLfloat current_distance = mgl_clip_distance(&current, plane);
			qboolean current_inside = (current_distance >= 0.0f);

			if (current_inside != previous_inside)
			{
				mgl_vertex_t edge_vertex;
				GLfloat t = previous_distance / (previous_distance - current_distance);
				if (out_count >= MGL_CLIPPED_MAX_VERTS)
					return;
				mgl_lerp_vertex(&edge_vertex, &previous, &current, t);
				out_poly[out_count++] = edge_vertex;
			}

			if (current_inside)
			{
				if (out_count >= MGL_CLIPPED_MAX_VERTS)
					return;
				out_poly[out_count++] = current;
			}

			previous = current;
			previous_distance = current_distance;
			previous_inside = current_inside;
		}

		out_count = mgl_compact_clipped_polygon(out_poly, out_count);

		if (out_count < 3)
			return;

		in_count = out_count;
		if (in_poly == src)
		{
			in_poly = dst;
			out_poly = src;
		}
		else
		{
			in_poly = src;
			out_poly = dst;
		}
	}

	in_count = mgl_compact_projected_polygon(in_poly, in_count, tex);
	if (in_count < 3)
		return;

	for (i = 1; i < (in_count - 1); ++i)
		mgl_draw_projected_triangle(&in_poly[0], &in_poly[i], &in_poly[i + 1], tex);
}

static void mgl_emit_polygon(const mgl_vertex_t *poly, int count, const mgl_texture_t *tex)
{
	mgl_vertex_t src[MGL_CLIPPED_POLY_MAX_VERTS];
	mgl_vertex_t dst[MGL_CLIPPED_POLY_MAX_VERTS];
	mgl_vertex_t *in_poly = src;
	mgl_vertex_t *out_poly = dst;
	int in_count;
	int plane;
	int i;

	if (!poly || count < 3)
		return;

	if (count > (MGL_CLIPPED_POLY_MAX_VERTS - 6))
	{
		for (i = 2; i < count; ++i)
			mgl_emit_triangle(&poly[0], &poly[i - 1], &poly[i], tex);
		return;
	}

	for (i = 0; i < count; ++i)
		in_poly[i] = poly[i];

	in_count = count;
	mgl_rebase_repeat_polygon_texcoords(in_poly, in_count, tex);

	if (mgl_polygon_inside_clip(in_poly, in_count))
	{
		in_count = mgl_compact_projected_polygon(in_poly, in_count, tex);
		if (in_count < 3)
			return;

		for (i = 1; i < (in_count - 1); ++i)
			mgl_draw_projected_triangle(&in_poly[0], &in_poly[i], &in_poly[i + 1], tex);
		return;
	}

	for (plane = 0; plane < 6; ++plane)
	{
		int out_count = 0;
		mgl_vertex_t previous = in_poly[in_count - 1];
		GLfloat previous_distance = mgl_clip_distance(&previous, plane);
		qboolean previous_inside = (previous_distance >= 0.0f);

		for (i = 0; i < in_count; ++i)
		{
			mgl_vertex_t current = in_poly[i];
			GLfloat current_distance = mgl_clip_distance(&current, plane);
			qboolean current_inside = (current_distance >= 0.0f);

			if (current_inside != previous_inside)
			{
				mgl_vertex_t edge_vertex;
				GLfloat t = previous_distance / (previous_distance - current_distance);
				if (out_count >= MGL_CLIPPED_POLY_MAX_VERTS)
					return;
				mgl_lerp_vertex(&edge_vertex, &previous, &current, t);
				out_poly[out_count++] = edge_vertex;
			}

			if (current_inside)
			{
				if (out_count >= MGL_CLIPPED_POLY_MAX_VERTS)
					return;
				out_poly[out_count++] = current;
			}

			previous = current;
			previous_distance = current_distance;
			previous_inside = current_inside;
		}

		out_count = mgl_compact_clipped_polygon(out_poly, out_count);
		if (out_count < 3)
			return;

		in_count = out_count;
		if (in_poly == src)
		{
			in_poly = dst;
			out_poly = src;
		}
		else
		{
			in_poly = src;
			out_poly = dst;
		}
	}

	in_count = mgl_compact_projected_polygon(in_poly, in_count, tex);
	if (in_count < 3)
		return;

	for (i = 1; i < (in_count - 1); ++i)
		mgl_draw_projected_triangle(&in_poly[0], &in_poly[i], &in_poly[i + 1], tex);
}

static void mgl_emit_line(const mgl_vertex_t *a, const mgl_vertex_t *b)
{
	GrVertex ga;
	GrVertex gb;

	if (!mgl_project_vertex(a, &ga, NULL) || !mgl_project_vertex(b, &gb, NULL))
		return;
	if (!mgl_projected_vertex_is_safe(&ga, false) || !mgl_projected_vertex_is_safe(&gb, false))
		return;
	if (fabsf(ga.x - gb.x) < 0.25f && fabsf(ga.y - gb.y) < 0.25f)
		return;

	grDrawLine(&ga, &gb);
}

static void mgl_emit_point(const mgl_vertex_t *a)
{
	GrVertex gp;

	if (!mgl_project_vertex(a, &gp, NULL))
		return;
	if (!mgl_projected_vertex_is_safe(&gp, false))
		return;

	grDrawPoint(&gp);
}

static void mgl_flush_primitive(void)
{
	mgl_texture_t *tex = NULL;
	mgl_texture_t *tex1 = NULL;
	qboolean textured;
	qboolean dual_textured;
	int i;
	int primitive_count;

	if (!mgl.in_begin || mgl.primitive_count <= 0)
		return;

	primitive_count = mgl.primitive_count;
	tex = mgl_prepare_texture();
	textured = (tex != NULL);
	if (mgl.num_tmus >= 2)
		tex1 = mgl_prepare_texture_tmu1();
	dual_textured = (textured && (tex1 != NULL));
	mgl_apply_state(textured);

	if (dual_textured)
	{
		/* Override TMU0 combine: output = TMU0_tex * TMU1_output (base * lightmap) */
		mgl_set_texture_combine_multitex();
		/* Enable per-vertex ST differencing for both TMUs */
		mgl_set_stw_hint(GR_STWHINT_ST_DIFF_TMU0 | GR_STWHINT_ST_DIFF_TMU1);
	}

	/* Cache per-flush projection constants used by mgl_project_vertex() */
	mgl.proj_view_top = (GLfloat)(mgl.video_height - (mgl.viewport[1] + mgl.viewport[3]));
	mgl.proj_screen_bias = mgl_is_ortho_2d_path() ? 0.0f : 0.5f;
	if (tex)
		mgl_texture_coord_scales(tex->aspect, &mgl.proj_tex_s_scale, &mgl.proj_tex_t_scale);
	else
	{
		mgl.proj_tex_s_scale = 1.0f;
		mgl.proj_tex_t_scale = 1.0f;
	}
	if (tex1)
		mgl_texture_coord_scales(tex1->aspect, &mgl.proj_tex1_s_scale, &mgl.proj_tex1_t_scale);
	else
	{
		mgl.proj_tex1_s_scale = 0.0f;
		mgl.proj_tex1_t_scale = 0.0f;
	}

	switch (mgl.primitive_mode)
	{
	case GL_TRIANGLES:
		for (i = 0; i + 2 < mgl.primitive_count; i += 3)
			mgl_emit_triangle(&mgl.primitive[i], &mgl.primitive[i + 1], &mgl.primitive[i + 2], tex);
		break;

	case GL_TRIANGLE_STRIP:
		for (i = 2; i < mgl.primitive_count; ++i)
		{
			if (i & 1)
				mgl_emit_triangle(&mgl.primitive[i - 1], &mgl.primitive[i - 2], &mgl.primitive[i], tex);
			else
				mgl_emit_triangle(&mgl.primitive[i - 2], &mgl.primitive[i - 1], &mgl.primitive[i], tex);
		}
		break;

	case GL_TRIANGLE_FAN:
		for (i = 2; i < mgl.primitive_count; ++i)
			mgl_emit_triangle(&mgl.primitive[0], &mgl.primitive[i - 1], &mgl.primitive[i], tex);
		break;

	case GL_POLYGON:
		mgl_emit_polygon(mgl.primitive, mgl.primitive_count, tex);
		break;

	case GL_QUADS:
		for (i = 0; i + 3 < mgl.primitive_count; i += 4)
			mgl_emit_polygon(&mgl.primitive[i], 4, tex);
		break;

	case GL_LINES:
		for (i = 0; i + 1 < mgl.primitive_count; i += 2)
			mgl_emit_line(&mgl.primitive[i], &mgl.primitive[i + 1]);
		break;

	case GL_LINE_STRIP:
		for (i = 1; i < mgl.primitive_count; ++i)
			mgl_emit_line(&mgl.primitive[i - 1], &mgl.primitive[i]);
		break;

	case GL_POINTS:
		for (i = 0; i < mgl.primitive_count; ++i)
			mgl_emit_point(&mgl.primitive[i]);
		break;

	default:
		mgl_set_error(GL_INVALID_ENUM);
		break;
	}

	mgl.in_begin = false;
	mgl.primitive_count = 0;
}

static void mgl_submit_vertex(GLfloat x, GLfloat y, GLfloat z, GLfloat w)
{
	GLfloat object[4];
	GLfloat eye[4];
	mgl_vertex_t *vertex;

	if (!mgl.in_begin)
	{
		mgl_set_error(GL_INVALID_OPERATION);
		return;
	}

	if (mgl.primitive_count >= MGL_MAX_PRIM_VERTS)
	{
		mgl_set_error(GL_OUT_OF_MEMORY);
		return;
	}

	object[0] = x;
	object[1] = y;
	object[2] = z;
	object[3] = w;

	vertex = &mgl.primitive[mgl.primitive_count++];
	mgl_vec4_mul(eye, mgl.modelview, object);
	mgl_vec4_mul(vertex->clip, mgl.projection, eye);
	memcpy(vertex->color, mgl.current_color, sizeof(vertex->color));
	memcpy(vertex->tex, mgl.current_texcoord, sizeof(vertex->tex));
	memcpy(vertex->tex2, mgl.current_texcoord2, sizeof(vertex->tex2));
}

static void mgl_reset_texture_object(mgl_texture_t *tex)
{
	if (!tex)
		return;

	if (tex->data)
	{
		free(tex->data);
		tex->data = NULL;
	}

	if (tex->clean_data)
	{
		free(tex->clean_data);
		tex->clean_data = NULL;
	}

	memset(tex, 0, sizeof(*tex));
	tex->min_filter = GL_LINEAR_MIPMAP_NEAREST;
	tex->mag_filter = GL_LINEAR;
	tex->wrap_s = GL_REPEAT;
	tex->wrap_t = GL_REPEAT;
}

static void mgl_reset_state(void)
{
	int video_width = mgl.video_width;
	int video_height = mgl.video_height;
	FxU32 tex_min_addr = mgl.tex_min_addr;
	FxU32 tex_max_addr = mgl.tex_max_addr;
	int num_tmus = mgl.num_tmus;
	FxU32 tex1_min_addr = mgl.tex1_min_addr;
	FxU32 tex1_max_addr = mgl.tex1_max_addr;
	unsigned i;

	memset(&mgl, 0, sizeof(mgl));
	mgl.active = true;
	mgl.glide_initialized = true;
	mgl.video_width = video_width;
	mgl.video_height = video_height;
	mgl.tex_min_addr = tex_min_addr;
	mgl.tex_max_addr = tex_max_addr;
	mgl.num_tmus = num_tmus;
	mgl.tex1_min_addr = tex1_min_addr;
	mgl.tex1_max_addr = tex1_max_addr;
	mgl.tex1_generation = 1;
	mgl.tex1_next_addr = tex1_min_addr;
	mgl.tex1_high_addr = tex1_max_addr;
	mgl.draw_buffer = GL_BACK;
	mgl.viewport[0] = 0;
	mgl.viewport[1] = 0;
	mgl.viewport[2] = mgl.video_width;
	mgl.viewport[3] = mgl.video_height;
	mgl.scissor[0] = 0;
	mgl.scissor[1] = 0;
	mgl.scissor[2] = mgl.video_width;
	mgl.scissor[3] = mgl.video_height;
	mgl.blend_src = GL_SRC_ALPHA;
	mgl.blend_dst = GL_ONE_MINUS_SRC_ALPHA;
	mgl.depth_func = GL_LEQUAL;
	mgl.depth_mask = GL_TRUE;
	mgl.depth_range_near = 0.0;
	mgl.depth_range_far = 1.0;
	mgl.alpha_func = GL_ALWAYS;
	mgl.alpha_ref = 0.0f;
	mgl.cull_face = GL_BACK;
	mgl.shade_model = GL_SMOOTH;
	mgl.polygon_mode = GL_FILL;
	mgl.tex_env_mode = GL_REPLACE;
	mgl.tex_env_mode2 = GL_REPLACE;
	mgl.point_size = 1.0f;
	mgl.clear_color[0] = 0.0f;
	mgl.clear_color[1] = 0.0f;
	mgl.clear_color[2] = 0.0f;
	mgl.clear_color[3] = 0.0f;
	mgl.current_color[0] = 1.0f;
	mgl.current_color[1] = 1.0f;
	mgl.current_color[2] = 1.0f;
	mgl.current_color[3] = 1.0f;
	mgl.texture_generation = 1;
	mgl.tex_next_addr = mgl.tex_min_addr;
	mgl.tex_high_addr = mgl.tex_max_addr;
	mgl.bound_texture = 0;
	mgl.current_error = GL_NO_ERROR;
	mgl.matrix_mode = GL_MODELVIEW;
	mgl_mat_identity(mgl.modelview);
	mgl_mat_identity(mgl.projection);

	for (i = 0; i < MGL_MAX_TEXTURES; ++i)
	{
		mgl_textures[i].resident_generation = 0;
		mgl_textures[i].resident_generation_tmu1 = 0;
	}

	grRenderBuffer(GR_BUFFER_BACKBUFFER);
	grSstOrigin(GR_ORIGIN_UPPER_LEFT);
	mgl_set_stw_hint(0);
	grDepthBufferMode(GR_DEPTHBUFFER_DISABLE);
	grDepthBufferFunction(GR_CMP_ALWAYS);
	grDepthMask(FXTRUE);
	grAlphaBlendFunction(GR_BLEND_ONE, GR_BLEND_ZERO, GR_BLEND_ONE, GR_BLEND_ZERO);
	grAlphaTestFunction(GR_CMP_ALWAYS);
	grAlphaTestReferenceValue(0);
	grCullMode(GR_CULL_DISABLE);
	grClipWindow(0, 0, (FxU32)mgl.video_width, (FxU32)mgl.video_height);
	mgl_invalidate_hw_state();
}

qboolean MiniGL_SetMode(GrScreenResolution_t resolution, int width, int height)
{
	unsigned i;

	for (i = 0; i < MGL_MAX_TEXTURES; ++i)
		mgl_textures[i].resident_generation = 0;

	if (mgl.active)
		grSstWinClose();

	if (mgl.glide_initialized)
		grGlideShutdown();

	memset(&mgl, 0, sizeof(mgl));
	grGlideInit();
	mgl.glide_initialized = true;

	grSstSelect(0);
	if (!grSstWinOpen(0,
		resolution,
		GR_REFRESH_60Hz,
		GR_COLORFORMAT_ABGR,
		GR_ORIGIN_UPPER_LEFT,
		2,
		1))
	{
		grGlideShutdown();
		memset(&mgl, 0, sizeof(mgl));
		return false;
	}

	mgl.video_width = width;
	mgl.video_height = height;
	mgl.tex_min_addr = grTexMinAddress(GR_TMU0);
	mgl.tex_max_addr = grTexMaxAddress(GR_TMU0);

	/* Detect number of TMUs via hardware query */
	{
		GrHwConfiguration hwConfig;
		mgl.num_tmus = 1;
		mgl.tex1_min_addr = 0;
		mgl.tex1_max_addr = 0;
		if (grSstQueryHardware(&hwConfig) && hwConfig.num_sst > 0)
		{
			int ntmu = hwConfig.SSTs[0].sstBoard.VoodooConfig.nTexelfx;
			if (ntmu >= 2)
			{
				mgl.num_tmus = 2;
				mgl.tex1_min_addr = grTexMinAddress(GR_TMU1);
				mgl.tex1_max_addr = grTexMaxAddress(GR_TMU1);
				ri.Con_Printf(PRINT_ALL, "miniGL: dual-TMU Voodoo detected, TMU1 TRAM 0x%x-0x%x\n",
					(unsigned)mgl.tex1_min_addr, (unsigned)mgl.tex1_max_addr);
			}
		}
	}

	guTexMemReset();
	mgl_reset_state();
	mgl.texture_generation = 1;
	mgl.tex_next_addr = mgl.tex_min_addr;
	mgl.tex_high_addr = mgl.tex_max_addr;
	mgl.dynamic_lightmap_addr = 0;
	mgl.dynamic_lightmap_size = 0;

	for (i = 0; i < MGL_MAX_TEXTURES; ++i)
		mgl_textures[i].resident_generation = 0;

	if (mgl_textures[TEXNUM_LIGHTMAPS].defined)
	{
		size_t required = mgl_texture_mem_required(&mgl_textures[TEXNUM_LIGHTMAPS]);
		if (required > 0)
			mgl_reserve_dynamic_lightmap(required);
	}

	mgl_do_clear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
	return true;
}

void MiniGL_Shutdown(void)
{
	unsigned i;

	if (mgl.active)
		grSstWinClose();

	if (mgl.glide_initialized)
		grGlideShutdown();

	memset(&mgl, 0, sizeof(mgl));
	for (i = 0; i < MGL_MAX_TEXTURES; ++i)
		mgl_textures[i].resident_generation = 0;
}

void MiniGL_SwapBuffers(void)
{
	if (!mgl.active)
		return;

	if (mgl.draw_buffer == GL_FRONT)
	{
		grSstIdle();
		return;
	}

	grBufferSwap(1);
	grRenderBuffer(GR_BUFFER_BACKBUFFER);
	mgl.hw.render_buffer = GR_BUFFER_BACKBUFFER;
	mgl.hw.render_buffer_valid = true;
}

void APIENTRY glAlphaFunc(GLenum func, GLclampf ref)
{
	mgl.alpha_func = func;
	mgl.alpha_ref = ref;
}

void APIENTRY glArrayElement(GLint i)
{
	const GLfloat *vertex;
	const GLfloat *color;
	int vertex_stride;
	int color_stride;

	if (!mgl.vertex_array.enabled || !mgl.vertex_array.pointer)
		return;

	if (mgl.vertex_array.type != GL_FLOAT)
	{
		mgl_set_error(GL_INVALID_ENUM);
		return;
	}

	vertex_stride = mgl.vertex_array.stride ? mgl.vertex_array.stride : (mgl.vertex_array.size * (int)sizeof(GLfloat));
	vertex = (const GLfloat *)((const GLubyte *)mgl.vertex_array.pointer + (i * vertex_stride));

	if (mgl.color_array.enabled && mgl.color_array.pointer && mgl.color_array.type == GL_FLOAT)
	{
		color_stride = mgl.color_array.stride ? mgl.color_array.stride : (mgl.color_array.size * (int)sizeof(GLfloat));
		color = (const GLfloat *)((const GLubyte *)mgl.color_array.pointer + (i * color_stride));
		mgl.current_color[0] = color[0];
		mgl.current_color[1] = color[1];
		mgl.current_color[2] = color[2];
		mgl.current_color[3] = (mgl.color_array.size >= 4) ? color[3] : 1.0f;
	}

	mgl_submit_vertex(vertex[0], vertex[1], (mgl.vertex_array.size >= 3) ? vertex[2] : 0.0f, 1.0f);
}

void APIENTRY glBegin(GLenum mode)
{
	switch (mode)
	{
	case GL_POINTS:
	case GL_LINES:
	case GL_LINE_STRIP:
	case GL_TRIANGLES:
	case GL_TRIANGLE_STRIP:
	case GL_TRIANGLE_FAN:
	case GL_QUADS:
	case GL_POLYGON:
		break;
	default:
		mgl_set_error(GL_INVALID_ENUM);
		return;
	}

	if (mgl.in_begin)
	{
		mgl_set_error(GL_INVALID_OPERATION);
		return;
	}

	mgl.in_begin = true;
	mgl.primitive_mode = mode;
	mgl.primitive_count = 0;
}

void APIENTRY glBindTexture(GLenum target, GLuint texture)
{
	mgl_texture_t *tex;

	if (target != GL_TEXTURE_2D)
	{
		mgl_set_error(GL_INVALID_ENUM);
		return;
	}

	tex = mgl_get_texture(texture);
	if (!tex)
		return;

	if (!tex->defined && !tex->data_size)
	{
		tex->min_filter = GL_LINEAR_MIPMAP_NEAREST;
		tex->mag_filter = GL_LINEAR;
		tex->wrap_s = GL_REPEAT;
		tex->wrap_t = GL_REPEAT;
	}

	if (mgl.active_texture_unit == 1)
		mgl.bound_texture2 = texture;
	else
		mgl.bound_texture = texture;
}

void APIENTRY glBlendFunc(GLenum sfactor, GLenum dfactor)
{
	mgl.blend_src = sfactor;
	mgl.blend_dst = dfactor;
}

void APIENTRY glClear(GLbitfield mask)
{
	if (!mgl.active)
		return;

	if (mgl.in_begin)
	{
		mgl_set_error(GL_INVALID_OPERATION);
		return;
	}

	mgl_do_clear(mask);
}

void APIENTRY glClearColor(GLclampf red, GLclampf green, GLclampf blue, GLclampf alpha)
{
	mgl.clear_color[0] = red;
	mgl.clear_color[1] = green;
	mgl.clear_color[2] = blue;
	mgl.clear_color[3] = alpha;
}

void APIENTRY glColor3f(GLfloat red, GLfloat green, GLfloat blue)
{
	mgl.current_color[0] = red;
	mgl.current_color[1] = green;
	mgl.current_color[2] = blue;
	mgl.current_color[3] = 1.0f;
}

void APIENTRY glColor3fv(const GLfloat *v)
{
	glColor3f(v[0], v[1], v[2]);
}

void APIENTRY glColor4f(GLfloat red, GLfloat green, GLfloat blue, GLfloat alpha)
{
	mgl.current_color[0] = red;
	mgl.current_color[1] = green;
	mgl.current_color[2] = blue;
	mgl.current_color[3] = alpha;
}

void APIENTRY glColor4fv(const GLfloat *v)
{
	glColor4f(v[0], v[1], v[2], v[3]);
}

void APIENTRY glColor4ubv(const GLubyte *v)
{
	mgl.current_color[0] = v[0] / 255.0f;
	mgl.current_color[1] = v[1] / 255.0f;
	mgl.current_color[2] = v[2] / 255.0f;
	mgl.current_color[3] = v[3] / 255.0f;
}

void APIENTRY glColorTableEXT(GLenum target, GLenum internalformat, GLsizei width,
	GLenum format, GLenum type, const GLvoid *table)
{
	int i;
	int stride;
	const GLubyte *src;

	if (target != GL_SHARED_TEXTURE_PALETTE_EXT)
	{
		mgl_set_error(GL_INVALID_ENUM);
		return;
	}

	if ((internalformat != GL_RGB) && (internalformat != GL_RGBA))
	{
		mgl_set_error(GL_INVALID_ENUM);
		return;
	}

	if ((width != 256) ||
		((format != GL_RGB) && (format != GL_RGBA)) ||
		(type != GL_UNSIGNED_BYTE) ||
		!table)
	{
		mgl_set_error(GL_INVALID_VALUE);
		return;
	}

	stride = (format == GL_RGBA) ? 4 : 3;
	src = (const GLubyte *)table;

	for (i = 0; i < 256; ++i)
	{
		mgl.shared_palette.data[i] =
			(FxU32)src[0] |
			((FxU32)src[1] << 8) |
			((FxU32)src[2] << 16);
		src += stride;
	}

	mgl.shared_palette_dirty = true;
}

void APIENTRY glColorPointer(GLint size, GLenum type, GLsizei stride, const GLvoid *pointer)
{
	mgl.color_array.pointer = (const GLfloat *)pointer;
	mgl.color_array.size = size;
	mgl.color_array.type = type;
	mgl.color_array.stride = stride;
}

void APIENTRY glCullFace(GLenum mode)
{
	mgl.cull_face = mode;
}

void APIENTRY glDeleteTextures(GLsizei n, const GLuint *textures)
{
	GLsizei i;

	for (i = 0; i < n; ++i)
	{
		if (textures[i] < MGL_MAX_TEXTURES)
		{
			if (mgl.bound_texture == textures[i])
				mgl.bound_texture = 0;
			if (mgl.bound_texture2 == textures[i])
				mgl.bound_texture2 = 0;
			mgl_reset_texture_object(&mgl_textures[textures[i]]);
		}
	}
}

void APIENTRY glDepthFunc(GLenum func)
{
	mgl.depth_func = func;
}

void APIENTRY glDepthMask(GLboolean flag)
{
	mgl.depth_mask = flag;
}

void APIENTRY glDepthRange(GLclampd zNear, GLclampd zFar)
{
	mgl.depth_range_near = zNear;
	mgl.depth_range_far = zFar;
}

void APIENTRY glDisable(GLenum cap)
{
	switch (cap)
	{
	case GL_TEXTURE_2D:
		if (mgl.active_texture_unit == 1)
			mgl.texture_2d_enabled2 = false;
		else
			mgl.texture_2d_enabled = false;
		break;
	case GL_ALPHA_TEST:     mgl.alpha_test_enabled = false; break;
	case GL_DEPTH_TEST:     mgl.depth_test_enabled = false; break;
	case GL_CULL_FACE:      mgl.cull_enabled = false; break;
	case GL_BLEND:          mgl.blend_enabled = false; break;
	case GL_SCISSOR_TEST:   mgl.scissor_enabled = false; break;
	case GL_SHARED_TEXTURE_PALETTE_EXT: mgl.shared_texture_palette_enabled = false; break;
	case GL_POINT_SMOOTH:   break;
	default:                mgl_set_error(GL_INVALID_ENUM); break;
	}
}

void APIENTRY glDisableClientState(GLenum array)
{
	switch (array)
	{
	case GL_VERTEX_ARRAY: mgl.vertex_array.enabled = false; break;
	case GL_COLOR_ARRAY:  mgl.color_array.enabled = false; break;
	default:              mgl_set_error(GL_INVALID_ENUM); break;
	}
}

void APIENTRY glDrawBuffer(GLenum mode)
{
	switch (mode)
	{
	case GL_FRONT:
	case GL_BACK:
	case GL_BACK_LEFT:
		mgl.draw_buffer = mode;
		break;
	default:
		mgl_set_error(GL_INVALID_ENUM);
		break;
	}
}

void APIENTRY glEnable(GLenum cap)
{
	switch (cap)
	{
	case GL_TEXTURE_2D:
		if (mgl.active_texture_unit == 1)
			mgl.texture_2d_enabled2 = true;
		else
			mgl.texture_2d_enabled = true;
		break;
	case GL_ALPHA_TEST:     mgl.alpha_test_enabled = true; break;
	case GL_DEPTH_TEST:     mgl.depth_test_enabled = true; break;
	case GL_CULL_FACE:      mgl.cull_enabled = true; break;
	case GL_BLEND:          mgl.blend_enabled = true; break;
	case GL_SCISSOR_TEST:   mgl.scissor_enabled = true; break;
	case GL_SHARED_TEXTURE_PALETTE_EXT: mgl.shared_texture_palette_enabled = true; break;
	case GL_POINT_SMOOTH:   break;
	default:                mgl_set_error(GL_INVALID_ENUM); break;
	}
}

void APIENTRY glEnableClientState(GLenum array)
{
	switch (array)
	{
	case GL_VERTEX_ARRAY: mgl.vertex_array.enabled = true; break;
	case GL_COLOR_ARRAY:  mgl.color_array.enabled = true; break;
	default:              mgl_set_error(GL_INVALID_ENUM); break;
	}
}

void APIENTRY glEnd(void)
{
	mgl_flush_primitive();
}

void APIENTRY glFinish(void)
{
	if (mgl.active)
		grSstIdle();
}

void APIENTRY glFlush(void)
{
}

void APIENTRY glFrustum(GLdouble left, GLdouble right, GLdouble bottom, GLdouble top, GLdouble zNear, GLdouble zFar)
{
	mgl_mat_frustum(mgl_current_matrix(), left, right, bottom, top, zNear, zFar);
}

GLenum APIENTRY glGetError(void)
{
	GLenum error = mgl.current_error;
	mgl.current_error = GL_NO_ERROR;
	return error;
}

void APIENTRY glGetFloatv(GLenum pname, GLfloat *params)
{
	switch (pname)
	{
	case GL_MODELVIEW_MATRIX:
		memcpy(params, mgl.modelview, sizeof(GLfloat) * 16);
		break;
	case GL_PROJECTION_MATRIX:
		memcpy(params, mgl.projection, sizeof(GLfloat) * 16);
		break;
	default:
		memset(params, 0, sizeof(GLfloat) * 16);
		mgl_set_error(GL_INVALID_ENUM);
		break;
	}
}

const GLubyte * APIENTRY glGetString(GLenum name)
{
	static const GLubyte vendor[] = "3Dfx Interactive";
	static const GLubyte renderer[] = "Voodoo Graphics SST-1 miniGL";
	static const GLubyte version[] = "1.1";
	static const GLubyte extensions_1tmu[] = "GL_EXT_paletted_texture GL_EXT_shared_texture_palette";
	static const GLubyte extensions_2tmu[] = "GL_EXT_paletted_texture GL_EXT_shared_texture_palette GL_SGIS_multitexture";

	switch (name)
	{
	case GL_VENDOR:     return vendor;
	case GL_RENDERER:   return renderer;
	case GL_VERSION:    return version;
	case GL_EXTENSIONS:
		return (mgl.num_tmus >= 2) ? extensions_2tmu : extensions_1tmu;
	default:
		mgl_set_error(GL_INVALID_ENUM);
		return extensions_1tmu;
	}
}

void APIENTRY glLoadIdentity(void)
{
	mgl_mat_identity(mgl_current_matrix());
}

void APIENTRY glLoadMatrixf(const GLfloat *m)
{
	mgl_mat_copy(mgl_current_matrix(), m);
}

void APIENTRY glMatrixMode(GLenum mode)
{
	if ((mode != GL_MODELVIEW) && (mode != GL_PROJECTION))
	{
		mgl_set_error(GL_INVALID_ENUM);
		return;
	}

	mgl.matrix_mode = mode;
}

void APIENTRY glOrtho(GLdouble left, GLdouble right, GLdouble bottom, GLdouble top, GLdouble zNear, GLdouble zFar)
{
	mgl_mat_ortho(mgl_current_matrix(), left, right, bottom, top, zNear, zFar);
}

void APIENTRY glPointSize(GLfloat size)
{
	mgl.point_size = (size < 1.0f) ? 1.0f : size;
}

void APIENTRY glPolygonMode(GLenum face, GLenum mode)
{
	if (face != GL_FRONT_AND_BACK)
	{
		mgl_set_error(GL_INVALID_ENUM);
		return;
	}

	mgl.polygon_mode = mode;
}

void APIENTRY glPopMatrix(void)
{
	GLfloat *current = mgl_current_matrix();
	GLfloat (*stack)[16] = mgl_current_stack();
	int *depth = mgl_current_stack_depth();

	if (*depth <= 0)
	{
		mgl_set_error(GL_STACK_UNDERFLOW);
		return;
	}

	(*depth)--;
	mgl_mat_copy(current, stack[*depth]);
}

void APIENTRY glPushMatrix(void)
{
	GLfloat *current = mgl_current_matrix();
	GLfloat (*stack)[16] = mgl_current_stack();
	int *depth = mgl_current_stack_depth();

	if (*depth >= MGL_MAX_STACK_DEPTH)
	{
		mgl_set_error(GL_STACK_OVERFLOW);
		return;
	}

	mgl_mat_copy(stack[*depth], current);
	(*depth)++;
}

void APIENTRY glReadPixels(GLint x, GLint y, GLsizei width, GLsizei height, GLenum format, GLenum type, GLvoid *pixels)
{
	FxU16 *tmp;
	int row;
	int col;
	int src_y;

	if (!pixels || width <= 0 || height <= 0 || format != GL_RGB || type != GL_UNSIGNED_BYTE)
	{
		mgl_set_error(GL_INVALID_ENUM);
		return;
	}

	tmp = (FxU16 *)malloc(width * height * sizeof(FxU16));
	if (!tmp)
	{
		mgl_set_error(GL_OUT_OF_MEMORY);
		return;
	}

	src_y = mgl.video_height - (y + height);
	if (src_y < 0)
		src_y = 0;

	if (!grLfbReadRegion((mgl.draw_buffer == GL_FRONT) ? GR_BUFFER_FRONTBUFFER : GR_BUFFER_BACKBUFFER,
		(FxU32)x,
		(FxU32)src_y,
		(FxU32)width,
		(FxU32)height,
		(FxU32)(width * sizeof(FxU16)),
		tmp))
	{
		free(tmp);
		mgl_set_error(GL_INVALID_OPERATION);
		return;
	}

	for (row = 0; row < height; ++row)
	{
		const FxU16 *src = tmp + ((height - 1 - row) * width);
		GLubyte *dst = ((GLubyte *)pixels) + (row * width * 3);

		for (col = 0; col < width; ++col)
		{
			FxU16 value = src[col];
			dst[col * 3 + 0] = (GLubyte)(((value >> 11) & 0x1f) << 3);
			dst[col * 3 + 1] = (GLubyte)(((value >> 5) & 0x3f) << 2);
			dst[col * 3 + 2] = (GLubyte)((value & 0x1f) << 3);
		}
	}

	free(tmp);
}

void APIENTRY glRotatef(GLfloat angle, GLfloat x, GLfloat y, GLfloat z)
{
	mgl_mat_rotate(mgl_current_matrix(), angle, x, y, z);
}

void APIENTRY glScalef(GLfloat x, GLfloat y, GLfloat z)
{
	mgl_mat_scale(mgl_current_matrix(), x, y, z);
}

void APIENTRY glScissor(GLint x, GLint y, GLsizei width, GLsizei height)
{
	mgl.scissor[0] = x;
	mgl.scissor[1] = y;
	mgl.scissor[2] = width;
	mgl.scissor[3] = height;
}

void APIENTRY glShadeModel(GLenum mode)
{
	mgl.shade_model = mode;
}

void APIENTRY glTexCoord2f(GLfloat s, GLfloat t)
{
	mgl.current_texcoord[0] = s;
	mgl.current_texcoord[1] = t;
}

/* GL_SGIS_multitexture extension functions */
void APIENTRY glSelectTextureSGIS(GLenum target)
{
	if (target == GL_TEXTURE1_SGIS)
		mgl.active_texture_unit = 1;
	else
		mgl.active_texture_unit = 0;
}

void APIENTRY glMTexCoord2fSGIS(GLenum target, GLfloat s, GLfloat t)
{
	if (target == GL_TEXTURE1_SGIS)
	{
		mgl.current_texcoord2[0] = s;
		mgl.current_texcoord2[1] = t;
	}
	else
	{
		mgl.current_texcoord[0] = s;
		mgl.current_texcoord[1] = t;
	}
}

void APIENTRY glTexEnvf(GLenum target, GLenum pname, GLfloat param)
{
	if ((target != GL_TEXTURE_ENV) || (pname != GL_TEXTURE_ENV_MODE))
	{
		mgl_set_error(GL_INVALID_ENUM);
		return;
	}

	if (mgl.active_texture_unit == 1)
		mgl.tex_env_mode2 = (GLenum)param;
	else
		mgl.tex_env_mode = (GLenum)param;
}

void APIENTRY glTexImage2D(GLenum target, GLint level, GLint internalformat,
	GLsizei width, GLsizei height, GLint border,
	GLenum format, GLenum type, const GLvoid *pixels)
{
	mgl_texture_t *tex;

	if (target != GL_TEXTURE_2D || border != 0 || type != GL_UNSIGNED_BYTE)
	{
		mgl_set_error(GL_INVALID_ENUM);
		return;
	}

	if (level != 0)
		return;

	tex = mgl_get_texture(mgl.active_texture_unit == 1 ? mgl.bound_texture2 : mgl.bound_texture);
	if (!tex)
		return;

	if (format == GL_RGBA)
	{
		mgl_texture_define_image(tex, internalformat, width, height, pixels);
		return;
	}

	if (((format == GL_COLOR_INDEX) || (format == GL_COLOR_INDEX8_EXT)) &&
		((internalformat == GL_COLOR_INDEX) || (internalformat == GL_COLOR_INDEX8_EXT)))
	{
		mgl_texture_define_paletted_image(tex, internalformat, width, height, pixels);
		return;
	}

	mgl_set_error(GL_INVALID_ENUM);
}

void APIENTRY glTexParameterf(GLenum target, GLenum pname, GLfloat param)
{
	mgl_texture_t *tex = mgl_get_texture(mgl.active_texture_unit == 1 ? mgl.bound_texture2 : mgl.bound_texture);

	if (!tex || target != GL_TEXTURE_2D)
	{
		mgl_set_error(GL_INVALID_ENUM);
		return;
	}

	switch (pname)
	{
	case GL_TEXTURE_MIN_FILTER:
		tex->min_filter = (GLenum)param;
		break;
	case GL_TEXTURE_MAG_FILTER:
		tex->mag_filter = (GLenum)param;
		break;
	case GL_TEXTURE_WRAP_S:
		tex->wrap_s = (GLenum)param;
		break;
	case GL_TEXTURE_WRAP_T:
		tex->wrap_t = (GLenum)param;
		break;
	default:
		mgl_set_error(GL_INVALID_ENUM);
		break;
	}
}

void APIENTRY glTexSubImage2D(GLenum target, GLint level, GLint xoffset, GLint yoffset,
	GLsizei width, GLsizei height, GLenum format, GLenum type, const GLvoid *pixels)
{
	mgl_texture_t *tex;

	if (target != GL_TEXTURE_2D || level != 0 || format != GL_RGBA || type != GL_UNSIGNED_BYTE)
	{
		mgl_set_error(GL_INVALID_ENUM);
		return;
	}

	tex = mgl_get_texture(mgl.active_texture_unit == 1 ? mgl.bound_texture2 : mgl.bound_texture);
	if (!tex)
		return;

	mgl_texture_sub_image(tex, xoffset, yoffset, width, height, pixels);
}

void APIENTRY glTranslatef(GLfloat x, GLfloat y, GLfloat z)
{
	mgl_mat_translate(mgl_current_matrix(), x, y, z);
}

void APIENTRY glVertex2f(GLfloat x, GLfloat y)
{
	mgl_submit_vertex(x, y, 0.0f, 1.0f);
}

void APIENTRY glVertex3f(GLfloat x, GLfloat y, GLfloat z)
{
	mgl_submit_vertex(x, y, z, 1.0f);
}

void APIENTRY glVertex3fv(const GLfloat *v)
{
	mgl_submit_vertex(v[0], v[1], v[2], 1.0f);
}

void APIENTRY glVertexPointer(GLint size, GLenum type, GLsizei stride, const GLvoid *pointer)
{
	mgl.vertex_array.pointer = (const GLfloat *)pointer;
	mgl.vertex_array.size = size;
	mgl.vertex_array.type = type;
	mgl.vertex_array.stride = stride;
}

void APIENTRY glViewport(GLint x, GLint y, GLsizei width, GLsizei height)
{
	mgl.viewport[0] = x;
	mgl.viewport[1] = y;
	mgl.viewport[2] = width;
	mgl.viewport[3] = height;
}
