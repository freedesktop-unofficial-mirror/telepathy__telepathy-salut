/*
 * gibber-xmpp-connection.h - Header for GibberXmppConnection
 * Copyright (C) 2006 Collabora Ltd.
 * @author Sjoerd Simons <sjoerd@luon.net>
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

#ifndef __GIBBER_XMPP_CONNECTION_H__
#define __GIBBER_XMPP_CONNECTION_H__

#include <glib-object.h>

#include "gibber-transport.h"
#include "gibber-xmpp-stanza.h"

G_BEGIN_DECLS

typedef struct _GibberXmppConnection GibberXmppConnection;
typedef struct _GibberXmppConnectionClass GibberXmppConnectionClass;

struct _GibberXmppConnectionClass {
    GObjectClass parent_class;
};

struct _GibberXmppConnection {
    GObject parent;
    GibberTransport *transport;
    gboolean stream_open;
};

GType gibber_xmpp_connection_get_type(void);

/* TYPE MACROS */
#define GIBBER_TYPE_XMPP_CONNECTION \
  (gibber_xmpp_connection_get_type())
#define GIBBER_XMPP_CONNECTION(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj), GIBBER_TYPE_XMPP_CONNECTION, GibberXmppConnection))
#define GIBBER_XMPP_CONNECTION_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass), GIBBER_TYPE_XMPP_CONNECTION, GibberXmppConnectionClass))
#define GIBBER_IS_XMPP_CONNECTION(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj), GIBBER_TYPE_XMPP_CONNECTION))
#define GIBBER_IS_XMPP_CONNECTION_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass), GIBBER_TYPE_XMPP_CONNECTION))
#define GIBBER_XMPP_CONNECTION_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS ((obj), GIBBER_TYPE_XMPP_CONNECTION, GibberXmppConnectionClass))



GibberXmppConnection *gibber_xmpp_connection_new(GibberTransport *transport); 
void gibber_xmpp_connection_open(GibberXmppConnection *connection,
                                const gchar *to, const gchar *from,
                                const gchar *version);
void gibber_xmpp_connection_close(GibberXmppConnection *connection);
gboolean gibber_xmpp_connection_send(GibberXmppConnection *connection, 
                                    GibberXmppStanza *stanza, 
                                    GError **error);

G_END_DECLS

#endif /* #ifndef __GIBBER_XMPP_CONNECTION_H__*/
