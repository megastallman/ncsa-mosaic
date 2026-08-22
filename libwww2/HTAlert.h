/*  */

/*      Displaying messages and getting input for WWW Library
**      =====================================================
**
**         May 92 Created By C.T. Barker
**         Feb 93 Portablized etc TBL
*/

#include "HTUtils.h"
#include "tcp.h"

/*      Display a message and get the input
**
**      On entry,
**              Msg is the message.
**
**      On exit,
**              Return value is malloc'd string which must be freed.
*/
extern char * HTPrompt PARAMS((WWW_CONST char * Msg, WWW_CONST char * deflt));
extern char * HTPromptPassword PARAMS((WWW_CONST char * Msg));


/*      Display a message, don't wait for input
**
**      On entry,
**              The input is a list of parameters for printf.
*/
extern void HTAlert PARAMS((WWW_CONST char * Msg));
extern void HTDoneWithIcon NOPARAMS;

/*      Display a progress message for information (and diagnostics) only
**
**      On entry,
**              The input is a list of parameters for printf.
*/
extern void HTProgress PARAMS((WWW_CONST char * Msg));
extern void HTMeter PARAMS((WWW_CONST int, WWW_CONST char *));
extern int HTCheckActiveIcon PARAMS((int twirl));
extern void HTClearActiveIcon NOPARAMS;


/*      Display a message, then wait for 'yes' or 'no'.
**
**      On entry,
**              Takes a list of parameters for printf.
**
**      On exit,
**              If the user enters 'YES', returns TRUE, returns FALSE
**              otherwise.
*/
extern BOOL HTConfirm PARAMS ((WWW_CONST char * Msg));

/*      Ask the user about a server certificate that failed verification.
**
**      On exit,
**              returns 1 to accept the certificate for this host,
**                      2 to accept all unverified certificates
**                        for the rest of the session,
**                      0 to cancel the transfer.
*/
extern int HTPromptCertAccept PARAMS ((WWW_CONST char * Msg));
/*

    */
