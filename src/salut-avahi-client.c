/*
 * salut-avahi-client.c - Source for SalutAvahiClient
 * Copyright (C) 2005 Collabora Ltd.
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

#include "salut-avahi-client.h"

#include "salut-avahi-client-enumtypes.h"
#include "salut-avahi-errors.h"

/* FIXME what to do about glib-malloc ? */
#include <avahi-glib/glib-watch.h>
#include <avahi-glib/glib-malloc.h>
#include <avahi-common/error.h>
#include <avahi-common/timeval.h>


G_DEFINE_TYPE(SalutAvahiClient, salut_avahi_client, G_TYPE_OBJECT)

/* signal enum */
enum {
  STATE_CHANGED,
  LAST_SIGNAL
};

static guint signals[LAST_SIGNAL] = {0};

/* properties */
enum {
  PROP_STATE = 1,
  PROP_FLAGS
};

/* private structure */
typedef struct _SalutAvahiClientPrivate SalutAvahiClientPrivate;

struct _SalutAvahiClientPrivate {
  AvahiGLibPoll *poll;
  SalutAvahiClientFlags flags;
  SalutAvahiClientState state;
  gboolean dispose_has_run;
};

#define SALUT_AVAHI_CLIENT_GET_PRIVATE(o)     (G_TYPE_INSTANCE_GET_PRIVATE ((o), SALUT_TYPE_AVAHI_CLIENT, SalutAvahiClientPrivate))

static void
salut_avahi_client_init (SalutAvahiClient *self)
{
  SalutAvahiClientPrivate *priv = SALUT_AVAHI_CLIENT_GET_PRIVATE (self);
  /* allocate any data required by the object here */
  self->avahi_client = NULL;
  priv->state = SALUT_AVAHI_CLIENT_STATE_NOT_STARTED;
  priv->flags = SALUT_AVAHI_CLIENT_FLAG_NO_FLAGS;
}

static void salut_avahi_client_dispose (GObject *object);
static void salut_avahi_client_finalize (GObject *object);

static void
salut_avahi_client_set_property (GObject *object,
                                 guint property_id,
                                 const GValue *value,
                                 GParamSpec *pspec) {
 SalutAvahiClient *client = SALUT_AVAHI_CLIENT(object);
 SalutAvahiClientPrivate *priv = SALUT_AVAHI_CLIENT_GET_PRIVATE(client);

 switch (property_id) {
   case PROP_FLAGS:
     g_assert(client->avahi_client == NULL);
     priv->flags = g_value_get_enum(value);
     break;
   default:
     G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
     break;
 }
}

static void
salut_avahi_client_get_property (GObject *object,
                                 guint property_id,
                                 GValue *value,
                                 GParamSpec *pspec) {
  SalutAvahiClient *client = SALUT_AVAHI_CLIENT(object);
  SalutAvahiClientPrivate *priv = SALUT_AVAHI_CLIENT_GET_PRIVATE(client);

  switch (property_id) {
    case PROP_STATE:
      g_value_set_enum(value, priv->state);
      break;
    case PROP_FLAGS:
      g_value_set_enum(value, priv->flags);
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
  }
}

static void
salut_avahi_client_class_init (SalutAvahiClientClass *salut_avahi_client_class)
{
  GObjectClass *object_class = G_OBJECT_CLASS (salut_avahi_client_class);
  GParamSpec *param_spec;

  g_type_class_add_private (salut_avahi_client_class, sizeof (SalutAvahiClientPrivate));


  object_class->dispose = salut_avahi_client_dispose;
  object_class->finalize = salut_avahi_client_finalize;

  object_class->set_property = salut_avahi_client_set_property;
  object_class->get_property = salut_avahi_client_get_property;

  param_spec = g_param_spec_enum("state", "Client state",
                                 "The state of the salut avahi client",
                                 SALUT_TYPE_AVAHI_CLIENT_STATE,
                                 SALUT_AVAHI_CLIENT_STATE_NOT_STARTED,
                                 G_PARAM_READABLE  |
                                 G_PARAM_STATIC_NAME |
                                 G_PARAM_STATIC_BLURB);
  g_object_class_install_property(object_class, PROP_STATE, param_spec);

  param_spec = g_param_spec_enum("flags", "Client flags",
                                 "The flags the avahi client is started with",
                                 SALUT_TYPE_AVAHI_CLIENT_FLAGS,
                                 SALUT_AVAHI_CLIENT_FLAG_NO_FLAGS,
                                 G_PARAM_READWRITE  |
                                 G_PARAM_CONSTRUCT_ONLY |
                                 G_PARAM_STATIC_NAME |
                                 G_PARAM_STATIC_BLURB);
  g_object_class_install_property(object_class, PROP_FLAGS, param_spec);

  signals[STATE_CHANGED] = 
    g_signal_new("state-changed",
                 G_OBJECT_CLASS_TYPE(salut_avahi_client_class),
                 G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
                 0,
                 NULL, NULL,
                 g_cclosure_marshal_VOID__ENUM,
                 G_TYPE_NONE, 1, SALUT_TYPE_AVAHI_CLIENT_STATE);

}

void
salut_avahi_client_dispose (GObject *object)
{
  SalutAvahiClient *self = SALUT_AVAHI_CLIENT (object);
  SalutAvahiClientPrivate *priv = SALUT_AVAHI_CLIENT_GET_PRIVATE (self);

  if (priv->dispose_has_run)
    return;

  priv->dispose_has_run = TRUE;

  if (self->avahi_client) {
    avahi_client_free(self->avahi_client);
    self->avahi_client = NULL;
  }
  if (priv->poll) {
    avahi_glib_poll_free(priv->poll);
    priv->poll = NULL;
  }

  /* release any references held by the object here */
  if (G_OBJECT_CLASS (salut_avahi_client_parent_class)->dispose)
    G_OBJECT_CLASS (salut_avahi_client_parent_class)->dispose (object);
}

void
salut_avahi_client_finalize (GObject *object)
{

  /* free any data held directly by the object here */
  G_OBJECT_CLASS (salut_avahi_client_parent_class)->finalize (object);
}

SalutAvahiClient *
salut_avahi_client_new(SalutAvahiClientFlags flags) {
  return g_object_new(SALUT_TYPE_AVAHI_CLIENT, 
                      "flags", flags,
                      NULL);
}

static GQuark
detail_for_state(AvahiClientState state) {
  static struct { AvahiClientState state; gchar *name; GQuark quark; } 
    states[]  = { { AVAHI_CLIENT_S_REGISTERING, "registering", 0 },
                  { AVAHI_CLIENT_S_RUNNING,     "running", 0 },
                  { AVAHI_CLIENT_S_COLLISION,   "collistion", 0 },
                  { AVAHI_CLIENT_FAILURE,       "failure", 0 },
                  { AVAHI_CLIENT_CONNECTING,    "connecting", 0 },
                  { 0, NULL, 0 }
    };
  int i;

  for (i = 0; states[i].name != NULL; i++) {
    if (state != states[i].state)
      continue;

    if (!states[i].quark)
      states[i].quark = g_quark_from_static_string(states[i].name);
    printf("Detail: %s\n", states[i].name);
    return states[i].quark;
  }
  g_assert_not_reached();
}

static void
_avahi_client_cb(AvahiClient *c, AvahiClientState state, void *data) {
  SalutAvahiClient *self = SALUT_AVAHI_CLIENT(data);
  SalutAvahiClientPrivate *priv = SALUT_AVAHI_CLIENT_GET_PRIVATE (self);

  printf("CLIENT CB: %d\n", state);

  /* Avahi can call the callback before return from _client_new */
  if (self->avahi_client == NULL)
    self->avahi_client = c;

  g_assert(c == self->avahi_client);
  priv->state = state;
  g_signal_emit(self, signals[STATE_CHANGED], 
                detail_for_state(state),  state);
}

gboolean
salut_avahi_client_start(SalutAvahiClient *client, GError **error) {
  SalutAvahiClientPrivate *priv = SALUT_AVAHI_CLIENT_GET_PRIVATE (client);
  AvahiClient *aclient;
  int aerror;

  g_assert(client->avahi_client == NULL);
  g_assert(priv->poll == NULL);

  avahi_set_allocator (avahi_glib_allocator ());

  priv->poll = avahi_glib_poll_new(NULL, G_PRIORITY_DEFAULT);

  aclient = avahi_client_new(avahi_glib_poll_get(priv->poll),
                            priv->flags, 
                            _avahi_client_cb,
                            client,
                            &aerror);
  if (aclient == NULL) {
    if (error != NULL) {
      *error = g_error_new(SALUT_AVAHI_ERRORS, aerror, 
                           "Failed to create avahi client: %s", 
                           avahi_strerror(aerror));
    }
    return FALSE;
  }
  client->avahi_client = aclient;
  return TRUE;
}
