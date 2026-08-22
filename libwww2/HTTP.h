/*      HyperText Tranfer Protocol                                      HTTP.h
**      ==========================
*/

#ifndef HTTP_H
#define HTTP_H

#include "HTAccess.h"


extern HTProtocol HTTP;

#ifdef USE_LIBCURL
extern HTProtocol HTTPS;
#endif

#endif /* HTTP_H */
