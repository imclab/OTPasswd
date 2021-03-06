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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <errno.h>

#include <unistd.h>	/* usleep, open, close, unlink, getuid */
#include <sys/types.h>
#include <sys/stat.h>	/* stat */
#include <pwd.h>	/* getpwnam */
#include <fcntl.h>

#include "print.h"
#include "state.h"
#include "db.h"
#include "config.h"
#include "crypto.h"

#if S_SPLINT_S
#define PRIuMAX "llu"
#endif

/******************
 * Static helpers
 ******************/
static char *_strtok(char *input, const char *delim)
{
	static char *position = NULL;
	char *token;
	if (input != NULL)
		position = input;

	/* FIXME: valgrind doesn't like following line:
	 * As of 0.7 valgrind doesn't seem to have a problem here. */
	token = strsep(&position, delim); /* Non C99 function */

	/* Cut token at any \n found */
	if (token) {
		char *pos = strchr(token, '\n');
		if (pos)
			*pos = '\0';
	}
	return token;
}

/* We might be run by root (from PAM) or suid to cfg->user_uid.
 *
 * 1) Check if db exists
 * 2) Check if it has correct permissions.
 * print warning if anything is wrong, but don't fix permissions.
 * If we will be unable to continue in general sense (directory
 * not owned by us - fail.
 *
 * Mode 1. DB=user.
 * Check ownership and see if others can read it. Check if db_user_home exists.
 *
 * Mode 2. DB=global or other
 * We're run as root then we drop rights to match cfg->user_uid.
 *
 * State files are created with PAM also. Because of this
 * we must ensure correct owner of file.
 */
static int _db_file_permissions(const char *db_path, const char *user_home)
{
	/* Limit number of warnings printed */
	static int printed = 0;

	const uid_t uid = getuid();
	struct stat st;
	cfg_t *cfg = cfg_get();

	/* Ensure permissions were changed */
	if (uid != 0) {
		if (cfg->db != CONFIG_DB_USER) {
			assert(getuid() == cfg->user_uid);
		}
	}

	switch (cfg->db) {
	case CONFIG_DB_GLOBAL:
		/*** Start with directory ***/
		if (stat(CONFIG_DIR, &st) != 0) {
			/* Might be caused by some weird race condition.
			 * Just fail */
			print(PRINT_ERROR, "Strange error. " CONFIG_DIR
			      " ceased to exist during program work.\n");
			return STATE_IO_ERROR;
		}

		/* 1) It should be a dir */
		if (!S_ISDIR(st.st_mode)) {
			/* Error, not a file */
			print(PRINT_ERROR, "ERROR: " CONFIG_DIR " is not a directory\n");
			return STATE_IO_ERROR;
		}

		/* 2) Owned by user defined in config */
		if (st.st_uid != cfg->user_uid) {
			/* Not ok. */
			print(PRINT_ERROR,
			      "Owner of \"%s\" file must match USER field defined in config.\n",
			      CONFIG_DIR);
			return STATE_IO_ERROR;
		}

		/* 3) No "write" rights for group or others too */
		if (st.st_mode & (S_IWGRP | S_IWOTH)) {
			/* If were wrong show it. */
			print(PRINT_ERROR,
			      "Directory \"%s\" has write permissions "
			      "for others or group. Fix it and try again.\n",
			      CONFIG_DIR);
			return STATE_IO_ERROR;
		}

		/*** Directory checked, now check file itself ***/
		if (stat(db_path, &st) != 0) {
			/* Does not exists */
			print(PRINT_NOTICE, "Database \"%s\" does not exists.\n",
				db_path);
			return STATE_NON_EXISTENT;
		}

		/* Is DB a regular file? */
		if (!S_ISREG(st.st_mode)) {
			print(PRINT_ERROR, "Database \"%s\" is not a regular file.\n",
				db_path);
			return STATE_IO_ERROR;
		}

		/* Is it at most 700? */
		if (st.st_mode & (S_IWGRP | S_IWOTH)) {

			print(PRINT_ERROR,
			      "Database \"%s\" has write permissions "
			      "for others or group. Fix it and try again.\n",
			      CONFIG_DIR);
			return STATE_IO_ERROR;
		}

		/* Is it owned by user from config? */
		if (st.st_uid != cfg->user_uid) {
			/* Not it's not */
			if (uid == 0) {
				/* Ok. It might be created by PAM. Then - fix */
				if (chown(db_path, cfg->user_uid, cfg->user_gid) != 0) {
					print(PRINT_ERROR,
					      "Fixing perms of global DB failed while root.\n");
					return STATE_IO_ERROR;
				}
				/* OK */
			} else {
				print(PRINT_ERROR, "Database \"%s\" not owned by user defined in config.\n",
				      db_path);
				return STATE_IO_ERROR;
			}
		}

		break;

	case CONFIG_DB_USER:
		/* Be much more lenient than in GLOBAL_DB mode. */

		/* 1) If state doesn't exist return appropriate error.
		 * 2) It must be a file
		 * 3) Others should never have rwx - notice.
		 *
		 * We can be run as either from PAM as uid=0,
		 * or from the utility with credentials of either
		 * the user or special SUID otpasswd user.
		 */

		/* 1) If the user doesn't have a HOME directory - return appropriate error */
		if (user_home != NULL) {
			if (stat(user_home, &st) != 0) {
				/* User doesn't have HOME */
				print(PRINT_WARN, "User without a HOME (%s) tried to login.\n", user_home);
				return STATE_NO_USER_HOME;
			}

			if (!S_ISDIR(st.st_mode)) {
				/* Error, not a directory. */
				print(PRINT_ERROR, "User home directory is not a directory\n");
				return STATE_NO_USER_HOME;
			}
		}

		/* 2) Check if state file exists and read it's parameters */
		if (stat(db_path, &st) != 0) {
			/* Does not exists - not fatal if not enforcing.
			 * Can happen when locking for state creation.
			 */
			print(PRINT_NOTICE, "User state doesn't exists.\n");
			return STATE_NON_EXISTENT;
		}

		/* 3) Is file */
		if (!S_ISREG(st.st_mode)) {
			/* Error, not a file */
			print(PRINT_ERROR, "User state is not a regular file\n");
			return STATE_IO_ERROR;
		}

		/* 4) Others/Group */
		if (st.st_mode & (S_IRWXG | S_IRWXO)) {
			if (!printed)
				print(PRINT_WARN,
				      "User state file is readable by others or by a group.\n");
		}

		/* TODO: Enforce uid of file matches uid of s->username uid */
		if (uid != 0 && st.st_uid != uid) {
			if (!printed)
				print(PRINT_WARN, "User state not owned by user "
				      "running the utility.\n");
		}
		break;

	default:
		print(PRINT_ERROR, "You should never end up here\n");
		assert(0);
		return 1;
		break;
	}
	printed++;
	return 0;
}

/* Returns a name of current state + lock + temp file.
 * When DB=USER is set it also returns UID, GID and HOME of a given user
 */
static int _db_path(const char *username, char **db, char **lck, char **tmp,
                    uid_t *uid, gid_t *gid, char **home)
{
	int retval = 1;
	static struct passwd *pwdata = NULL;
	cfg_t *cfg = cfg_get();

	assert(username != NULL);
	assert(cfg != NULL);
	assert(*db == NULL && *lck == NULL && *tmp == NULL); /* Must be NULL */

	if (uid)
		*uid = (uid_t) -1;
	if (gid)
		*gid = (gid_t) -1;

	/* Determine db location at first */
	switch (cfg->db) {
	case CONFIG_DB_USER:
	{
		char *userhome = NULL;
		int length;

		/* Get home */
		pwdata = getpwnam(username);
		if (pwdata && pwdata->pw_dir) {
			userhome = pwdata->pw_dir;
		} else {
			return STATE_NO_SUCH_USER;
		}

		/* Append a filename */
		length = strlen(userhome);
		length += strlen(cfg->user_db_path);
		length += 2;

		*db = malloc(length);
		if (!*db)
			return STATE_NOMEM;

		{
			int ret = snprintf(*db, length, "%s/%s", userhome, cfg->user_db_path);
			assert(ret == length - 1);
		}

		if (home) {
			*home = strdup(userhome);
			if (!*home) {
				retval = STATE_NOMEM;
				goto error;
			}
		}

		if (uid)
			*uid = pwdata->pw_uid;
		if (gid)
			*gid = pwdata->pw_gid;

		break;
	}
	case CONFIG_DB_GLOBAL:
		*db = strdup(cfg->global_db_path);
		if (!*db) {
			return STATE_NOMEM;
		}
		break;

	case CONFIG_DB_MYSQL:
	case CONFIG_DB_LDAP:
	default:
		/* We should not be called with this options never */
		assert(0);
		retval = 1;
		goto error;
	}

	{
		const int db_len = strlen(*db);
		/* Create lock filename; normal file + .lck */

		retval = STATE_NOMEM;
		*lck = malloc(db_len + 5 + 1);
		*tmp = malloc(db_len + 5 + 1);

		if (!*lck || !*tmp) {
			goto error;
		}

		strncpy(*lck, *db, db_len);
		strncpy(*tmp, *db, db_len);
		strcpy(*lck + db_len, ".lck");
		strcpy(*tmp + db_len, ".tmp");
	}
	/* All ok */
	return 0;

error:
	free(*db), *db = NULL;
	free(*tmp), *tmp = NULL;
	free(*lck), *lck = NULL;
	free(*home), *home = NULL;
	return retval;
}

/* State files constants */
static const int _version = 1;
static const char *_delim = ":"; /* Change it also in snprintf in store */

static const int fields = 15;

enum {
	FIELD_USER = 0,
	FIELD_VERSION,
	FIELD_KEY,
	FIELD_COUNTER,
	FIELD_LATEST_CARD,
	FIELD_FAILURES,
	FIELD_RECENT_FAILURES,
	FIELD_CHANNEL_TIME,
	FIELD_CODE_LENGTH,
	FIELD_ALPHABET,
	FIELD_FLAGS,
	FIELD_SPASS,
	FIELD_SPASS_TIME,
	FIELD_LABEL,
	FIELD_CONTACT,
};

/* Find entry in database for username. Unmodified line
 * is left in buffer.
 *
 * If out is given each line we pass without a match
 * is written into this file.
 */
static int _db_find_user_entry(
	const char *username, FILE *f, FILE *out,
	char *buff, size_t buff_size)
{
	size_t line_length;
	char *first_sep;

	assert(username != NULL);
	assert(f != NULL);
	assert(buff != NULL);

	while (!feof(f)) {
		/* Read all file into a buffer */
		if (fgets(buff, buff_size, f) == NULL) {
			if (feof(f))
				/* Not found */
				return STATE_NO_USER_ENTRY;
			else
				/* Error */
				return STATE_IO_ERROR;
		}

		line_length = strlen(buff);

		if (buff[line_length-1] != '\n') {
			print(PRINT_NOTICE,
			      "Line too long inside the state file\n");
			return STATE_PARSE_ERROR;
		}

		if (line_length < 10) {
			/* This can't hold correct state */
			print(PRINT_NOTICE,
			      "State file is invalid. Line too short.\n");
			return STATE_PARSE_ERROR;
		}

		/* Temporary change first separator into \0 */
		first_sep = strchr(buff, _delim[0]);
		if (first_sep) {
			*first_sep = '\0';

			/* Check the username */
			if (username && (strcmp(buff, username) == 0)) {
				/* Found */
				*first_sep = _delim[0];
				return 0;
			}

			*first_sep = _delim[0];
		}

		if (out) {
			if (fputs(buff, out) < 0) {
				print(PRINT_NOTICE,
				      "Error while writing data to file!\n");
				return STATE_IO_ERROR;
			}
		}
	}

	/* Not found */
	return STATE_NO_USER_ENTRY;
}

static int _db_parse_user_entry(char *buff, char **field)
{
	int i, ret;
	/* Parse entry - split into fields, verify version */
	for (i=0; i<fields; i++) {
		field[i] = _strtok(i == 0 ? buff : NULL, _delim);
		if (field[i] == NULL) {
			print(PRINT_ERROR,
			      "State file invalid. Not enough fields.\n");
			return STATE_PARSE_ERROR;
		}

		if (strlen(field[i]) > STATE_MAX_FIELD_SIZE) {
			print(PRINT_ERROR,
			      "State file corrupted. Entry too long\n");
			return STATE_PARSE_ERROR;
		}

		/* If we parsed field version, check it immediately */
		if (i == FIELD_VERSION) {
			if (sscanf(field[i], "%u", &ret) != 1) {
				print(PRINT_ERROR,
				      "Error while parsing state file "
				      "version.\n");
				return STATE_PARSE_ERROR;
			}

			if (ret != _version) {
				print(PRINT_ERROR,
				      "State file version is incompatible. "
				      "Recreate key.\n");
				return STATE_PARSE_ERROR;
			}
		}
	}

	if (_strtok(NULL, _delim) != NULL) {
		print(PRINT_ERROR, "State file invalid. Too much fields.\n");
		return STATE_PARSE_ERROR;
	}

	return 0;
}

/**********************************************
 * Interface functions for managing state files
 **********************************************/
int db_file_load(state *s)
{
	/* State file should never be larger than 160 bytes */
	char buff[STATE_ENTRY_SIZE];

	/* Did we lock it here? */
	int locked;

	/* Pointers to fields in file */
	char *field[fields];

	/* Temporary variable for returned values */
	int ret = 0;

	/* State file */
	FILE *f = NULL;

	/* Value returned. */
	int retval;

	/* Iterator */
	int i;

	/* Files: database, lock and temporary */
	char *db = NULL, *lck = NULL, *tmp = NULL, *home = NULL;
	ret = _db_path(s->username, &db, &lck, &tmp, NULL, NULL, &home);
	if (ret != 0) {
		return ret;
	}

	/* Permissions will be checked during locking
	 * now, or was already checked */
	retval = _db_file_permissions(db, home);
	if (retval != 0) {
		goto cleanup1;
	}

	/* DB file should always be locked before changing.
	 * Locking can only be omitted when we want to discard
	 * any changes or that we don't bother if somebody changes
	 * them at the same time.
	 * Here we just detect that it's not locked and lock it then
	 */
	if (s->lock <= 0) {
		print(PRINT_NOTICE,
		      "State file not locked while reading from it\n");
		retval = db_file_lock(s);
		if (retval != 0) {
			print(PRINT_ERROR, "Unable to lock file for reading!\n");
			goto cleanup1;
		}

		/* Locked locally, unlock locally later */
		locked = 1;
	} else {
		locked = 0;
	}

	f = fopen(db, "r");
	if (!f) {
		if (errno == ENOENT)
			retval = STATE_NON_EXISTENT;
		else
			retval = STATE_IO_ERROR;
		print_perror(PRINT_ERROR,
			     "Unable to open %s for reading.",
			     db);
		goto cleanup;
	}

	/* Read all file into a buffer */
	ret = _db_find_user_entry(s->username, f, NULL, buff, sizeof(buff));
	if (ret != 0) {
		/* No entry, or file invalid */
		retval = ret;
		goto cleanup;
	}

	ret = _db_parse_user_entry(buff, field);
	if (ret != 0) {
		/* Parse error */
		retval = ret;
		goto cleanup;
	}

	/* Parse fields, if anybody bad happens return parse error */
	retval = STATE_PARSE_ERROR;

	/* Parse sequence key */
	for (i = 0; i < 32; i++) {
		char *pos = &field[FIELD_KEY][i*2];
		int tmp;
		if (!*pos || !*(pos+1)) {
			print(PRINT_ERROR, "Error while parsing sequence key.\n");
			goto cleanup;
		}
		if (sscanf(pos, "%02X", &tmp) != 1) {
			print(PRINT_ERROR, "Error while parsing sequence key.\n");
			goto cleanup;
		}
		s->sequence_key[i] = tmp;
	}

	if (num_import(&s->counter, field[FIELD_COUNTER], NUM_FORMAT_HEX) != 0) {
		print(PRINT_ERROR, "Error while parsing counter.\n");
		goto cleanup;
	}

	if (num_import(&s->latest_card, field[FIELD_LATEST_CARD], NUM_FORMAT_HEX) != 0) {
		print(PRINT_ERROR,
		      "Error while parsing number "
		      "of latest printed passcard\n");
		goto cleanup;
	}

	/* TODO: Change sscanf to more simple function */
	if (sscanf(field[FIELD_FAILURES], "%u", &s->failures) != 1) {
		print(PRINT_ERROR, "Error while parsing failures count\n");
		goto cleanup;
	}

	if (sscanf(field[FIELD_RECENT_FAILURES], "%u", &s->recent_failures) != 1) {
		print(PRINT_ERROR, "Error while parsing recent failure count\n");
		goto cleanup;
	}

	if (sscanf(field[FIELD_CHANNEL_TIME], "%" PRIuMAX, &s->channel_time) != 1) {
		print(PRINT_ERROR, "Error while parsing channel use time.\n");
		goto cleanup;
	}

	if (sscanf(field[FIELD_CODE_LENGTH], "%u", &s->code_length) != 1) {
		print(PRINT_ERROR, "Error while parsing passcode length\n");
		goto cleanup;
	}

	if (sscanf(field[FIELD_ALPHABET], "%u", &s->alphabet) != 1) {
		print(PRINT_ERROR, "Error while parsing alphabet\n");
		goto cleanup;
	}

	if (sscanf(field[FIELD_FLAGS], "%x", &s->flags) != 1) {
		print(PRINT_ERROR, "Error while parsing flags\n");
		goto cleanup;
	}

	if (strlen(field[FIELD_SPASS]) == 0) {
		s->spass_set = 0;
	} else {
		if (crypto_hex_to_binary(field[FIELD_SPASS], 80, s->spass) != 0) {
			print(PRINT_ERROR, "Error while parsing static password.\n");
			goto cleanup;
		}

		if (sscanf(field[FIELD_SPASS_TIME], "%" PRIuMAX, &s->spass_time) != 1) {
			print(PRINT_ERROR, "Error while parsing static password change time.\n");
			goto cleanup;
		}

		s->spass_set = 1;
	}

	/* Copy label and contact */
	strncpy(s->label, field[FIELD_LABEL], sizeof(s->label)-1);
	strncpy(s->contact, field[FIELD_CONTACT], sizeof(s->contact)-1);

	if (s->label[sizeof(s->label)-1] != '\0') {
		print(PRINT_ERROR, "Label field too long\n");
		goto cleanup;
	}

	if (s->contact[sizeof(s->contact)-1] != '\0') {
		print(PRINT_ERROR, "Contact field too long\n");
		goto cleanup;
	}

	if (!state_validate_str(s->label)) {
		print(PRINT_ERROR, "Illegal characters in label\n");
		goto cleanup;
	}

	if (!state_validate_str(s->contact)) {
		print(PRINT_ERROR, "Illegal characters in contact\n");
		goto cleanup;
	}

	/* Everything is read. Now - check if it's correct */
	if (num_sgn(s->counter) == -1) {
		print(PRINT_ERROR,
		      "Read a negative counter. "
		      "State file is corrupted.\n");
		goto cleanup;
	}

	if (num_sgn(s->latest_card) == -1) {
		print(PRINT_ERROR,
		      "Latest printed card is negative. "
		      "State file is corrupted.\n");
		goto cleanup;
	}

	if (s->code_length < 2 || s->code_length > 16) {
		print(PRINT_ERROR, "Illegal passcode length. %s is invalid\n",
		      db);
		goto cleanup;
	}

	if (s->flags > (FLAG_SHOW|FLAG_SALTED|FLAG_DISABLED)) {
		print(PRINT_ERROR, "Unsupported set of flags. %s is invalid\n",
		      db);
		goto cleanup;

	}

	retval = 0;
cleanup:
	/* Clear memory */
	memset(buff, 0, sizeof(buff));
	/* Unlocked if locally locked */
	if ((locked == 1) && (db_file_unlock(s) != 0)) {
		print(PRINT_ERROR, "Error while unlocking state file!\n");
		if (retval == 0)
			retval = STATE_LOCK_ERROR;
	}
	if (f)
		fclose(f);

cleanup1:
	free(db);
	free(tmp);
	free(lck);
	free(home);
	return retval;
}

static int _db_generate_user_entry(const state *s, char *buffer, int buff_length)
{
	int retval = 1;
	int tmp;

	/* Converted state parts */
	char sequence_key[65] = {0};
	char counter[35] = {0};
	char latest_card[35] = {0};
	char spass[81] = {0};

	if (crypto_binary_to_hex(s->sequence_key, 32, sequence_key) != 0) {
		print(PRINT_ERROR, "Strange error while converting sequence key into hex\n");
		goto error;
	}

	tmp = 0;
	tmp += num_export(s->counter, counter, NUM_FORMAT_HEX);
	tmp += num_export(s->latest_card, latest_card, NUM_FORMAT_HEX);

	if (tmp != 0) {
		print(PRINT_ERROR, "Error while converting numbers\n");
		goto error;
	}

	if (s->spass_set) {
		tmp = crypto_binary_to_hex(s->spass, 40, spass);
		if (tmp != 0) {
			print(PRINT_ERROR, "Error while converting static password data\n");
			goto error;
		}
	}

	tmp = snprintf(buffer, buff_length,
		       "%s:%d:"	             /* User, version */
		       "%s:%s:%s:"           /* Key, counter, latest_card */
		       "%u:%u:%" PRIuMAX ":" /* Failures, recent fails, channel time */
		       "%u:%u:%x:%s:"        /* Codelength, alphabet, flags, spass */
		       "%" PRIuMAX ":"	     /* Time of spass change */
		       "%s:%s\n",            /* label, contact */
		       s->username, _version,
		       sequence_key, counter, latest_card,
		       s->failures, s->recent_failures, s->channel_time,
		       s->code_length, s->alphabet, s->flags, spass,
		       s->spass_time,
		       s->label, s->contact);
	if (tmp < 10 || tmp == buff_length) {
		print(PRINT_ERROR, "Error while writing data to state file.");
		goto error;
	}

	retval = 0;
error:
	memset(sequence_key, 0, sizeof(sequence_key));
	return retval;
}

int db_file_store(state *s, int remove)
{
	/* Return value, by default return error */
	int ret;

	/* State file */
	FILE *in = NULL, *out = NULL;

	/* Did we lock the file? */
	int locked = 0;

	cfg_t *cfg = cfg_get();

	char user_entry_buff[STATE_ENTRY_SIZE];

	/* Files: database, lock and temporary */
	char *db = NULL, *lck = NULL, *tmp = NULL;
	uid_t user_uid;
	gid_t user_gid;
	ret = _db_path(s->username, &db, &lck, &tmp, &user_uid, &user_gid, NULL);
	if (ret != 0) {
		return ret;
	}

	if (s->lock <= 0) {
		print(PRINT_NOTICE,
		      "State file not locked while writing to it. Locking for write.\n");
		ret = db_file_lock(s);
		if (ret != 0) {
			print(PRINT_ERROR, "Unable to lock file for writing!\n");
			goto cleanup_free;
		}
		locked = 1;
	}

	if (cfg->db == CONFIG_DB_USER && remove) {
		ret = unlink(db);
		if (ret != 0) {
			ret = STATE_IO_ERROR;
			print_perror(PRINT_ERROR,
				     "Unable to unlink state file\n");
		} else {
			ret = 0;
		}
		goto cleanup_lock;
	}

	in = fopen(db, "r");
	if (!in) {
		/* User=db and file doesn't exist - ok. */

		/* Probably doesn't exists, if not... */
		if (errno != ENOENT) {
			/* FIXME: better rewrite to use stat */
			print_perror(PRINT_ERROR,
				     "Unable to open %s for reading", db);

			ret = STATE_IO_ERROR;
			goto cleanup;
		}
	}

	out = fopen(tmp, "w");
	if (!out) {
		print_perror(PRINT_ERROR,
			     "Unable to open %s for writing",
			     tmp);

		ret = STATE_IO_ERROR;
		goto cleanup;

	}

	/* Temporary file opened.
	 * it's owner/group should match owner of original file
	 * or if this one doesn't exist - the user we are working with.
	 */
	if (geteuid() == 0) {
		struct stat st;

		ret = stat(db, &st);
		if (ret != 0) {
			/* That's fine only for DB_USER */
			if (cfg->db != CONFIG_DB_USER) {
				print_perror(PRINT_ERROR, "Unable to read state file parameters and DB!=USER:");
				ret = STATE_IO_ERROR;
				goto cleanup;
			}
			st.st_uid = user_uid;
			st.st_gid = user_gid;
		}


		if (chown(tmp, st.st_uid, st.st_gid) != 0) {
			print_perror(PRINT_ERROR, "Unable to ensure owner/group of temporary file\n");
			ret = STATE_IO_ERROR;
			goto cleanup;
		}
	}


	if (in) {
		/* 1) Copy entries before our username */
		ret = _db_find_user_entry(s->username, in, out,	user_entry_buff, sizeof(user_entry_buff));
		if (ret != STATE_NO_USER_ENTRY && ret != 0) {
			/* Error happened. */
			goto cleanup;
		}
	}

	/* 2) Generate our new entry and store it into file */
	if (remove == 0) {
		ret = _db_generate_user_entry(s, user_entry_buff,
					      sizeof(user_entry_buff));
		if (ret != 0) {
			print(PRINT_ERROR,
			      "Strange error while generating new user "
			      "entry line\n");
			goto cleanup;
		}

		if (fputs(user_entry_buff, out) < 0) {
			print(PRINT_ERROR, "Error while writing user "
			      "entry to database\n");
			ret = STATE_IO_ERROR;
			goto cleanup;
		}
	}

	/* 3) Copy rest of the file */
	if (in) {
		ret = _db_find_user_entry(s->username, in, out,	user_entry_buff, sizeof(user_entry_buff));
		if (ret == 0) {
			print(PRINT_ERROR, "Duplicate entry for user %s in state file\n", s->username);
			goto cleanup;
		}

		if (ret != STATE_NO_USER_ENTRY) {
			/* Double user entry. */
			print(PRINT_NOTICE, "Double user entry in state file.\n");
			ret = STATE_PARSE_ERROR;
			goto cleanup;
		}
	}

	/* 4) Flush, save... then rename in cleanup part */
	ret = fflush(out);
	ret += fclose(out);
	out = NULL;
	if (ret != 0) {
		print_perror(PRINT_ERROR, "Error while flushing/closing state file");
		ret = STATE_IO_ERROR;
		goto cleanup;
	}

	ret = 0; /* We are fine! */

cleanup:
	if (in)
		fclose(in);
	if (out) {
		fflush(out);
		fclose(out);
	}

	sync(); /* Flush to disk required before rename */
	if (ret == 0) {
		/* If everything went fine, rename tmp to normal file */
		if (rename(tmp, db) != 0) {
			print_perror(PRINT_WARN,
				     "Unable to rename temporary state "
				     "file and save state.");
			ret = STATE_IO_ERROR;
		} else {
			/* When state updated via PAM (root)
			 * we must set correct file owner. */
			if (_db_file_permissions(db, NULL) != 0) {
				print(PRINT_WARN,
				      "Unable to set state file permissions. "
				      "Key might be world-readable!\n");
			}
			print(PRINT_NOTICE, "State file written correctly\n");
		}

	} else if (unlink(tmp) != 0) {
		print_perror(PRINT_WARN, "Unable to unlink temporary state file %s",
			     tmp);
	}

cleanup_lock:
	if (locked && db_file_unlock(s) != 0) {
		print(PRINT_ERROR, "Error while unlocking state file!\n");
	}

cleanup_free:
	free(db);
	free(lck);
	free(tmp);
	return ret;
}

int db_file_lock(state *s)
{
	struct flock fl;
	int ret;
	int cnt;
	int fd;

	/* Files: database, lock and temporary */
	char *db = NULL, *lck = NULL, *tmp = NULL, *home=NULL;

	/* Check that the lock already is not set */
	assert(s->lock == -1);

	ret = _db_path(s->username, &db, &lck, &tmp, NULL, NULL, &home);
	if (ret != 0) {
		return ret;
	}

	/*
	 * Verifies permissions for global DB or for user DB.
	 * OTPasswd should refuse to work with unsafe file permissions.
	 *
	 * Will also fail if the user doesn't have a home directory.
	 */
	ret = _db_file_permissions(db, home);
	switch (ret) {
	case STATE_IO_ERROR:
		print(PRINT_NOTICE, "File permission check failed\n");
		ret = STATE_LOCK_ERROR;
		goto cleanup;
	case STATE_NO_USER_HOME:
		/* We won't be able to lock. But that might not be
		 * fatal - propagate error */
		goto cleanup;
	case STATE_NON_EXISTENT:
		/* That's fine, we might be locking for state
		 * creation, try to lock. */
		break;
	}

	fl.l_type = F_WRLCK;
	fl.l_whence = SEEK_SET;
	fl.l_start = fl.l_len = 0;

	/* Open/create lock file */
	fd = open(lck, O_WRONLY|O_CREAT, S_IWUSR|S_IRUSR);

	if (fd == -1) {
		/* Unable to create file, therefore unable to obtain lock */
		print_perror(PRINT_NOTICE, "Unable to create %s lock file", lck);
		ret = STATE_LOCK_ERROR;
		goto cleanup;
	}

	/*
	 * Trying to lock the file 20 times.
	 * Any working otpasswd session shouldn't lock it for so long.
	 * If it does - system has some problem.
	 */
	for (cnt = 0; cnt < 20; cnt++) {
		ret = fcntl(fd, F_SETLK, &fl);
		if (ret == 0)
			break;
		usleep(700);
	}

	if (ret != 0) {
		/* Unable to lock for 10 times */
		close(fd);
		print(PRINT_NOTICE, "Unable to lock opened state file\n");
		ret = STATE_LOCK_ERROR;
		goto cleanup;
	}

	s->lock = fd;
	print(PRINT_NOTICE, "Got lock on state file\n");

	ret = 0; /* Got lock  */

cleanup:
	free(db);
	free(lck);
	free(tmp);
	free(home);

	return ret;
}



int db_file_unlock(state *s)
{
	struct flock fl;
	int retval = STATE_LOCK_ERROR;

	/* Files: database, lock and temporary */
	char *db = NULL, *lck = NULL, *tmp = NULL;
	retval = _db_path(s->username, &db, &lck, &tmp, NULL, NULL, NULL);
	if (retval != 0) {
		return retval;
	}


	if (s->lock < 0) {
		print(PRINT_NOTICE, "No lock to release!\n");
		goto error;
	}

	fl.l_type = F_UNLCK;
	fl.l_whence = SEEK_SET;
	fl.l_start = fl.l_len = 0;

	/* First unlink, then unlock to solve race condition */
	unlink(lck);

	retval = fcntl(s->lock, F_SETLK, &fl);

	close(s->lock);
	s->lock = -1;

	if (retval != 0) {
		print(PRINT_NOTICE, "Strange error while releasing lock\n");
		/* Strange error while releasing the lock */
		retval = STATE_LOCK_ERROR;
		goto error;
	}

	retval = 0;
error:
	free(db);
	free(lck);
	free(tmp);
	return retval;
}
