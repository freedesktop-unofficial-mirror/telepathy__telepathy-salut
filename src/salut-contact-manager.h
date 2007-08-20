/*
 * salut-contact-manager.h - Header for SalutContactManager
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

#include "config.h"

#ifndef __SALUT_CONTACT_MANAGER_H__
#define __SALUT_CONTACT_MANAGER_H__

#include <glib-object.h>

#include "salut-connection.h"
#include "salut-avahi-client.h"
#include "salut-contact.h"

G_BEGIN_DECLS

typedef struct _SalutContactManager SalutContactManager;
typedef struct _SalutContactManagerClass SalutContactManagerClass;

struct _SalutContactManagerClass {
    GObjectClass parent_class;
};

struct _SalutContactManager {
    GObject parent;
};


GType salut_contact_manager_get_type(void);

/* TYPE MACROS */
#define SALUT_TYPE_CONTACT_MANAGER \
  (salut_contact_manager_get_type())
#define SALUT_CONTACT_MANAGER(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj), SALUT_TYPE_CONTACT_MANAGER, SalutContactManager))
#define SALUT_CONTACT_MANAGER_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass), SALUT_TYPE_CONTACT_MANAGER, SalutContactManagerClass))
#define SALUT_IS_CONTACT_MANAGER(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj), SALUT_TYPE_CONTACT_MANAGER))
#define SALUT_IS_CONTACT_MANAGER_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass), SALUT_TYPE_CONTACT_MANAGER))
#define SALUT_CONTACT_MANAGER_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS ((obj), SALUT_TYPE_CONTACT_MANAGER, SalutContactManagerClass))

SalutContactManager *
salut_contact_manager_new(SalutConnection *connection);

gboolean salut_contact_manager_start(SalutContactManager *mgr,
                                     SalutAvahiClient *client,
                                     GError **error);


SalutContact *
salut_contact_manager_get_contact(SalutContactManager *mgr, TpHandle handle);

GList *
salut_contact_manager_find_contacts_by_address(SalutContactManager *mgr,
                                              struct sockaddr_storage *address);

#ifdef ENABLE_OLPC
gboolean salut_contact_manager_merge_olpc_activity_properties
  (SalutContactManager *self, TpHandle handle, const gchar **color,
   const gchar **name, const gchar **type);

void
salut_contact_manager_add_invited_olpc_activity (SalutContactManager *self,
    SalutContact *invitor, TpHandle room, const gchar *activity_id,
    const gchar *color, const gchar *activity_name,
    const gchar *activity_type);
#endif

#endif /* #ifndef __SALUT_CONTACT_MANAGER_H__*/
