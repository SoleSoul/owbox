/* gettext.h for owbox
   owbox has no message catalogs of its own yet.  The _() markers are kept from
   Openbox so that translations can be added (or Openbox's reused) later. */

#ifndef __ob_gettext_h
#define __ob_gettext_h

#define _(String) (String)
#define N_(String) (String)
#define gettext_noop(String) (String)

#endif
