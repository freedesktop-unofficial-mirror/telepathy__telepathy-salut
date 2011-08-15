/*
 * connection-contact-info.c - ContactInfo implementation
 * Copyright © 2011 Collabora Ltd.
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

#include "connection-contact-info.h"

#include <telepathy-glib/interfaces.h>
/* Slightly sketchy; included for TpContactInfoFieldSpec. */
#include <telepathy-glib/connection.h>
#include <telepathy-glib/gtypes.h>

#include "contact-manager.h"
#include "contact.h"

enum {
    PROP_CONTACT_INFO_FLAGS,
    PROP_SUPPORTED_FIELDS
};

static GPtrArray *
get_supported_fields (void)
{
  static gchar *i_heart_the_internet[] = { "type=internet", NULL };
  static TpContactInfoFieldSpec supported_fields[] = {
      /* We're gonna omit 'fn' because it shows up as the alias. */
      { "n", NULL,
        TP_CONTACT_INFO_FIELD_FLAG_PARAMETERS_EXACT, 1 },
      { "email", i_heart_the_internet,
        TP_CONTACT_INFO_FIELD_FLAG_PARAMETERS_EXACT, 1 },
      /* x-jabber is used for compatibility with Gabble */
      { "x-jabber", NULL,
        TP_CONTACT_INFO_FIELD_FLAG_PARAMETERS_EXACT, 1 },
      /* Heh, we could also include the contact's IP address(es) here. */
      { NULL }
  };
  static gsize supported_fields_ptr_array = 0;

  if (g_once_init_enter (&supported_fields_ptr_array))
    {
      GPtrArray *fields = dbus_g_type_specialized_construct (
          TP_ARRAY_TYPE_FIELD_SPECS);
      TpContactInfoFieldSpec *spec;

      for (spec = supported_fields; spec->name != NULL; spec++)
        g_ptr_array_add (fields,
            tp_value_array_build (4,
                G_TYPE_STRING, spec->name,
                G_TYPE_STRV, spec->parameters,
                G_TYPE_UINT, spec->flags,
                G_TYPE_UINT, spec->max,
                G_TYPE_INVALID));

      g_once_init_leave (&supported_fields_ptr_array, (gsize) fields);
    }

  return (GPtrArray *) supported_fields_ptr_array;
}

static void
salut_conn_contact_info_get_property (
    GObject *object,
    GQuark iface,
    GQuark name,
    GValue *value,
    gpointer getter_data)
{
  switch (GPOINTER_TO_UINT (getter_data))
    {
    case PROP_CONTACT_INFO_FLAGS:
      g_value_set_uint (value, TP_CONTACT_INFO_FLAG_PUSH);
      break;
    case PROP_SUPPORTED_FIELDS:
      g_value_set_boxed (value, get_supported_fields ());
      break;
    default:
      g_assert_not_reached ();
    }
}

void
salut_conn_contact_info_class_init (
    SalutConnectionClass *klass)
{
  static TpDBusPropertiesMixinPropImpl props[] = {
      { "ContactInfoFlags", GUINT_TO_POINTER (PROP_CONTACT_INFO_FLAGS), NULL },
      { "SupportedFields", GUINT_TO_POINTER (PROP_SUPPORTED_FIELDS), NULL },
      { NULL }
  };

  tp_dbus_properties_mixin_implement_interface (
      G_OBJECT_CLASS (klass),
      TP_IFACE_QUARK_CONNECTION_INTERFACE_CONTACT_INFO,
      salut_conn_contact_info_get_property,
      NULL,
      props);
}

static GPtrArray *
build_contact_info (
    const gchar *first,
    const gchar *last,
    const gchar *email,
    const gchar *jid)
{
  GPtrArray *contact_info = dbus_g_type_specialized_construct (
      TP_ARRAY_TYPE_CONTACT_INFO_FIELD_LIST);

  if (first != NULL || last != NULL)
    {
      const gchar *field_value[] = {
          last != NULL ? last : "",
          first != NULL ? first : "",
          "",
          "",
          "",
          NULL
      };

      g_ptr_array_add (contact_info,
          tp_value_array_build (3,
              G_TYPE_STRING, "n",
              G_TYPE_STRV, NULL,
              G_TYPE_STRV, field_value,
              G_TYPE_INVALID));
    }

  return contact_info;
}

static void
salut_conn_contact_info_fill_contact_attributes (
    GObject *obj,
    const GArray *contacts,
    GHashTable *attributes_hash)
{
  guint i;
  SalutConnection *self = SALUT_CONNECTION (obj);
  TpBaseConnection *base = TP_BASE_CONNECTION (self);
  SalutContactManager *contact_manager;

  g_object_get (self, "contact-manager", &contact_manager, NULL);

  for (i = 0; i < contacts->len; i++)
    {
      TpHandle handle = g_array_index (contacts, TpHandle, i);
      GPtrArray *contact_info = NULL;

      if (base->self_handle == handle)
        {
          /* TODO */
        }
      else
        {
          SalutContact *contact = salut_contact_manager_get_contact (
              contact_manager, handle);
          if (contact != NULL)
            {
              contact_info = build_contact_info (contact->first, contact->last,
                  contact->email, contact->jid);
              g_object_unref (contact);
            }
        }

      if (contact_info != NULL)
        tp_contacts_mixin_set_contact_attribute (attributes_hash,
            handle, TP_TOKEN_CONNECTION_INTERFACE_CONTACT_INFO_INFO,
            tp_g_value_slice_new_take_boxed (
                TP_ARRAY_TYPE_CONTACT_INFO_FIELD_LIST, contact_info));
    }

  g_object_unref (contact_manager);
}

void salut_conn_contact_info_init (
    SalutConnection *self)
{
  tp_contacts_mixin_add_contact_attributes_iface (
      G_OBJECT (self),
      TP_IFACE_CONNECTION_INTERFACE_CONTACT_INFO,
      salut_conn_contact_info_fill_contact_attributes);
}

static void
salut_conn_contact_info_refresh_contact_info (
    TpSvcConnectionInterfaceContactInfo *iface,
    const GArray *contacts,
    DBusGMethodInvocation *context)
{
  /* This is a no-op on link-local XMPP: everything's always pushed to us. */
  tp_svc_connection_interface_contact_info_return_from_refresh_contact_info (context);
}

void
salut_conn_contact_info_iface_init (
    gpointer g_iface,
    gpointer iface_data)
{
  TpSvcConnectionInterfaceContactInfoClass *klass = g_iface;

#define IMPLEMENT(x) tp_svc_connection_interface_contact_info_implement_##x \
    (klass, salut_conn_contact_info_##x)
  IMPLEMENT (refresh_contact_info);
#undef IMPLEMENT
}
