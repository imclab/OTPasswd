/**********************************************************************
 * otpasswd -- One-time password manager and PAM module.
 * Copyright (C) 2009, 2010 by Tomasz bla Fortuna <bla@thera.be>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with otpasswd. If not, see <http://www.gnu.org/licenses/>.
 **********************************************************************/

#ifndef _PAM_HELPERS_
#define _PAM_HELPERS_

/* libotp interface (for config and state declarations) */
#include "ppp.h"

/* Parse module options and modify options accordingly */
extern int ph_parse_module_options(int flags, int argc, const char **argv);

/* Send out of band message by calling external script.
 * s parameter is generally const, but child will
 * clean it up */
extern int ph_oob_send(pam_handle_t *pamh, state *s, const char *username);

/* Question user about static password. Return 0 on success */
extern int ph_validate_spass(pam_handle_t *pamh, 
                             const state *s, const char *username);

/* Display user a message; disabled if in "silent mode" */
extern void ph_show_message(pam_handle_t *pamh, 
                            const char *msg, const char *username);

/* Load state, increment Save, handle errors if any */
extern int ph_increment(pam_handle_t *pamh,
                        const char *username, state *s);

/* Function which automates a bit talking with a user */
extern struct pam_response *ph_query_user(
	pam_handle_t *pamh, int show, const char *prompt);

/* Drop user response */
extern void ph_drop_response(struct pam_response *reply);

/* Function performing PAM initialization */
extern int ph_init(pam_handle_t *pamh, int flags,
                   int argc, const char **argv,
                   state **s, const char **username);

/* Deinitialize whatever ph_init initialized */
extern void ph_fini(state *s);

#endif
