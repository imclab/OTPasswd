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

#ifndef _CONFIG_H_
#define _CONFIG_H_

#include <sys/types.h> /* uid_t */

#define CONFIG_DIR		"/etc/otpasswd/"
#define CONFIG_PATH		(CONFIG_DIR "otpasswd.conf")
#define CONFIG_DEF_DB_GLOBAL	(CONFIG_DIR "otshadow")
#define CONFIG_DEF_DB_USER	".otpasswd"
#define CONFIG_MAX_LINE_LEN	200
#define CONFIG_PATH_LEN		100
#define CONFIG_SQL_LEN		50
#define CONFIG_ALPHABET_LEN	90

/** DB types */
enum CONFIG_DB_TYPE {
	CONFIG_DB_GLOBAL = 0,
	CONFIG_DB_USER = 1,
	/* Feature database backends */
	CONFIG_DB_MYSQL = 2,
	CONFIG_DB_LDAP = 3,
	CONFIG_DB_UNCONFIGURED = 10
};

/** Fields */
enum {
	OOB_DISABLED = 0,
	OOB_REQUEST = 1,
	OOB_SECURE_REQUEST = 2,
	OOB_ALWAYS = 3
};

/** Arguments */
enum {
	CONFIG_DISALLOW = 0,
	CONFIG_ALLOW = 1,
	CONFIG_ENFORCE = 2,

	CONFIG_ENABLED = 0,
	CONFIG_DISABLED = 1,
};


typedef struct {
	/*** 
	 * General configuration 
	 ***/

	/** UID and GID of user we should run */
	uid_t user_uid;
	uid_t user_gid;

	/** Database selected */
	int db;

	/** Location of global database file */
	char global_db_path[CONFIG_PATH_LEN];

	/** Location of user database file */
	char user_db_path[CONFIG_PATH_LEN];

	/** SQL Configuration data */
	char sql_host[CONFIG_SQL_LEN];
	char sql_database[CONFIG_SQL_LEN];
	char sql_user[CONFIG_SQL_LEN];
	char sql_pass[CONFIG_SQL_LEN];

	/** SQL Configuration data */
	char ldap_host[CONFIG_SQL_LEN];
	char ldap_dn[CONFIG_SQL_LEN];
	char ldap_user[CONFIG_SQL_LEN];
	char ldap_pass[CONFIG_SQL_LEN];

	/***
	 * PAM Configuration
	 ***/

	/** Enforced makes any user without key
	 * fail to login */
	int pam_enforce;

	/** Should PAM check if existing state files break policy? */
	int pam_enforce_policy;

	/** Turns on increased debugging
	 * 0 - Only Errors
	 * 1 - Errors, Warnings
	 * 2 - Errors, Warnings, Notices
	 */
	int pam_logging;

	/** Silent flag. 1 - Be silent. Can be set in 
	 * config or as an module option */
	int pam_silent;

	/** 0 - no retry
	 * 1 - retry with new passcode
	 * 2 - retry with the same passcode
	 * Will always retry 3 times...
	 */
	int pam_retry;

	/** How many retries are allowed */
	int pam_retries;

	/** Do we allow key regeneration (,) prompt? */
	int pam_key_regeneration_prompt;

	/** If recent_failures > 0, show user warning during session */
	int pam_failure_warning;
	
	/** Number of recent failures after which to increment
	 * delay */
	int pam_failure_boundary;

	/** Delay to add before authentication if 
	 * recent_failures hits failure_boundary */
	int pam_failure_delay;

	/** Require spass prefix on each logon */
	int pam_spass_require;

	/** 0 - OOB disabled
	 * 1 - OOB on request
	 * 2 - OOB on request; request requires password
	 * 3 - OOB sent during all authentication sessions
	 */
	int pam_oob;

	/** Out-Of-Band script path */
	/** Ensure that size of this field matches sscanf in _parse_options */
	char pam_oob_path[CONFIG_PATH_LEN];

	/* Parameters determined from the environment and
	 * not options themselves  */
	/** uid, gid of a safe, non-root user who can run OOB script */
	uid_t pam_oob_uid, pam_oob_gid; 

	/** Delay in seconds between two consecutive uses of oob */
	int pam_oob_delay;

	/***
	 * Policy configuration
	 * 1 - enable, 0 - disable
	 ***/
	/** User can generate himself a key */
	int key_generation;

	/** User can reregenerate existing key */
	int key_regeneration;

	/** User can disable his state */
	int disabling; 

	/** User can generate key by using a command line or file entry */
	int sourced_key_generation;
	
	/** Allow user to remove his key */
	int key_removal;

	/** Allow -a option usage */
	int shell_auth;

	/** User can skip further in passcodes */
	int skipping;

	/** User can print passcards/passcodes */
	int passcode_print;
	/** User can see his key/counter */
	int key_print;

	/** Allow state export */
	int state_export;
	/** Allow state import */
	int state_import;

	/** Allow contact change */
	int contact_change;
	/** Allow label change */
	int label_change;

	/** @{ Passcode configuration. Default, minimal and maximal */
	int passcode_def_length;
	int passcode_min_length;
	int passcode_max_length;
	/* @} */

	/** @{ Alphabet configuration. Default, minimal and maximal */
	/* def=1 - 64 long alphabet 
	 * def=2 - 88 long alphabet 
	 */
	int alphabet_change;
	int alphabet_def;
	int alphabet_min_length;
	int alphabet_max_length;
	char alphabet_custom[CONFIG_ALPHABET_LEN];
	/* @} */

	/** Allow user to change his static password */
	int spass_change;

	/** Minimal length */
	int spass_min_length;

	/** Minimal number of digits */
	int spass_require_digit;

	/** Minimal number of non-alpha */
	int spass_require_special;

	/** Minimal number of uppercase letters */
	int spass_require_uppercase;

	/** Disallow (0), allow (1) or enforce (2) salt */
	int salt;

	/** Enabled (1), disabled (1) */
	int salt_def;

	/** Passcode show policy: 
	 * Disallow (0) display, allow (1) or enforce (2) display 
	 */
	int show;

	/** Show passcodes: Enabled (1), disabled (1) */
	int show_def;
} cfg_t;

/** Get options structure or NULL if error happens */
extern cfg_t *cfg_get(void);

extern int cfg_permissions(void);

#endif
