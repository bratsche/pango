/* Pango
 * pangocairo-fontmap.c: Cairo font handling
 *
 * Copyright (C) 2000-2005 Red Hat Software
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.	 See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

#include "config.h"

#include "pangocairo.h"
#include "pangocairo-private.h"
#include "pango-impl-utils.h"

#include <string.h>

typedef struct _PangoCairoContextInfo       PangoCairoContextInfo;
typedef struct _PangoCairoShapeRendererInfo PangoCairoShapeRendererInfo;

struct _PangoCairoShapeRendererInfo
{
  PangoCairoShapeRendererFunc shape_renderer_func;
  gpointer                    shape_renderer_data;
  GDestroyNotify              shape_renderer_notify;
};

struct _PangoCairoContextInfo
{
  double dpi;

  cairo_font_options_t *set_options;
  cairo_font_options_t *surface_options;
  cairo_font_options_t *merged_options;

  GHashTable *shape_renderers;
};

static void
free_shape_renderer_info (gpointer key,
			  gpointer value,
			  gpointer data)
{
  PangoCairoShapeRendererInfo *info = value;

  if (info->shape_renderer_notify)
    info->shape_renderer_notify (info->shape_renderer_data);

  g_slice_free (PangoCairoShapeRendererInfo, info);
}

static void
free_context_info (PangoCairoContextInfo *info)
{
  if (info->shape_renderers)
    g_hash_table_foreach (info->shape_renderers,
			  free_shape_renderer_info, NULL);

  if (info->set_options)
    cairo_font_options_destroy (info->set_options);
  if (info->surface_options)
    cairo_font_options_destroy (info->surface_options);
  if (info->merged_options)
    cairo_font_options_destroy (info->merged_options);
}

static PangoCairoContextInfo *
get_context_info (PangoContext *context,
		  gboolean      create)
{
  static GQuark context_info_quark;
  PangoCairoContextInfo *info;

  if (G_UNLIKELY (!context_info_quark))
    context_info_quark = g_quark_from_static_string ("pango-cairo-context-info");

  info = g_object_get_qdata (G_OBJECT (context), context_info_quark);

  if (G_UNLIKELY (!info) && create)
    {
      info = g_slice_new0 (PangoCairoContextInfo);
      info->dpi = -1.0;

      g_object_set_qdata_full (G_OBJECT (context), context_info_quark,
			       info, (GDestroyNotify)free_context_info);
    }

  return info;
}

static gboolean
_pango_cairo_update_context (cairo_t      *cr,
			     PangoContext *context)
{
  PangoCairoContextInfo *info;
  cairo_matrix_t cairo_matrix;
  cairo_surface_t *target;
  PangoMatrix pango_matrix;
  const PangoMatrix *current_matrix, identity_matrix = PANGO_MATRIX_INIT;
  const cairo_font_options_t *merged_options;
  cairo_font_options_t *old_merged_options;
  gboolean changed = FALSE;

  info = get_context_info (context, TRUE);

  target = cairo_get_target (cr);

  if (!info->surface_options)
    info->surface_options = cairo_font_options_create ();
  cairo_surface_get_font_options (target, info->surface_options);

  old_merged_options = info->merged_options;
  info->merged_options = NULL;

  merged_options = _pango_cairo_context_get_merged_font_options (context);

  if (old_merged_options)
    {
      if (!cairo_font_options_equal (merged_options, old_merged_options))
	changed = TRUE;
      cairo_font_options_destroy (old_merged_options);
      old_merged_options = NULL;
    }
  else
    changed = TRUE;

  cairo_get_matrix (cr, &cairo_matrix);
  pango_matrix.xx = cairo_matrix.xx;
  pango_matrix.yx = cairo_matrix.yx;
  pango_matrix.xy = cairo_matrix.xy;
  pango_matrix.yy = cairo_matrix.yy;
  pango_matrix.x0 = 0;
  pango_matrix.y0 = 0;

  current_matrix = pango_context_get_matrix (context);
  if (!current_matrix)
    current_matrix = &identity_matrix;

  /* layout is matrix-independent if metrics-hinting is off.
   * also ignore matrix translation offsets */
  if ((cairo_font_options_get_hint_metrics (merged_options) != CAIRO_HINT_METRICS_OFF) &&
      (0 != memcmp (&pango_matrix, current_matrix, sizeof (PangoMatrix))))
    changed = TRUE;

  pango_context_set_matrix (context, &pango_matrix);


  return changed;
}

/**
 * pango_cairo_update_context:
 * @cr: a Cairo context
 * @context: a #PangoContext, from a pangocairo font map
 *
 * Updates a #PangoContext previously created for use with Cairo to
 * match the current transformation and target surface of a Cairo
 * context. If any layouts have been created for the context,
 * it's necessary to call pango_layout_context_changed() on those
 * layouts.
 *
 * Since: 1.10
 **/
void
pango_cairo_update_context (cairo_t      *cr,
			    PangoContext *context)
{
  g_return_if_fail (cr != NULL);
  g_return_if_fail (PANGO_IS_CONTEXT (context));

  (void) _pango_cairo_update_context (cr, context);
}

/**
 * pango_cairo_context_set_resolution:
 * @context: a #PangoContext, from a pangocairo font map
 * @dpi: the resolution in "dots per inch". (Physical inches aren't actually
 *   involved; the terminology is conventional.) A 0 or negative value
 *   means to use the resolution from the font map.
 *
 * Sets the resolution for the context. This is a scale factor between
 * points specified in a #PangoFontDescription and Cairo units. The
 * default value is 96, meaning that a 10 point font will be 13
 * units high. (10 * 96. / 72. = 13.3).
 *
 * Since: 1.10
 **/
void
pango_cairo_context_set_resolution (PangoContext *context,
				    double        dpi)
{
  PangoCairoContextInfo *info = get_context_info (context, TRUE);
  info->dpi = dpi;
}

/**
 * pango_cairo_context_get_resolution:
 * @context: a #PangoContext, from a pangocairo font map
 *
 * Gets the resolution for the context. See pango_cairo_context_set_resolution()
 *
 * Return value: the resolution in "dots per inch". A negative value will
 *  be returned if no resolution has previously been set.
 *
 * Since: 1.10
 **/
double
pango_cairo_context_get_resolution (PangoContext *context)
{
  PangoCairoContextInfo *info = get_context_info (context, FALSE);

  if (info)
    return info->dpi;
  else
    return -1.0;
}

/**
 * pango_cairo_context_set_font_options:
 * @context: a #PangoContext, from a pangocairo font map
 * @options: a #cairo_font_options_t, or %NULL to unset any previously set
 *           options. A copy is made.
 *
 * Sets the font options used when rendering text with this context.
 * These options override any options that pango_cairo_update_context()
 * derives from the target surface.
 *
 * Since: 1.10
 */
void
pango_cairo_context_set_font_options (PangoContext               *context,
				      const cairo_font_options_t *options)
{
  PangoCairoContextInfo *info;

  g_return_if_fail (PANGO_IS_CONTEXT (context));

  info  = get_context_info (context, TRUE);

  if (info->set_options)
    cairo_font_options_destroy (info->set_options);

  if (options)
    info->set_options = cairo_font_options_copy (options);
  else
    info->set_options = NULL;

  if (info->merged_options)
    {
      cairo_font_options_destroy (info->merged_options);
      info->merged_options = NULL;
    }
}

/**
 * pango_cairo_context_get_font_options:
 * @context: a #PangoContext, from a pangocairo font map
 *
 * Retrieves any font rendering options previously set with
 * pango_cairo_font_map_set_font_options(). This function does not report options
 * that are derived from the target surface by pango_cairo_update_context()
 *
 * Return value: the font options previously set on the context, or %NULL
 *   if no options have been set. This value is owned by the context
 *   and must not be modified or freed.
 *
 * Since: 1.10
 **/
G_CONST_RETURN cairo_font_options_t *
pango_cairo_context_get_font_options (PangoContext *context)
{
  PangoCairoContextInfo *info;

  g_return_val_if_fail (PANGO_IS_CONTEXT (context), NULL);

  info = get_context_info (context, FALSE);

  if (info)
    return info->set_options;
  else
    return NULL;
}

/**
 * _pango_cairo_context_merge_font_options:
 * @context: a #PangoContext
 * @options: a #cairo_font_options_t
 *
 * Merge together options from the target surface and explicitly set
 * on the context.
 *
 * Return value: the combined set of font options. This value is owned
 * by the context and must not be modified or freed.
 **/
G_CONST_RETURN cairo_font_options_t *
_pango_cairo_context_get_merged_font_options (PangoContext *context)
{
  PangoCairoContextInfo *info = get_context_info (context, TRUE);

  if (!info->merged_options)
    {
      info->merged_options = cairo_font_options_create ();

      if (info->surface_options)
	cairo_font_options_merge (info->merged_options, info->surface_options);
      if (info->set_options)
	cairo_font_options_merge (info->merged_options, info->set_options);
    }

  return info->merged_options;
}

/**
 * pango_cairo_context_set_shape_renderer:
 * @context: a #PangoContext, from a pangocairo font map
 * @func: Callback function for rendering attributes of type
 * %PANGO_ATTR_SHAPE, or %NULL to disable shape rendering.
 * @data: User data that will be passed to @func.
 * @dnotify: Callback that will be called when the
 *           context is freed to release @data, or %NULL.
 *
 * Sets callback function for context to use for rendering attributes
 * of type %PANGO_ATTR_SHAPE.  See #PangoCairoShapeRendererFunc for
 * details.
 *
 * Since: 1.18
 */
void
pango_cairo_context_set_shape_renderer (PangoContext                *context,
					PangoCairoShapeRendererFunc  func,
					gpointer                     data,
					GDestroyNotify               dnotify)
{
  pango_cairo_context_set_shape_renderer_for_attr_type (context,
							PANGO_ATTR_SHAPE,
							func,
							data,
							dnotify);
}

void
pango_cairo_context_set_shape_renderer_for_attr_type (PangoContext                *context,
						      PangoAttrType                attr_type,
						      PangoCairoShapeRendererFunc  func,
						      gpointer                     data,
						      GDestroyNotify               dnotify)
{
  PangoCairoContextInfo *info;
  PangoCairoShapeRendererInfo *renderer_info;

  g_return_if_fail (PANGO_IS_CONTEXT (context));
  g_return_if_fail (pango_attr_type_is_a (attr_type, PANGO_ATTR_SHAPE));

  info = get_context_info (context, TRUE);

  if (!info->shape_renderers)
    info->shape_renderers = g_hash_table_new (g_int_hash, g_int_equal);

  renderer_info = g_hash_table_lookup (info->shape_renderers, GUINT_TO_POINTER (attr_type));
  if (!renderer_info)
    {
      renderer_info = g_slice_new0 (PangoCairoShapeRendererInfo);
      g_hash_table_insert (info->shape_renderers,
			   GUINT_TO_POINTER (attr_type),
			   renderer_info);
    }

  if (renderer_info->shape_renderer_notify)
    renderer_info->shape_renderer_notify (renderer_info->shape_renderer_data);

  renderer_info->shape_renderer_func   = func;
  renderer_info->shape_renderer_data   = data;
  renderer_info->shape_renderer_notify = dnotify;
}

/**
 * pango_cairo_context_get_shape_renderer:
 * @context: a #PangoContext, from a pangocairo font map
 * @data: Pointer to #gpointer to return user data
 *
 * Sets callback function for context to use for rendering attributes
 * of type %PANGO_ATTR_SHAPE.  See #PangoCairoShapeRendererFunc for
 * details.
 *
 * Retrieves callback function and associated user data for rendering
 * attributes of type %PANGO_ATTR_SHAPE as set by
 * pango_cairo_context_set_shape_renderer(), if any.
 *
 * Return value: the shape rendering callback previously set on the context, or %NULL
 *   if no shape rendering callback have been set.
 *
 * Since: 1.18
 */
PangoCairoShapeRendererFunc
pango_cairo_context_get_shape_renderer (PangoContext *context,
					gpointer     *data)
{
  return pango_cairo_context_get_shape_renderer_for_attr_type (context,
							       PANGO_ATTR_SHAPE,
							       data);
}

PangoCairoShapeRendererFunc
pango_cairo_context_get_shape_renderer_for_attr_type (PangoContext   *context,
						      PangoAttrType   attr_type,
						      gpointer       *data)
{
  PangoCairoContextInfo *info;
  PangoCairoShapeRendererInfo *renderer_info;

  g_return_val_if_fail (PANGO_IS_CONTEXT (context), NULL);
  g_return_val_if_fail (pango_attr_type_is_a (attr_type, PANGO_ATTR_SHAPE), NULL);

  info = get_context_info (context, FALSE);

  if (info)
    {
      renderer_info = g_hash_table_lookup (info->shape_renderers,
					   GUINT_TO_POINTER (attr_type));
      if (renderer_info)
	{
	  if (data)
	    *data = renderer_info->shape_renderer_data;
	  return renderer_info->shape_renderer_func;
	}
    }

  if (data)
    *data = NULL;
  return NULL;
}

/**
 * pango_cairo_create_context:
 * @cr: a Cairo context
 *
 * Creates a context object set up to match the current transformation
 * and target surface of the Cairo context.  This context can then be
 * used to create a layout using pango_layout_new().
 *
 * This function is a convenience function that creates a context using
 * the default font map, then updates it to @cr.  If you just need to
 * create a layout for use with @cr and do not need to access #PangoContext
 * directly, you can use pango_cairo_create_layout() instead.
 *
 * Return value: the newly created #PangoContext. Free with
 *   g_object_unref().
 *
 * Since: 1.22
 **/
PangoContext *
pango_cairo_create_context (cairo_t *cr)
{
  PangoFontMap *fontmap;
  PangoContext *context;

  g_return_val_if_fail (cr != NULL, NULL);

  fontmap = pango_cairo_font_map_get_default ();
  context = pango_font_map_create_context (fontmap);
  pango_cairo_update_context (cr, context);

  return context;
}

/**
 * pango_cairo_create_layout:
 * @cr: a Cairo context
 *
 * Creates a layout object set up to match the current transformation
 * and target surface of the Cairo context.  This layout can then be
 * used for text measurement with functions like
 * pango_layout_get_size() or drawing with functions like
 * pango_cairo_show_layout(). If you change the transformation
 * or target surface for @cr, you need to call pango_cairo_update_layout()
 *
 * This function is the most convenient way to use Cairo with Pango,
 * however it is slightly inefficient since it creates a separate
 * #PangoContext object for each layout. This might matter in an
 * application that was laying out large amounts of text.
 *
 * Return value: the newly created #PangoLayout. Free with
 *   g_object_unref().
 *
 * Since: 1.10
 **/
PangoLayout *
pango_cairo_create_layout  (cairo_t *cr)
{
  PangoContext *context;
  PangoLayout *layout;

  g_return_val_if_fail (cr != NULL, NULL);

  context = pango_cairo_create_context (cr);
  layout = pango_layout_new (context);
  g_object_unref (context);

  return layout;
}

/**
 * pango_cairo_update_layout:
 * @cr: a Cairo context
 * @layout: a #PangoLayout, from pango_cairo_create_layout()
 *
 * Updates the private #PangoContext of a #PangoLayout created with
 * pango_cairo_create_layout() to match the current transformation
 * and target surface of a Cairo context.
 *
 * Since: 1.10
 **/
void
pango_cairo_update_layout (cairo_t     *cr,
			   PangoLayout *layout)
{
  g_return_if_fail (cr != NULL);
  g_return_if_fail (PANGO_IS_LAYOUT (layout));

  if (_pango_cairo_update_context (cr, pango_layout_get_context (layout)))
    pango_layout_context_changed (layout);
}

