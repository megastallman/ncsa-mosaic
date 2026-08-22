/*	HyperText Transfer Protocol client via libcurl		HTTPcurl.c
**	===============================================
**
**	Replaces the hand-rolled HTTP/1.0 client in HTTP.c when Mosaic is
**	built with -DUSE_LIBCURL, and adds https support.  libcurl does the
**	transport (connections, keep-alive, chunked encoding, proxying,
**	SSL/TLS); the response is fed to the existing www/mime stream stack
**	(HTMIME.c) so content-type dispatch, redirection via redirecting_url
**	and inline-image handling behave exactly as before.
**
**	Certificates that fail verification pop up an
**	"Accept Certificate / Accept All / Cancel" dialog; accepted hosts
**	are remembered for the rest of the session only.
*/

#include "../config.h"

#ifdef USE_LIBCURL

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <curl/curl.h>

#include "HTTP.h"
#include "HTParse.h"
#include "HTUtils.h"
#include "HTString.h"
#include "HTFormat.h"
#include "HTFile.h"
#include "HTAtom.h"
#include "HTAlert.h"
#include "HTMIME.h"
#include "HTAABrow.h"
#include "HTAAUtil.h"

struct _HTStream
{
  HTStreamClass * isa;
};

/* Shared state owned by HTTP.c (also compiled without its old engine). */
extern int securityType;
extern int selectedAgent;
extern char **agent;
extern int sendAgent;
extern int sendReferer;
extern char **extra_headers;
extern BOOL reloading;
extern char *HTReferer;

extern int do_head;
extern char *headData;
extern int do_post;
extern int do_put;
extern int do_meta;
extern int put_file_size;
extern FILE *put_fp;
extern char *post_content_type;
extern char *post_data;

extern BOOL using_proxy;
extern BOOL using_gateway;

extern int loading_inlined_images;

#ifndef DISABLE_TRACE
extern int www2Trace;
extern int httpTrace;
#endif

/* ------------------------- per-request state ---------------------------- */

typedef struct _CurlState
{
  HTParentAnchor *anchor;
  HTFormat format_out;
  HTStream *sink;
  HTStream *target;

  char *url;			/* the document URL (never the proxied form) */

  char *headers;		/* response headers, status line stripped */
  int header_len;
  int header_size;
  long status;			/* HTTP status code; 0 for an HTTP/0.9 reply */

  int auth_retried;		/* already retried with Authorization */
  char *held_body;		/* body of a 401 held back pending the retry */
  int held_len;
  int held_size;

  int no_stack;			/* no way to convert the content */
} CurlState;

/* ------------------------------ cookies --------------------------------- */

/*
 * Persistent cookie jar (Netscape format, managed entirely by
 * libcurl's cookie engine) in ~/.mosaic/cookies, so logins survive
 * between sessions.  The in-memory cookie store lives in the easy
 * handle and survives curl_easy_reset; the saved jar is read once at
 * the first request and flushed to disk after every request, since
 * the handle never gets a curl_easy_cleanup.
 */

static char cookie_jar[512];

static void setup_cookie_jar (CURL *handle)
{
  static int have_jar = -1;

  if (have_jar < 0)
    {
      char *home = getenv ("HOME");

      cookie_jar[0] = '\0';
      if ((home != NULL) && (strlen (home) + 32 < sizeof (cookie_jar)))
        {
          sprintf (cookie_jar, "%s/.mosaic", home);
          mkdir (cookie_jar, 0700);	/* usually exists already */
          strcat (cookie_jar, "/cookies");
        }
      have_jar = (cookie_jar[0] != '\0');
      if (have_jar)
        {
          /* read the cookies saved by previous sessions */
          curl_easy_setopt (handle, CURLOPT_COOKIEFILE, cookie_jar);
        }
    }

  if (have_jar)
    {
      /* keep the engine enabled after curl_easy_reset, and tell
         the FLUSH below where to write */
      curl_easy_setopt (handle, CURLOPT_COOKIEFILE, "");
      curl_easy_setopt (handle, CURLOPT_COOKIEJAR, cookie_jar);
    }
}

/* -------------------- session certificate whitelist --------------------- */

static char **cert_ok_hosts = NULL;
static int cert_ok_count = 0;
static int cert_accept_all = 0;

static int cert_host_accepted (char *host)
{
  int i;

  if (!host)
    return 0;
  for (i = 0; i < cert_ok_count; i++)
    if (strcasecomp (cert_ok_hosts[i], host) == 0)
      return 1;
  return 0;
}

static void cert_accept_host (char *host)
{
  if (!host || cert_host_accepted (host))
    return;
  if (cert_ok_hosts)
    cert_ok_hosts = (char **) realloc (cert_ok_hosts,
                                       (cert_ok_count + 1) * sizeof (char *));
  else
    cert_ok_hosts = (char **) malloc (sizeof (char *));
  cert_ok_hosts[cert_ok_count++] = strdup (host);
}

/* Fold a message onto lines of roughly `width' columns so the modal
   dialog does not clip it; returns a malloc'd string. */
static char *fold_message (char *text, int width)
{
  char *folded = strdup (text);
  char *p, *last_space = NULL;
  int col = 0;

  for (p = folded; *p; p++)
    {
      if (*p == '\n')
        {
          col = 0;
          last_space = NULL;
          continue;
        }
      if (*p == ' ')
        last_space = p;
      if (++col > width && last_space)
        {
          *last_space = '\n';
          col = p - last_space;
          last_space = NULL;
        }
    }
  return folded;
}

/* ----------------------------- callbacks -------------------------------- */

static size_t header_cb (char *buf, size_t size, size_t nitems, void *userdata)
{
  CurlState *st = (CurlState *) userdata;
  size_t len = size * nitems;

  if (len > 5 && strncmp (buf, "HTTP/", 5) == 0)
    {
      /* A new reply begins (100-continue, curl-internal retry, ...):
         restart header capture and pick up the status code. */
      char *sp;

      st->header_len = 0;
      if (st->headers)
        st->headers[0] = '\0';
      sp = (char *) memchr (buf, ' ', len);
      st->status = sp ? atol (sp + 1) : 0;
      return len;
    }

  if (st->header_len + (int) len + 1 > st->header_size)
    {
      st->header_size = (st->header_len + (int) len + 1) * 2;
      st->headers = st->headers ?
        (char *) realloc (st->headers, st->header_size) :
        (char *) malloc (st->header_size);
      if (!st->headers)
        outofmem (__FILE__, "header_cb");
    }
  memcpy (st->headers + st->header_len, buf, len);
  st->header_len += len;
  st->headers[st->header_len] = '\0';
  return len;
}

/* Build the output stream, replaying the captured headers into the
   www/mime parser the way the old code recycled its first read. */
static int start_target (CurlState *st)
{
  HTFormat format_in;
  int compressed = 0;

  if (st->status == 0)
    {
      /* An HTTP/0.9 reply -- no headers came back; guess from the URL. */
      HTAtom *encoding;

      format_in = HTFileFormat (st->url, &encoding, WWW_HTML, &compressed);
    }
  else
    format_in = HTAtom_for ("www/mime");

  st->target = HTStreamStack (format_in, st->format_out, compressed,
                              st->sink, st->anchor);
  if (!st->target)
    {
      char buffer[1024];

      sprintf (buffer, "Sorry, no known way of converting %s to %s.",
               HTAtom_name (format_in), HTAtom_name (st->format_out));
      HTProgress (buffer);
      return 0;
    }

  if (st->status != 0 && st->header_len)
    (*st->target->isa->put_block) (st->target, st->headers, st->header_len);
  return 1;
}

static size_t write_cb (char *buf, size_t size, size_t nmemb, void *userdata)
{
  CurlState *st = (CurlState *) userdata;
  size_t len = size * nmemb;

  if (st->status == 401 && !st->auth_retried)
    {
      /* Hold the reply back until we know whether the user will retry
         with authorization; it is only shown if they decline. */
      if (st->held_len + (int) len + 1 > st->held_size)
        {
          st->held_size = (st->held_len + (int) len + 1) * 2;
          st->held_body = st->held_body ?
            (char *) realloc (st->held_body, st->held_size) :
            (char *) malloc (st->held_size);
          if (!st->held_body)
            outofmem (__FILE__, "write_cb");
        }
      memcpy (st->held_body + st->held_len, buf, len);
      st->held_len += (int) len;
      return len;
    }

  if (st->status == 204)
    return len;			/* "no response" -- handled at the end */

  if (!st->target && !start_target (st))
    {
      st->no_stack = 1;
      return 0;			/* aborts the transfer */
    }

  (*st->target->isa->put_block) (st->target, buf, (int) len);
  return len;
}

static int xferinfo_cb (void *userdata, curl_off_t dltotal, curl_off_t dlnow,
                        curl_off_t ultotal, curl_off_t ulnow)
{
  char line[256];
  char *msg;

  if (HTCheckActiveIcon (1))
    return 1;			/* user interrupt -- abort the transfer */

  if (dlnow > 0)
    {
      if (dltotal > 0)
        {
          msg = loading_inlined_images ?
            "Read %ld of %ld bytes of inlined image data." :
            "Read %ld of %ld bytes of data.";
          sprintf (line, msg, (long) dlnow, (long) dltotal);
          HTMeter ((int) ((dlnow * 100) / dltotal), NULL);
        }
      else
        {
          msg = loading_inlined_images ?
            "Read %ld bytes of inlined image data." :
            "Read %ld bytes of data.";
          sprintf (line, msg, (long) dlnow);
        }
      HTProgress (line);
    }
  return 0;
}

/* ------------------------------ the loader ------------------------------ */

PUBLIC int HTLoadHTTPCurl (char *arg, HTParentAnchor *anAnchor,
                           HTFormat format_out, HTStream *sink)
{
  static CURL *handle = NULL;
  CurlState st;
  struct curl_slist *req_headers = NULL;
  char errbuf[CURL_ERROR_SIZE];
  char line[2048];		/* bumped up to cover huge headers */
  char *proxy = NULL;
  char *url;
  char *host = NULL;
  CURLcode res;
  long response_code;
  int status;
  int statusError = 0;
  int is_https;

  if (!arg || !*arg)
    {
      HTProgress ("Bad request.");
      do_post = 0;
      return arg ? -2 : -3;
    }

  if (!handle)
    {
      curl_global_init (CURL_GLOBAL_ALL);
      handle = curl_easy_init ();
      if (!handle)
        {
          HTProgress ("Could not initialize libcurl.");
          do_post = 0;
          return HT_NO_DATA;
        }
    }

  memset (&st, 0, sizeof (st));
  st.anchor = anAnchor;
  st.format_out = format_out;
  st.sink = sink;

  url = arg;
  /* When a proxy is configured, HTAccess hands us
     http://proxyhost:port/real-url -- take it apart again. */
  if (using_proxy)
    {
      char *p;
      int slashes;

      for (p = arg, slashes = 0; *p; p++)
        if (*p == '/' && ++slashes == 3)
          break;
      if (*p)
        {
          proxy = (char *) malloc (p - arg + 1);
          strncpy (proxy, arg, p - arg);
          proxy[p - arg] = '\0';
          url = p + 1;
        }
    }
  st.url = url;

  is_https = (strncasecomp (url, "https:", 6) == 0);
  host = HTParse (url, "", PARSE_HOST);

 try_again:
  st.header_len = 0;
  st.held_len = 0;
  st.status = 0;
  st.target = NULL;
  st.no_stack = 0;
  errbuf[0] = '\0';
  req_headers = NULL;

  curl_easy_reset (handle);
  curl_easy_setopt (handle, CURLOPT_URL, url);
  curl_easy_setopt (handle, CURLOPT_ERRORBUFFER, errbuf);
  curl_easy_setopt (handle, CURLOPT_NOSIGNAL, 1L);
  curl_easy_setopt (handle, CURLOPT_CONNECTTIMEOUT, 30L);
  curl_easy_setopt (handle, CURLOPT_HTTP09_ALLOWED, 1L);
  curl_easy_setopt (handle, CURLOPT_SUPPRESS_CONNECT_HEADERS, 1L);
  curl_easy_setopt (handle, CURLOPT_HEADERFUNCTION, header_cb);
  curl_easy_setopt (handle, CURLOPT_HEADERDATA, &st);
  curl_easy_setopt (handle, CURLOPT_WRITEFUNCTION, write_cb);
  curl_easy_setopt (handle, CURLOPT_WRITEDATA, &st);
  curl_easy_setopt (handle, CURLOPT_NOPROGRESS, 0L);
  curl_easy_setopt (handle, CURLOPT_XFERINFOFUNCTION, xferinfo_cb);
  curl_easy_setopt (handle, CURLOPT_XFERINFODATA, &st);
#ifndef DISABLE_TRACE
  if (httpTrace)
    curl_easy_setopt (handle, CURLOPT_VERBOSE, 1L);
#endif

  setup_cookie_jar (handle);

  if (proxy)
    curl_easy_setopt (handle, CURLOPT_PROXY, proxy);

  if (cert_accept_all || cert_host_accepted (host))
    {
      curl_easy_setopt (handle, CURLOPT_SSL_VERIFYPEER, 0L);
      curl_easy_setopt (handle, CURLOPT_SSL_VERIFYHOST, 0L);
    }

  if (do_post && do_put)
    {
      curl_easy_setopt (handle, CURLOPT_UPLOAD, 1L);
      curl_easy_setopt (handle, CURLOPT_READDATA, put_fp);
      curl_easy_setopt (handle, CURLOPT_INFILESIZE, (long) put_file_size);
      fseek (put_fp, 0L, SEEK_SET);
    }
  else if (do_post)
    {
      curl_easy_setopt (handle, CURLOPT_POSTFIELDS,
                        post_data ? post_data : "lose");
      sprintf (line, "Content-Type: %s",
               post_content_type ? post_content_type : "lose");
      req_headers = curl_slist_append (req_headers, line);
    }
  else if (do_head)
    {
      curl_easy_setopt (handle, CURLOPT_NOBODY, 1L);
    }
  else if (do_meta)
    {
      curl_easy_setopt (handle, CURLOPT_CUSTOMREQUEST, "META");
    }

  /* if reloading, send no-cache pragma to proxy servers. --swp */
  if (reloading)
    req_headers = curl_slist_append (req_headers, "Pragma: no-cache");

  if (sendAgent && agent && agent[selectedAgent])
    {
      sprintf (line, "User-Agent: %s", agent[selectedAgent]);
      req_headers = curl_slist_append (req_headers, line);
    }

  if (sendReferer && HTReferer)
    {
      /* HTTP Referer field, specifies back-link URL   - amb */
      sprintf (line, "Referer: %s", HTReferer);
      req_headers = curl_slist_append (req_headers, line);
      HTReferer = NULL;
    }

  /* Domain Restriction -- SWP */
  req_headers = curl_slist_append (req_headers,
                                   "Extension: Notify-Domain-Restriction");

  /* BJS -- allow arbitrary headers sent from browser */
  if (extra_headers)
    {
      int h;

      for (h = 0; extra_headers[h]; h++)
        req_headers = curl_slist_append (req_headers, extra_headers[h]);
    }

  /* Access authorization via the existing HTAA package; it prompts
     for a username/password when a 401 told us to retry. */
  {
    char *docname = HTParse (url, "", PARSE_PATH);
    char *hostname = HTParse (url, "", PARSE_HOST);
    char *colon;
    int portnumber = is_https ? 443 : 80;
    char *auth;

    if (hostname && NULL != (colon = strchr (hostname, ':')))
      {
        *(colon++) = '\0';
        portnumber = atoi (colon);
      }
    if (NULL != (auth = HTAA_composeAuth (hostname, portnumber, docname)))
      req_headers = curl_slist_append (req_headers, auth);
#ifndef DISABLE_TRACE
    if (www2Trace)
      {
        if (auth)
          fprintf (stderr, "HTTP: Sending authorization: %s\n", auth);
        else
          fprintf (stderr, "HTTP: Not sending authorization (yet)\n");
      }
#endif
    FREE (hostname);
    FREE (docname);
  }

  if (req_headers)
    curl_easy_setopt (handle, CURLOPT_HTTPHEADER, req_headers);

  if (host && *host)
    {
      sprintf (line, "Connecting to %s.", host);
      HTProgress (line);
    }

  res = curl_easy_perform (handle);
  response_code = 0;
  curl_easy_getinfo (handle, CURLINFO_RESPONSE_CODE, &response_code);
  if (response_code && !st.status)
    st.status = response_code;

  if (req_headers)
    {
      curl_slist_free_all (req_headers);
      req_headers = NULL;
    }

#ifndef DISABLE_TRACE
  if (www2Trace)
    fprintf (stderr, "HTTP: curl result %d, status %ld for '%s'\n",
             (int) res, st.status, url);
#endif

  if (res == CURLE_ABORTED_BY_CALLBACK)
    {
      HTProgress ("Data transfer interrupted.");
      if (st.target)
        (*st.target->isa->handle_interrupt) (st.target);
      HTMeter (100, NULL);
      status = HT_INTERRUPTED;
      goto done;
    }

  if (res == CURLE_WRITE_ERROR && st.no_stack)
    {
      status = -1;
      goto done;
    }

  if (res == CURLE_PEER_FAILED_VERIFICATION)
    {
      /* The server certificate could not be verified: put it to the user. */
      int answer;
      char *reason = fold_message (errbuf[0] ? errbuf :
                                   (char *) curl_easy_strerror (res), 52);
      char *msg = (char *) malloc ((host ? strlen (host) : 16) +
                                   strlen (reason) + 256);

      sprintf (msg,
               "The security certificate of host '%s'\ncould not be verified:\n\n%s\n\nAccept the certificate for this session, accept all\nunverified certificates, or cancel the transfer?",
               (host && *host) ? host : "(unknown)", reason);
      answer = HTPromptCertAccept (msg);
      free (msg);
      free (reason);

      if (answer == 2)
        cert_accept_all = 1;
      else if (answer == 1)
        cert_accept_host (host);
      else
        {
          HTProgress ("Transfer cancelled -- certificate not accepted.");
          status = HT_NO_DATA;
          goto done;
        }
      goto try_again;
    }

  if (res != CURLE_OK && !st.target && !st.held_len && !st.header_len)
    {
      HTProgress (errbuf[0] ? errbuf : (char *) curl_easy_strerror (res));
      status = HT_NO_DATA;
      goto done;
    }

  /* Now that the whole (small) 401 reply is held back, decide about
     an authorization retry. */
  if (st.status == 401 && !st.auth_retried && st.headers &&
      HTAA_shouldRetryWithAuth (st.headers, st.header_len, -1))
    {
      st.auth_retried = 1;
      HTProgress ("Retrying with access authorization information.");
      goto try_again;
    }

  if (st.status == 204)
    {
      /* return_nothing is high. */
      st.target = HTStreamStack (HTAtom_for ("text/html"), format_out, 0,
                                 sink, anAnchor);
      if (!st.target)
        {
          status = -1;
          goto done;
        }
      (*st.target->isa->put_string) (st.target, "<mosaic-access-override>\n");
      HTProgress ("And silence filled the night.");
    }
  else
    {
      if (!st.target && !start_target (&st))
        {
          status = -1;
          goto done;
        }
      if (st.held_len)
        (*st.target->isa->put_block) (st.target, st.held_body, st.held_len);
    }

  if (do_head && st.status == 200 && st.headers && *st.headers)
    {
      char *ptr;

      headData = strdup (st.headers);
      if (NULL != (ptr = strchr (headData, '\n')))
        *ptr = '\0';
      if (NULL != (ptr = strchr (headData, '\r')))
        *ptr = '\0';
    }

  (*st.target->isa->end_document) (st.target);
  (*st.target->isa->free) (st.target);
  st.target = NULL;

  HTProgress ("Data transfer complete.");
  HTMeter (100, NULL);

  switch (st.status / 100)
    {
    case 3:
      /* The redirection URL was stored in the external variable
         redirecting_url by HTMIME.c while it parsed the headers. */
      status = HT_REDIRECTING;
      break;
    case 4:
    case 5:
      statusError = 1;
      status = HT_LOADED;
      break;
    default:
      status = HT_LOADED;
      break;
    }

 done:
  /* Persist any new cookies now: the static handle never gets a
     curl_easy_cleanup, which is when the jar would normally be
     written. */
  if (cookie_jar[0])
    curl_easy_setopt (handle, CURLOPT_COOKIELIST, "FLUSH");

  /* Clear out on exit, just in case. */
  do_post = 0;

  if (statusError)
    {
      securityType = HTAA_NONE;
#ifndef DISABLE_TRACE
      if (www2Trace)
        fprintf (stderr, "Resetting security type to NONE.\n");
#endif
    }

  if (proxy)
    free (proxy);
  if (host)
    free (host);
  if (st.headers)
    free (st.headers);
  if (st.held_body)
    free (st.held_body);

  return status;
}


/*	Protocol descriptors
*/

PUBLIC HTProtocol HTTP = { "http", HTLoadHTTPCurl, 0 };
PUBLIC HTProtocol HTTPS = { "https", HTLoadHTTPCurl, 0 };

#endif /* USE_LIBCURL */
