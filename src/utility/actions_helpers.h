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

#ifndef _ACTIONS_HELPERS_H_
#define _ACTIONS_HELPERS_H_

enum {
	QUERY_YES=0,
	QUERY_NO=2,
	QUERY_OBSCURE=1
};

enum { 
	PRINT_CODE = 1,
	PRINT_CARD = 2
};

/** Ask user once and return _YES, _NO or _OBSCURE */
extern int ah_yes_or_no(const char *msg);

/** Ask user until he gives up and answers. */
extern int ah_enforced_yes_or_no(const char *msg);

/** Read password without echoing characters to console */
extern const char *ah_get_pass(void);

/** Show user flags */
extern int ah_show_flags(agent *a);

/** Show user state (current codes/cards) */
extern int ah_show_state(agent *a);

/** Show user key/counter */
extern int ah_show_keys(agent *a, const options_t *options);

/** Set options defined by user - one, by one */
extern int ah_set_options(agent *a, const options_t *options);

/** Parse code specification and store resulting data in arguments */
extern int ah_parse_code_spec(agent *a, const char *spec, num_t *item);

/** Decode external card number and XY code position into a counter 
 * This function decreases passcard by one so counting starts at '1'.
 * Counter is created with salt included. Result returned in 'passcode'. */
extern int ah_get_passcode_number(agent *a, 
                                  const num_t passcard, 
                                  num_t *passcode, char column, char row);


#endif
