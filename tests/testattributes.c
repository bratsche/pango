/* Pango
 * testattributes.c: Test program for Pango extended attributes
 *
 * Copyright (C) 2008 Cody Russell
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

#include <glib.h>
#include <pango/pango.h>


static void
test_derived ()
{
  PangoAttrType german = pango_attr_type_register_full ("German",
							PANGO_ATTR_LANGUAGE);
  PangoAttrType swiss_german = pango_attr_type_register_full ("SwissGerman",
							      german);
  PangoAttrType french = pango_attr_type_register_full ("French",
							PANGO_ATTR_LANGUAGE);

  g_assert (pango_attr_type_is_a (german, german));
  g_assert (pango_attr_type_is_a (german, PANGO_ATTR_LANGUAGE));
  g_assert (pango_attr_type_is_a (swiss_german, PANGO_ATTR_LANGUAGE));
  g_assert (pango_attr_type_is_a (swiss_german, german));
  g_assert (!pango_attr_type_is_a (french, german));
}


int
main (int argc, char *argv[])
{
  g_test_init (&argc, &argv, NULL);

  g_test_add_func ("/attributes/test-derived", test_derived);

  return g_test_run ();
}
