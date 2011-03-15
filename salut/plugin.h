/*
 * plugin.h — plugin API for telepathy-salut plugins
 * Copyright © 2009-2011 Collabora Ltd.
 * Copyright © 2009 Nokia Corporation
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 */

#ifndef SALUT_PLUGINS_PLUGIN_H
#define SALUT_PLUGINS_PLUGIN_H

#include <glib-object.h>

#include <telepathy-glib/base-connection-manager.h>
#include <telepathy-glib/base-connection.h>

G_BEGIN_DECLS

#define SALUT_TYPE_PLUGIN (salut_plugin_get_type ())
#define SALUT_PLUGIN(obj) \
    (G_TYPE_CHECK_INSTANCE_CAST ((obj), SALUT_TYPE_PLUGIN, SalutPlugin))
#define SALUT_IS_PLUGIN(obj) \
    (G_TYPE_CHECK_INSTANCE_TYPE ((obj), SALUT_TYPE_PLUGIN))
#define SALUT_PLUGIN_GET_INTERFACE(obj) \
    (G_TYPE_INSTANCE_GET_INTERFACE ((obj), SALUT_TYPE_PLUGIN, \
        SalutPluginInterface))

typedef struct _SalutPlugin SalutPlugin;
typedef struct _SalutPluginInterface SalutPluginInterface;

/* The caller of this function takes ownership of the returned
 * GPtrArray and the channel managers inside the array. This has the
 * same semantics as TpBaseConnectionCreateChannelManagersImpl. */
typedef GPtrArray * (*SalutPluginCreateChannelManagersImpl) (
    SalutPlugin *plugin,
    TpBaseConnection *connection);

typedef void (*SalutPluginInitializeImpl) (
    SalutPlugin *plugin,
    TpBaseConnectionManager *connection_manager);

#define SALUT_PLUGIN_CURRENT_VERSION 1

struct _SalutPluginInterface
{
  GTypeInterface parent;

  /**
   * The version of the SalutPluginInterface struct design. The
   * current version is at %SALUT_PLUGIN_CURRENT_VERSION.
   */
  guint api_version;

  /**
   * An arbitrary human-readable name identifying this plugin.
   */
  const gchar *name;

  /**
   * The plugin's version, conventionally a "."-separated sequence of
   * numbers.
   */
  const gchar *version;

  /**
   * An implementation of salut_plugin_initialize().
   */
  SalutPluginInitializeImpl initialize;

  /**
   * An implementation of salut_plugin_create_channel_managers().
   */
  SalutPluginCreateChannelManagersImpl create_channel_managers;

  GCallback _padding[7];
};

GType salut_plugin_get_type (void);

const gchar * salut_plugin_get_name (
    SalutPlugin *plugin);
const gchar * salut_plugin_get_version (
    SalutPlugin *plugin);

void salut_plugin_initialize (
    SalutPlugin *plugin,
    TpBaseConnectionManager *connection_manager);

GPtrArray * salut_plugin_create_channel_managers (
    SalutPlugin *plugin,
    TpBaseConnection *connection);

/**
 * salut_plugin_create:
 *
 * Prototype for the plugin entry point.
 *
 * Returns: a new instance of this plugin, which must not be %NULL.
 */
SalutPlugin * salut_plugin_create (void);

typedef SalutPlugin * (*SalutPluginCreateImpl) (void);

G_END_DECLS

#endif
