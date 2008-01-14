/*
 * gibber-resolver.c - Source for GibberResolver
 * Copyright (C) 2008 Collabora Ltd.
 * @author Sjoerd Simons <sjoerd.simons@collabora.co.uk>
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


#include <stdio.h>
#include <stdlib.h>

#include <string.h>
#include <netinet/in.h>
#include <arpa/nameser.h>
#include <resolv.h>

#include <errno.h>

#include "gibber-resolver.h"

G_DEFINE_TYPE(GibberResolver, gibber_resolver, G_TYPE_OBJECT)

typedef struct {
  guint jobid;
  GibberResolver *resolver;

  /* Data the user would like us to remember */
  GCallback callback;
  GDestroyNotify destroy;
  gpointer user_data;
  GObject *weak_object;

  /* Field settable by implementations of GibberResolver */
  gpointer data;
} GibberResolverJob;

/* private structure */
typedef struct _GibberResolverPrivate GibberResolverPrivate;

struct _GibberResolverPrivate
{
  gboolean dispose_has_run;
  /* guint * -> GibberResolverJob struct */
  GHashTable *jobs;
};


static gboolean resolver_resolv_srv (GibberResolver *resolver, guint id,
  const gchar *service_name, const char *service,
  GibberResolverServiceType type);

static gboolean resolver_resolv_addrinfo (GibberResolver *resolver, guint id,
  const gchar *hostname, const char *port, int address_family, int sock_type,
  int protocol, int flags);

static gboolean resolver_resolv_nameinfo (GibberResolver *resolver, guint id,
  const struct sockaddr *sa, socklen_t salen, gint flags);

static void resolver_resolv_cancel (GibberResolver *resolver, guint id);

static void free_job (gpointer data);

#define GIBBER_RESOLVER_GET_PRIVATE(o)  \
    (G_TYPE_INSTANCE_GET_PRIVATE ((o), GIBBER_TYPE_RESOLVER, \
      GibberResolverPrivate))

GQuark
gibber_resolver_error_quark (void)
{
  static GQuark quark = 0;

  if (!quark)
    quark = g_quark_from_static_string ("gibber_resolver_error");

  return quark;
}

static void
gibber_resolver_init (GibberResolver *obj)
{
  GibberResolverPrivate *priv = GIBBER_RESOLVER_GET_PRIVATE (obj);

  /* allocate any data required by the object here */
  priv->jobs = g_hash_table_new_full (g_int_hash, g_int_equal,
      NULL, free_job);
}

static void gibber_resolver_dispose (GObject *object);
static void gibber_resolver_finalize (GObject *object);

static void
gibber_resolver_class_init (GibberResolverClass *gibber_resolver_class)
{
  GObjectClass *object_class = G_OBJECT_CLASS (gibber_resolver_class);

  g_type_class_add_private (gibber_resolver_class,
      sizeof (GibberResolverPrivate));

  object_class->dispose = gibber_resolver_dispose;
  object_class->finalize = gibber_resolver_finalize;

  gibber_resolver_class->resolv_srv = resolver_resolv_srv;
  gibber_resolver_class->resolv_addrinfo = resolver_resolv_addrinfo;
  gibber_resolver_class->resolv_nameinfo = resolver_resolv_nameinfo;
  gibber_resolver_class->resolv_cancel = resolver_resolv_cancel;
}

void
gibber_resolver_dispose (GObject *object)
{
  GibberResolver *self = GIBBER_RESOLVER (object);
  GibberResolverPrivate *priv = GIBBER_RESOLVER_GET_PRIVATE (self);

  if (priv->dispose_has_run)
    return;

  priv->dispose_has_run = TRUE;

  if (priv->jobs != NULL)
    g_hash_table_destroy (priv->jobs);
  priv->jobs = NULL;

  /* release any references held by the object here */
  if (G_OBJECT_CLASS (gibber_resolver_parent_class)->dispose)
    G_OBJECT_CLASS (gibber_resolver_parent_class)->dispose (object);
}

void
gibber_resolver_finalize (GObject *object)
{

  /* free any data held directly by the object here */

  G_OBJECT_CLASS (gibber_resolver_parent_class)->finalize (object);
}

static void
weak_object_destroyed (gpointer data, GObject *old_object)
{
  GibberResolverJob *job = (GibberResolverJob *)data;

  g_assert (job->weak_object == old_object);

  job->weak_object = NULL;

  gibber_resolver_cancel (job->resolver, job->jobid);
}

static guint
gibber_resolver_job_add (GibberResolver *resolver, GCallback callback,
   gpointer user_data, GDestroyNotify destroy, GObject *weak_object)
{
  GibberResolverPrivate *priv = GIBBER_RESOLVER_GET_PRIVATE (resolver);
  GibberResolverJob *job;

  job = g_slice_new0 (GibberResolverJob);
  job->resolver = g_object_ref (resolver);

  job->callback = callback;
  job->destroy = destroy;
  job->user_data = user_data;
  job->weak_object = weak_object;

  /* Now decide on a decent job id.. The pointer is a pretty good initial
   * guess. A nicer solution would be to use an intset */
  job->jobid = GPOINTER_TO_UINT (job);

  /* Be carefull to skip 0 */
  while (job->jobid == 0 ||
      g_hash_table_lookup (priv->jobs, &(job->jobid)) != NULL)
    job->jobid++;

  g_hash_table_insert (priv->jobs, &(job->jobid), job);

  if (weak_object != NULL)
    {
      g_object_weak_ref (weak_object, weak_object_destroyed, job);
    }

  return job->jobid;
}

static void free_job (gpointer data)
{
  GibberResolverJob *job = (GibberResolverJob *)data;

  if (job->destroy)
    job->destroy (job->user_data);

  if (job->weak_object)
    g_object_weak_unref (job->weak_object, weak_object_destroyed, job);

  g_object_unref (job->resolver);
  g_slice_free (GibberResolverJob, job);
}

GibberResolverAddrInfo *
gibber_resolver_addrinfo_new (gint address_family, gint socket_type,
  gint protocol, struct sockaddr *addr, gsize sockaddr_len)
{
  GibberResolverAddrInfo *result;

  result = g_slice_new (GibberResolverAddrInfo);

  result->address_family = address_family;
  result->socket_type = socket_type;
  result->protocol = protocol;
  memcpy (&(result->sockaddr), addr, sockaddr_len);
  result->sockaddr_len = sockaddr_len;

  return result;
}

void
gibber_resolver_addrinfo_free (GibberResolverAddrInfo *addrinfo)
{
  g_slice_free (GibberResolverAddrInfo, addrinfo);
}

void
gibber_resolver_addrinfo_list_free (GList *addrinfo_list)
{
  GList *t;
  GibberResolverAddrInfo *a;

  for (t = addrinfo_list ; t != NULL; t = g_list_delete_link (t, t))
    {
      a = (GibberResolverAddrInfo *)t->data;
      gibber_resolver_addrinfo_free (a);
    }
}

GibberResolverSrvRecord *
gibber_resolver_srv_record_new (gchar *hostname, guint16 port,
  guint16 priority, guint16 weight)
{
  GibberResolverSrvRecord *result;

  result = g_slice_new (GibberResolverSrvRecord);
  result->hostname = g_strdup (hostname);
  result->port = port;
  result->priority = priority;
  result->weight = weight;

  return result;
}

void
gibber_resolver_srv_free (GibberResolverSrvRecord *srvrecord)
{
   g_free (srvrecord->hostname);
   g_slice_free (GibberResolverSrvRecord, srvrecord);
}

void
gibber_resolver_srv_list_free (GList *srv_list)
{
  GList *t;
  GibberResolverSrvRecord *s;

  for (t = srv_list ; t != NULL; t = g_list_delete_link (t, t))
    {
      s = (GibberResolverSrvRecord *) t->data;
      gibber_resolver_srv_free (s);
    }
}


guint
gibber_resolver_srv (GibberResolver *resolver,
  const gchar *service_name, const char *service,
  GibberResolverServiceType type,
  gibber_resolver_srv_cb callback,
  gpointer user_data, GDestroyNotify destroy, GObject *weak_object)
{
  GibberResolverClass *cls = GIBBER_RESOLVER_GET_CLASS (resolver);
  gboolean ret;
  guint jobid;

  jobid = gibber_resolver_job_add (resolver, G_CALLBACK (callback), user_data,
    destroy, weak_object);

  ret = cls->resolv_srv (resolver, jobid, service_name, service, type);

  return ret ? jobid : 0;
}

guint
gibber_resolver_addrinfo (GibberResolver *resolver,
  const gchar *hostname, const char *port,
  int address_family, int sock_type, int protocol, int flags,
  gibber_resolver_addrinfo_cb callback,
  gpointer user_data, GDestroyNotify destroy, GObject *weak_object)
{
  GibberResolverClass *cls = GIBBER_RESOLVER_GET_CLASS (resolver);
  gboolean ret;
  guint jobid;

  jobid = gibber_resolver_job_add (resolver, G_CALLBACK (callback),
    user_data, destroy, weak_object);

  ret = cls->resolv_addrinfo (resolver, jobid, hostname, port, address_family,
    sock_type, protocol, flags);

  return ret ? jobid : 0;
}

guint
gibber_resolver_nameinfo (GibberResolver *resolver, const struct sockaddr *sa,
  socklen_t salen, gint flags, gibber_resolver_nameinfo_cb callback,
  gpointer user_data, GDestroyNotify destroy, GObject *weak_object)
{
  GibberResolverClass *cls = GIBBER_RESOLVER_GET_CLASS (resolver);
  gboolean ret;
  guint jobid;

  jobid = gibber_resolver_job_add (resolver, G_CALLBACK (callback), user_data,
    destroy, weak_object);

  ret = cls->resolv_nameinfo (resolver, jobid, sa, salen, flags);

  return ret ? jobid : 0;
}

void
gibber_resolver_cancel (GibberResolver *resolver, guint id)
{
  GibberResolverClass *cls = GIBBER_RESOLVER_GET_CLASS (resolver);
  GibberResolverPrivate *priv = GIBBER_RESOLVER_GET_PRIVATE (resolver);

  if (g_hash_table_lookup (priv->jobs, &id) == NULL)
    {
      g_warning ("Trying to cancel a non-existing resolver jobs");
      return;
    }

  cls->resolv_cancel (resolver, id);
  g_hash_table_remove (priv->jobs, &id);
}

gboolean
gibber_resolver_sockaddr_to_str (const struct sockaddr *sa, gsize salen,
  gchar **address, gchar **service, GError **error)
{
  int ret;
  gchar name[NI_MAXHOST], servicename[NI_MAXSERV];

  ret = getnameinfo (sa, salen, name, NI_MAXHOST, servicename, NI_MAXSERV,
    NI_NUMERICHOST | NI_NUMERICSERV);

  if (ret != 0)
    {
      g_set_error (error, GIBBER_RESOLVER_ERROR, ret,
        "getnameinfo failed: %s", gai_strerror (ret));
      return FALSE;
    }

  if (address != NULL)
    *address = g_strdup(name);

  if (service != NULL)
    *service = g_strdup (servicename);

  return TRUE;
}

/* Utility function for classed implementing GibberResolver */
void
gibber_resolver_set_data (GibberResolver *resolver, guint id, gpointer data)
{
  GibberResolverPrivate *priv = GIBBER_RESOLVER_GET_PRIVATE (resolver);
  GibberResolverJob *job;

  job = g_hash_table_lookup (priv->jobs, &id);

  g_assert (job != NULL);

  job->data = data;
}

gpointer
gibber_resolver_get_data (GibberResolver *resolver, guint id)
{
  GibberResolverPrivate *priv = GIBBER_RESOLVER_GET_PRIVATE (resolver);
  GibberResolverJob *job;

  job = g_hash_table_lookup (priv->jobs, &id);

  g_assert (job != NULL);

  return job->data;
}

static gint
compare_srv_record (gconstpointer a, gconstpointer b)
{
  GibberResolverSrvRecord *asrv = (GibberResolverSrvRecord *) a;
  GibberResolverSrvRecord *bsrv = (GibberResolverSrvRecord *) b;

  if (asrv->priority != bsrv->priority)
    return asrv->priority < bsrv->priority ? -1 : 1;

  if (asrv->weight != 0 || bsrv->weight != 0)
    return asrv->weight == 0 ?  -1 : 1;

  return 0;
}


static GList *
weight_sort_srv_list_total (GList *srv_list, gint total)
{
  GList *l, *s;
  gint num;
  GibberResolverSrvRecord *srv;

  if (srv_list == NULL)
    return NULL;

  num = g_random_int_range (0, total + 1);

  for (l = srv_list ; l != NULL; l = l->next)
    {
      srv = (GibberResolverSrvRecord *)l->data;
      num -= srv->weight;
      if (num <= 0)
        break;
    }

  g_assert (l != NULL);

  s = g_list_remove_link (srv_list, l);

  return g_list_concat (l,
    weight_sort_srv_list_total (s, total - srv->weight));
}

static GList *
weight_sort_srv_list (GList *srv_list)
{
  GList *l;
  gint total = 0;
  GibberResolverSrvRecord *srv;

  /* Sort srv list of equal priority but with weight as specified in RFC2782 */
  srv = (GibberResolverSrvRecord *)srv_list->data;

  g_assert (srv_list != NULL);

  for (l = srv_list; l != NULL; l = l->next)
    {
      srv = (GibberResolverSrvRecord *)l->data;
      total += srv->weight;
    }

  return weight_sort_srv_list_total (srv_list, total);
}

static void
cut_list(GList *link)
{
  if (link->prev != NULL)
    link->prev->next = NULL;
  link->prev = NULL;
}

static GList *
sort_srv_list (GList *srv_list)
{
  GList *result = NULL;
  GList *start, *end;
  GList *sorted;
  guint16 priority = 0;

  sorted = g_list_sort (srv_list, compare_srv_record);

  while (sorted != NULL)
    {
      end = NULL;

      /* Find the start entry with a non-zero weight */
      for (start = sorted ; start != NULL &&
        ((GibberResolverSrvRecord *)start->data)->weight == 0;
        start = start->next)
        /* nothing */;

      if (start != sorted)
        result = g_list_concat (result, sorted);

      if (start != NULL)
        {
          cut_list (start);
          priority = ((GibberResolverSrvRecord *)start->data)->priority;
        }

      for (end = start ; end != NULL &&
        ((GibberResolverSrvRecord *)end->data)->priority == priority;
        end = end->next)
        /* nothing */;

      if (end != NULL)
        cut_list (end);

      sorted = end;

      if (start != NULL)
        {
        /* We know have a sublist of entries with the same priority but
         * different weights */
          start = weight_sort_srv_list (start);
          result = g_list_concat (result, start);
        }
    }

  return result;
}

void
gibber_resolver_srv_result (GibberResolver *resolver, guint jobid,
  GList *srv_list, GError *error)
{
  GibberResolverPrivate *priv = GIBBER_RESOLVER_GET_PRIVATE (resolver);
  GibberResolverJob *job;
  gibber_resolver_srv_cb callback;

  job = g_hash_table_lookup (priv->jobs, &jobid);

  g_assert (job != NULL);

  srv_list = sort_srv_list (srv_list);

  callback = (gibber_resolver_srv_cb) job->callback;
  callback (resolver, srv_list, error, job->user_data, job->weak_object);

  g_hash_table_remove (priv->jobs, &jobid);
}

void
gibber_resolver_addrinfo_result (GibberResolver *resolver, guint jobid,
  GList *entries, GError *error)
{
  GibberResolverPrivate *priv = GIBBER_RESOLVER_GET_PRIVATE (resolver);
  GibberResolverJob *job;
  gibber_resolver_addrinfo_cb callback;

  job = g_hash_table_lookup (priv->jobs, &jobid);

  g_assert (job != NULL);

  callback = (gibber_resolver_addrinfo_cb)job->callback;
  callback (resolver, entries, error, job->user_data, job->weak_object);

  g_hash_table_remove (priv->jobs, &jobid);
}

void
gibber_resolver_nameinfo_result (GibberResolver *resolver, guint jobid,
  const gchar *hostname, const gchar *port, GError *error)
{
  GibberResolverPrivate *priv = GIBBER_RESOLVER_GET_PRIVATE (resolver);
  GibberResolverJob *job;
  gibber_resolver_nameinfo_cb callback;

  job = g_hash_table_lookup (priv->jobs, &jobid);

  g_assert (job != NULL);

  callback = (gibber_resolver_nameinfo_cb)job->callback;
  callback (resolver, hostname, port, error,
    job->user_data, job->weak_object);

  g_hash_table_remove (priv->jobs, &jobid);
}


#define ANSWER_BUFSIZE 10240
static GList *
res_query_to_list (guchar *answer, int length)
{
  GList *list = NULL;
  int qdcount;
  int ancount;
  int len;
  const unsigned char *pos = answer + sizeof(HEADER);
  unsigned char *end = answer + length;
  HEADER *head = (HEADER *)answer;
  char name[256];

  qdcount = ntohs(head->qdcount);
  ancount = ntohs(head->ancount);

  /* Ignore the questions */
  while (qdcount-- > 0 && (len = dn_expand(answer, end, pos, name, 255)) >= 0)
    {
       pos += len + QFIXEDSZ;
     }

   /* Parse the answers */
   while (ancount-- > 0 && (len = dn_expand(answer, end, pos, name, 255)) >= 0)
     {
       uint16_t pref, weight, port, class, type;

       /* Ignore the initial string, which has the query in it */
       pos += len;
       GETSHORT (type, pos);
       GETSHORT (class, pos);

       if (type != T_SRV || class != C_IN)
         goto failed;

       /* skip ttl and dlen */
       pos += 6;

       GETSHORT(pref, pos);
       GETSHORT(weight, pos);
       GETSHORT(port, pos);
       len = dn_expand(answer, end, pos, name, 255);

       list = g_list_prepend (list,
         gibber_resolver_srv_record_new (name, port, pref, weight));

       pos += len;
    }

  return list;

failed:
 gibber_resolver_srv_list_free (list);
 return NULL;
}


static gboolean
resolver_resolv_srv (GibberResolver *resolver, guint id,
  const gchar *service_name, const char *service,
  GibberResolverServiceType type)
{
  gchar *srv_str;
  int ret;
  guchar answer[ANSWER_BUFSIZE];

  srv_str = g_strdup_printf ("_%s._%s.%s", service,
    type == GIBBER_RESOLVER_SERVICE_TYPE_TCP ? "tcp" : "udp", service_name);

  ret = res_query (srv_str, C_IN, T_SRV, answer, ANSWER_BUFSIZE);

  if (ret < 0)
    {
      /* FIXME decent error code */
      GError e = { GIBBER_RESOLVER_ERROR, 0,
        "Srv lookup failed" };
      gibber_resolver_srv_result (resolver, id, NULL, &e);
    }
  else
    {
      GList *entries = res_query_to_list (answer, ret);
      gibber_resolver_srv_result (resolver, id, entries, NULL);
    }

  g_free (srv_str);

  return FALSE;
}

static gboolean
resolver_resolv_addrinfo (GibberResolver *resolver, guint id,
  const gchar *hostname, const char *port, int address_family, int sock_type,
  int protocol, int flags)
{
  struct addrinfo req, *ans = NULL, *tmpaddr;
  int ret;
  GList *entries = NULL;

  memset (&req, 0, sizeof (req));
  req.ai_family = address_family;
  req.ai_socktype = sock_type;
  req.ai_protocol = protocol;
  req.ai_flags = flags;

  ret = getaddrinfo (hostname, port, &req, &ans);

  if (ret != 0)
    {
      GError e = { GIBBER_RESOLVER_ERROR, -ret, (gchar *)gai_strerror (ret) };
      gibber_resolver_addrinfo_result (resolver, id,
        NULL, &e);
      return FALSE;
    }

  for (tmpaddr = ans; tmpaddr != NULL; tmpaddr = tmpaddr->ai_next)
    {
      entries = g_list_append (entries,
        gibber_resolver_addrinfo_new (tmpaddr->ai_family,
          tmpaddr->ai_socktype, tmpaddr->ai_protocol,
          tmpaddr->ai_addr, tmpaddr->ai_addrlen));
    }

  freeaddrinfo (ans);

  gibber_resolver_addrinfo_result (resolver, id, entries, NULL);

  return FALSE;
}

static gboolean
resolver_resolv_nameinfo (GibberResolver *resolver, guint id,
  const struct sockaddr *sa, socklen_t salen, gint flags)
{
  int ret;
  gchar name[NI_MAXHOST], servicename[NI_MAXSERV];

  ret = getnameinfo (sa, salen, name, NI_MAXHOST, servicename, NI_MAXSERV,
    flags);

  if (ret != 0)
    {
      GError e = { GIBBER_RESOLVER_ERROR, ret, (gchar *) gai_strerror (ret) };

      gibber_resolver_nameinfo_result (resolver, id, NULL, NULL, &e);
      return FALSE;
    }

  gibber_resolver_nameinfo_result (resolver, id, g_strdup (name),
    g_strdup (servicename), NULL);

  return FALSE;
}

static void
resolver_resolv_cancel (GibberResolver *resolver, guint id)
{
  return;
}
