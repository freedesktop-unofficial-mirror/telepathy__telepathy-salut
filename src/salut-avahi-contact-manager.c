/*
 * salut-avahi-contact-manager.c - Source for SalutAvahiContactManager
 * Copyright (C) 2008 Collabora Ltd.
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

#include <dbus/dbus-glib.h>
#include <stdio.h>
#include <stdlib.h>
#include <arpa/inet.h>

#include <string.h>

#include <avahi-gobject/ga-service-browser.h>
#include <avahi-gobject/ga-enums.h>

#define DEBUG_FLAG DEBUG_MUC
#include "debug.h"

#include "salut-avahi-contact-manager.h"
#include "salut-avahi-contact.h"

G_DEFINE_TYPE (SalutAvahiContactManager, salut_avahi_contact_manager,
    SALUT_TYPE_CONTACT_MANAGER);

/* properties */
enum
{
  PROP_DISCOVERY_CLIENT = 1,
  LAST_PROPERTY
};

/* private structure */
typedef struct _SalutAvahiContactManagerPrivate SalutAvahiContactManagerPrivate;

struct _SalutAvahiContactManagerPrivate
{
  SalutAvahiDiscoveryClient *discovery_client;
  GaServiceBrowser *presence_browser;

  gboolean dispose_has_run;
};

#define SALUT_AVAHI_CONTACT_MANAGER_GET_PRIVATE(obj) \
    ((SalutAvahiContactManagerPrivate *) (SalutAvahiContactManager *)obj->priv)


static void
salut_avahi_contact_manager_get_property (GObject *object,
                                          guint property_id,
                                          GValue *value,
                                          GParamSpec *pspec)
{
  SalutAvahiContactManager *chan = SALUT_AVAHI_CONTACT_MANAGER (object);
  SalutAvahiContactManagerPrivate *priv =
    SALUT_AVAHI_CONTACT_MANAGER_GET_PRIVATE (chan);

  switch (property_id) {
    case PROP_DISCOVERY_CLIENT:
      g_value_set_object (value, priv->discovery_client);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
  }
}

static void
salut_avahi_contact_manager_set_property (GObject *object,
                                          guint property_id,
                                          const GValue *value,
                                          GParamSpec *pspec)
{
  SalutAvahiContactManager *chan = SALUT_AVAHI_CONTACT_MANAGER (object);
  SalutAvahiContactManagerPrivate *priv =
    SALUT_AVAHI_CONTACT_MANAGER_GET_PRIVATE (chan);

  switch (property_id) {
    case PROP_DISCOVERY_CLIENT:
      priv->discovery_client = g_value_get_object (value);
      g_object_ref (priv->discovery_client);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
  }
}

static GObject *
salut_avahi_contact_manager_constructor (GType type,
                                         guint n_props,
                                         GObjectConstructParam *props)
{
  GObject *obj;
  SalutAvahiContactManager *self;
  SalutAvahiContactManagerPrivate *priv;

  obj = G_OBJECT_CLASS (salut_avahi_contact_manager_parent_class)->
    constructor (type, n_props, props);

  self = SALUT_AVAHI_CONTACT_MANAGER (obj);
  priv = SALUT_AVAHI_CONTACT_MANAGER_GET_PRIVATE (self);

  priv->presence_browser = ga_service_browser_new (SALUT_DNSSD_PRESENCE);

  return obj;
}

static void
salut_avahi_contact_manager_init (SalutAvahiContactManager *self)
{
  SalutAvahiContactManagerPrivate *priv = G_TYPE_INSTANCE_GET_PRIVATE (self,
      SALUT_TYPE_AVAHI_CONTACT_MANAGER, SalutAvahiContactManagerPrivate);

  self->priv = priv;

  priv->discovery_client = NULL;
}

static SalutContact *
salut_avahi_contact_manager_create_contact (SalutContactManager *mgr,
                                            const gchar *name)
{
  SalutAvahiContactManager *self = SALUT_AVAHI_CONTACT_MANAGER (mgr);
  SalutAvahiContactManagerPrivate *priv =
    SALUT_AVAHI_CONTACT_MANAGER_GET_PRIVATE (self);

  return SALUT_CONTACT (salut_avahi_contact_new (mgr->connection,
      name, priv->discovery_client));
}

static void
browser_found (GaServiceBrowser *browser,
               AvahiIfIndex interface,
               AvahiProtocol protocol,
               const char *name,
               const char *type,
               const char *domain,
               GaLookupResultFlags flags,
               SalutAvahiContactManager *self)
{
  SalutContactManager *mgr = SALUT_CONTACT_MANAGER (self);
  SalutContact *contact;
  const char *contact_name = name;

  if (flags & AVAHI_LOOKUP_RESULT_OUR_OWN)
    return;

  /* FIXME: For now we assume name is unique on the lan */
  contact = g_hash_table_lookup (mgr->contacts, contact_name);
  if (contact == NULL)
    {
      contact = salut_avahi_contact_manager_create_contact (mgr, contact_name);
      salut_contact_manager_contact_created (mgr, contact);
    }
  else if (!salut_avahi_contact_has_services (SALUT_AVAHI_CONTACT (contact)))
    {
      /* We keep a ref on the contact as long it has services */
     g_object_ref (contact);
    }

  salut_avahi_contact_add_service (SALUT_AVAHI_CONTACT (contact), interface,
      protocol, name, type, domain);
}

static void
browser_removed (GaServiceBrowser *browser,
                 AvahiIfIndex interface,
                 AvahiProtocol protocol,
                 const char *name,
                 const char *type,
                 const char *domain,
                 GaLookupResultFlags flags,
                 SalutAvahiContactManager *self)
{
  SalutContactManager *mgr = SALUT_CONTACT_MANAGER (self);
  SalutContact *contact;
  const char *contact_name = name;

  DEBUG("Browser removed for %s", name);

  contact = g_hash_table_lookup (mgr->contacts, contact_name);
  if (contact != NULL)
    {
      salut_avahi_contact_remove_service (SALUT_AVAHI_CONTACT (contact),
          interface, protocol, name, type, domain);
    }
  else
    {
      DEBUG ("Unknown contact removed from service browser");
    }
}

static void
browser_failed (GaServiceBrowser *browser,
                GError *error,
                SalutAvahiContactManager *self)
{
  /* FIXME proper error handling */
  g_warning("browser failed -> %s", error->message);
}

static gboolean
salut_avahi_contact_manager_start (SalutContactManager *mgr,
                                   GError **error)
{
  SalutAvahiContactManager *self = SALUT_AVAHI_CONTACT_MANAGER (mgr);
  SalutAvahiContactManagerPrivate *priv =
    SALUT_AVAHI_CONTACT_MANAGER_GET_PRIVATE (self);

  g_signal_connect (priv->presence_browser, "new-service",
      G_CALLBACK (browser_found), mgr);
  g_signal_connect(priv->presence_browser, "removed-service",
      G_CALLBACK (browser_removed), mgr);
  g_signal_connect (priv->presence_browser, "failure",
      G_CALLBACK (browser_failed), mgr);

  if (!ga_service_browser_attach(priv->presence_browser,
        priv->discovery_client->avahi_client, error))
    {
      DEBUG ("browser attach failed");
      return FALSE;
    }

  return TRUE;
}

static void
salut_avahi_contact_manager_dispose_contact (SalutContactManager *mgr,
                                             SalutContact *contact)
{
  if (salut_avahi_contact_has_services (SALUT_AVAHI_CONTACT (contact)))
    {
      /* We reffed this contact as it has services */
      g_object_unref (contact);
    }
}

static void salut_avahi_contact_manager_dispose (GObject *object);

static void
salut_avahi_contact_manager_class_init (
    SalutAvahiContactManagerClass *salut_avahi_contact_manager_class) {
  GObjectClass *object_class = G_OBJECT_CLASS (salut_avahi_contact_manager_class);
  SalutContactManagerClass *contact_manager_class = SALUT_CONTACT_MANAGER_CLASS (
      salut_avahi_contact_manager_class);
  GParamSpec *param_spec;

  g_type_class_add_private (salut_avahi_contact_manager_class,
      sizeof (SalutAvahiContactManagerPrivate));

  object_class->dispose = salut_avahi_contact_manager_dispose;

  object_class->constructor = salut_avahi_contact_manager_constructor;
  object_class->get_property = salut_avahi_contact_manager_get_property;
  object_class->set_property = salut_avahi_contact_manager_set_property;

  contact_manager_class->start = salut_avahi_contact_manager_start;
  contact_manager_class->create_contact =
    salut_avahi_contact_manager_create_contact;
  contact_manager_class->dispose_contact =
    salut_avahi_contact_manager_dispose_contact;

  param_spec = g_param_spec_object (
      "discovery-client",
      "SalutAvahiDiscoveryClient object",
      "The Salut Avahi Discovery client associated with this muc channel",
      SALUT_TYPE_AVAHI_DISCOVERY_CLIENT,
      G_PARAM_CONSTRUCT_ONLY |
      G_PARAM_READWRITE |
      G_PARAM_STATIC_NAME |
      G_PARAM_STATIC_NICK |
      G_PARAM_STATIC_BLURB);
  g_object_class_install_property (object_class, PROP_DISCOVERY_CLIENT,
      param_spec);
}

void
salut_avahi_contact_manager_dispose (GObject *object)
{
  SalutAvahiContactManager *self = SALUT_AVAHI_CONTACT_MANAGER (object);
  SalutAvahiContactManagerPrivate *priv =
    SALUT_AVAHI_CONTACT_MANAGER_GET_PRIVATE (self);

  if (priv->dispose_has_run)
    return;

  priv->dispose_has_run = TRUE;

  if (priv->presence_browser != NULL)
    {
      g_object_unref (priv->presence_browser);
      priv->presence_browser = NULL;
    }

  if (priv->discovery_client != NULL)
    {
      g_object_unref (priv->discovery_client);
      priv->discovery_client = NULL;
    }

  if (G_OBJECT_CLASS (salut_avahi_contact_manager_parent_class)->dispose)
    G_OBJECT_CLASS (salut_avahi_contact_manager_parent_class)->dispose (object);
}

SalutAvahiContactManager *
salut_avahi_contact_manager_new (SalutConnection *connection,
                                 SalutAvahiDiscoveryClient *discovery_client)
{
  return g_object_new (SALUT_TYPE_AVAHI_CONTACT_MANAGER,
      "connection", connection,
      "discovery-client", discovery_client,
      NULL);
}