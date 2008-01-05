/*
 * salut-muc-manager.h - Header for SalutMucManager
 * Copyright (C) 2006 Collabora Ltd.
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

#ifndef __SALUT_MUC_MANAGER_H__
#define __SALUT_MUC_MANAGER_H__

#include <glib-object.h>

#include <gibber/gibber-bytestream-iface.h>
#include <avahi-gobject/ga-client.h>

#include <salut-connection.h>
#include "salut-xmpp-connection-manager.h"
#include "salut-muc-channel.h"

G_BEGIN_DECLS

typedef struct _SalutMucManager SalutMucManager;
typedef struct _SalutMucManagerClass SalutMucManagerClass;

struct _SalutMucManagerClass {
    GObjectClass parent_class;
};

struct _SalutMucManager {
    GObject parent;
};

GType salut_muc_manager_get_type(void);

/* TYPE MACROS */
#define SALUT_TYPE_MUC_MANAGER \
  (salut_muc_manager_get_type())
#define SALUT_MUC_MANAGER(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj), SALUT_TYPE_MUC_MANAGER, SalutMucManager))
#define SALUT_MUC_MANAGER_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass), SALUT_TYPE_MUC_MANAGER, SalutMucManagerClass))
#define SALUT_IS_MUC_MANAGER(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj), SALUT_TYPE_MUC_MANAGER))
#define SALUT_IS_MUC_MANAGER_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass), SALUT_TYPE_MUC_MANAGER))
#define SALUT_MUC_MANAGER_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS ((obj), SALUT_TYPE_MUC_MANAGER, SalutMucManagerClass))

SalutMucManager *
salut_muc_manager_new (SalutConnection *connection,
                       SalutXmppConnectionManager *xmpp_connection_manager);

gboolean
salut_muc_manager_start (SalutMucManager *muc_manager,
    GaClient *client, GError **error);

SalutMucChannel *
salut_muc_manager_get_text_channel (SalutMucManager *muc_manager,
    TpHandle handle);

void salut_muc_manager_handle_si_stream_request (SalutMucManager *muc_manager,
    GibberBytestreamIface *bytestream, TpHandle room_handle,
    const gchar *stream_id, GibberXmppStanza *msg);

G_END_DECLS

#endif /* #ifndef __SALUT_MUC_MANAGER_H__*/
