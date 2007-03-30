
#include <stdarg.h>

#include <glib.h>

#include <telepathy-glib/debug.h>

#include "debug.h"

#ifdef ENABLE_DEBUG

static DebugFlags flags = 0;

GDebugKey keys[] = {
  { "presence",       DEBUG_PRESENCE },
  { "groups",         DEBUG_GROUPS },
  { "contacts",       DEBUG_CONTACTS },
  { "disco",          DEBUG_DISCO },
  { "properties",     DEBUG_PROPERTIES },
  { "roomlist",       DEBUG_ROOMLIST },
  { "media-channel",  DEBUG_MEDIA },
  { "muc",            DEBUG_MUC },
  { "muc-connection", DEBUG_MUC },
  { "net",            DEBUG_NET },
  { "connection",     DEBUG_CONNECTION },
  { "persist",        DEBUG_PERSIST },
  { "self",           DEBUG_SELF },
  { "all",            ~0 },
  { 0, },
};

void debug_set_flags_from_env ()
{
  guint nkeys;
  const gchar *flags_string;

  for (nkeys = 0; keys[nkeys].value; nkeys++);

  flags_string = g_getenv ("SALUT_DEBUG");

  if (flags_string) {
    tp_debug_set_flags_from_env("SALUT_DEBUG");
    debug_set_flags (g_parse_debug_string (flags_string, keys, nkeys));
  }
}

void debug_set_flags (DebugFlags new_flags)
{
  flags |= new_flags;
}

gboolean debug_flag_is_set (DebugFlags flag)
{
  return flag & flags;
}

void debug (DebugFlags flag,
                   const gchar *format,
                   ...)
{
  if (flag & flags)
    {
      va_list args;
      va_start (args, format);
      g_logv (G_LOG_DOMAIN, G_LOG_LEVEL_DEBUG, format, args);
      va_end (args);
    }
}

#endif /* ENABLE_DEBUG */

