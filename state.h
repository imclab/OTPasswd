#ifndef _STATE_H_
#define _STATE_H_

#include <gmp.h>

/*** Config ***/
#define STATE_FILENAME ".otpasswd"
/* Base for storing big numbers inside STATE_FILENAME */
#define STATE_BASE	62
#define STATE_LABEL_SIZE 30
#define STATE_CONTACT_SIZE 60
#define ROWS_PER_CARD 10

/* We must distinguish between locking problems (critical)
 * and non-existant state file (usually not critical) */
enum errors {
	STATE_LOCK_ERROR = 45,
	STATE_PARSE_ERROR = 46,
	STATE_DOESNT_EXISTS = 47, /* or it not a regular file */
	STATE_PERMISSIONS = 48, /* Insufficient possibly */
};


/*** State ***/
enum flags {
	FLAG_SHOW = 1,
	FLAG_SKIP = 2,
	FLAG_ALPHABET_EXTENDED = 4,
	FLAG_NOT_SALTED = 8,
};

typedef struct {
	/*** State stored in STATE_FILENAME ***/

	/* 128 bit counter pointing at the next passcode
	 * which will be used for authentication */
	mpz_t counter;

	/* User secret sequence key */
	mpz_t sequence_key;

	/* Furthest printed passcode
	 * used for warnings */
	mpz_t furthest_printed;

	/* Number of bytes used for passcode (2 - 16) */
	unsigned int code_length;

	/* User flags */
	unsigned int flags;

	/* Card label (might be zeroed, then hostname is used) */
	char label[STATE_LABEL_SIZE];

	/* Phone number... ANY info used for informing a user
	 * for example an email, or - a phone number... */
	char contact[STATE_CONTACT_SIZE];

	/*** Temporary / not-saved data ***/
	char *prompt; /* Keep it here so we can safely dispose of it */

	/* Salt helper. counter & salt_mask = salt while
	 * counter & code_mask = passcode number 
	 * TODO: this can probably be achieved without this masks.
	 */
	mpz_t salt_mask;
	mpz_t code_mask;

	/* Card information, calculated once for
	 * simplicity and stored here
	 */
	unsigned int codes_on_card;
	unsigned int codes_in_row;
	mpz_t current_card;		/* Card with current code */
	unsigned char current_row;	/* 1 - 10 */
	unsigned char current_column;	/* A ... */

	/* Not necessarily part of a user state,
	 * this is data used for storing/restoring
	 * state information
	 */

	char *username;
	char *filename;	/* Path to state file    */
	int fd;		/* State file descriptor */
	int lock_fd;	/* Is the file locked?   */
} state;


/* Initializes state structure. Must be called before
 * any else function from this set 
 * If username is given we determine user home directory
 * using specified username, if it's NULL - we lookup environment
 */
extern int state_init(state *s, const char *username, const char *configfile);

/* Deinitializes state struct; should clear
 * any secure-relevant data and free the memory */
extern void state_fini(state *s);

/* Generate new key */
extern int state_key_generate(state *s, const int salt);
/* Increment passcode counter */
extern void state_inc(state *s);

extern void state_debug(const state *s);

/* Load state file. */
extern int state_load(state *s);
/* Store state into file */
extern int state_store(const state *s);

/* High level function used during authentication
 * 1. Lock file
 * 2. Open it
 * 3. Increment counter
 * 4. Save it and unlock
 */
extern int state_load_inc_store(state *s);


/* Do some tests (may overwrite your key file!) */
extern void state_testcase(void);
#endif