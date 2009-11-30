/**********************************************************************
 * otpasswd -- One-time password manager and PAM module.
 * (C) 2009 by Tomasz bla Fortuna <bla@thera.be>, <bla@af.gliwice.pl>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * See LICENSE file for details.
 **********************************************************************/

#include "num.h"
#include "crypto.h"
#include "ppp.h"
#include "state.h"
#include "print.h"

/* 64 characters -> 16 777 216 passcodes for length 4 */
const char alphabet_simple[] =
	"!#%+23456789:=?@"
	"ABCDEFGHJKLMNPRSTUVWXYZ"
	"abcdefghijkmnopqrstuvwxyz";

/* 88 characters -> 59 969 536 passcodes for length 4 */
const char alphabet_extended[] =
	"!\"#$%&'()*+,-./23456789:;<=>?@ABCDEFGHJKLMNO"
	"PRSTUVWXYZ[\\]^_abcdefghijkmnopqrstuvwxyz{|}~";

void ppp_add_salt(const state *s, mpz_t passcode) 
{
	if (!(s->flags & FLAG_NOT_SALTED)) {
		mpz_t salt;
		mpz_init_set(salt, s->counter);
		mpz_and(salt, salt, s->salt_mask);
		mpz_add(passcode, passcode, salt);
		num_dispose(salt);
	}
}

int ppp_get_passcode_number(const state *s, const mpz_t passcard, mpz_t passcode, char column, char row)
{
	/* TODO, FIXME Ensure ranges */

	if (column < 'A' || column >= 'A' + s->codes_in_row) {
		print(PRINT_NOTICE, "Column out of possible range!\n");
		return 1;
	}

	if (row < 1 || row > 10) {
		print(PRINT_NOTICE, "Row out of range!\n");
		return 1;
	}

	/* Start with calculating first passcode on card */
	/* passcode = (passcard-1)*codes_on_card + salt */
	mpz_sub_ui(passcode, passcard, 1);
	mpz_mul_ui(passcode, passcode, s->codes_on_card);

	/* Then add location on card */
	mpz_add_ui(passcode, passcode, (row - 1) * s->codes_in_row);
	mpz_add_ui(passcode, passcode, column - 'A');

	/* Add salt if required */
	ppp_add_salt(s, passcode);
	return 0;
}

int ppp_get_passcode(const state *s, const mpz_t counter, char *passcode)
{
	unsigned char key_bin[32];
	unsigned char cnt_bin[16];
	unsigned char cipher_bin[16];
	mpz_t cipher;
	mpz_t quotient;
	int i;

	int ret;

	/* Assure range during development */
	assert(mpz_tstbit(counter, 128) == 0);

	/* Check for illegal data */
	assert(s->code_length >= 2 && s->code_length <= 16);
	assert(mpz_sgn(s->counter) >= 0);

	if (!passcode)
		return 2;

	mpz_init(quotient);
	mpz_init(cipher);

	/* Convert numbers to binary */
	num_to_bin(s->sequence_key, key_bin, 32);
	num_to_bin(counter, cnt_bin, 16);

	/* Encrypt counter with key */
	ret = crypto_aes_encrypt(key_bin, cnt_bin, cipher_bin);
	if (ret != 0) {
		goto clear;
	}

	/* Convert result back to number */
	num_from_bin(cipher, cipher_bin, 16);

	int alphabet_len;
	const char *alphabet;
	if (s->flags & FLAG_ALPHABET_EXTENDED) {
		alphabet = alphabet_extended;
		alphabet_len = sizeof(alphabet_extended) - 1;
	} else {
		alphabet = alphabet_simple;
		alphabet_len = sizeof(alphabet_simple) - 1;
	}


	for (i=0; i<s->code_length; i++) {
		unsigned long int r = mpz_fdiv_q_ui(quotient, cipher, alphabet_len);
		mpz_set(cipher, quotient);

		passcode[i] = alphabet[r];
	}

	passcode[i] = '\0';

clear:
	memset(key_bin, 0, sizeof(key_bin));
	memset(cnt_bin, 0, sizeof(cnt_bin));
	memset(cipher_bin, 0, sizeof(cipher_bin));

	num_dispose(quotient);
	num_dispose(cipher);
	return ret;
}

void ppp_dispose_prompt(state *s)
{
	if (!s->prompt)
		return;

	const int length = strlen(s->prompt);
	memset(s->prompt, 0, length);
	free(s->prompt);
	s->prompt = NULL;
}

const char *ppp_get_prompt(state *s)
{
	/* "Passcode RRC [number]: " */
	const char intro[] = "Passcode ";
	int length = sizeof(intro)-1 + 3 + 5 + 1;
	char *num;

	if (s->prompt)
		ppp_dispose_prompt(s);

	/* Ensure ppp_calculate was called already! */
	assert(s->codes_on_card != 0);

	num = mpz_get_str(NULL, 10, s->current_card);
	length += strlen(num);

	s->prompt = malloc(length);
	if (!s->prompt)
		return NULL;

	int ret = sprintf(s->prompt, "%s%2d%c [%s]: ", intro, s->current_row, s->current_column, num);

	memset(num, 0, strlen(num)); 
	free(num);
	num = NULL;

	assert(ret+1 == length);

	if (ret <= 0) {
		memset(s->prompt, 0, length);
		free(s->prompt);
		s->prompt = NULL;
		return NULL;
	}
	return s->prompt;
}

int ppp_authenticate(const state *s, const char *passcode) 
{
	char current_passcode[17];

	if (passcode == NULL)
		return 1;

	if (ppp_get_passcode(s, s->counter, current_passcode) != 0)
		return 2;

	if (strcmp(passcode, current_passcode) != 0)
		return 3;

	return 0;
}

/**********************
 * Passcard management
 **********************/
/* Number of passcodes in row depending on passcode length */
static int _len_to_card_size[] = {
	-1, /* use up index 0, just to make it easier */
	-1, /* minimal length is 2 */
	11, /* which fits 11 passcodes in row */
	8,
	7,
	5, /* 5 - 6 */
	5,
	4, /* 7 */
	3, /* 8 - 10 */
	3,
	3,
	2, /* 11 - 16 */
	2,
	2,
	2,
	2,
	2,

};

void ppp_calculate(state *s)
{
	const char columns[] = "ABCDEFGHIJKL";

	/* Do some checks */
	assert(s->code_length >= 2 && s->code_length <= 16);
	assert(mpz_sgn(s->counter) >= 0);

	s->codes_in_row = _len_to_card_size[s->code_length];
	s->codes_on_card = s->codes_in_row * ROWS_PER_CARD;

	/* Calculate current card */
	mpz_t unsalted_counter;
	mpz_init_set(unsalted_counter, s->counter);
	if (!(s->flags & FLAG_NOT_SALTED)) {
		mpz_and(unsalted_counter, unsalted_counter, s->code_mask);
	} 

	unsigned long int r = mpz_fdiv_q_ui(s->current_card, unsalted_counter, s->codes_on_card);
	mpz_add_ui(s->current_card, s->current_card, 1);

	num_dispose(unsalted_counter);

	/* Calculate column/row using rest from division */
	int current_column = r % s->codes_in_row;
	r -= current_column;
	s->current_row = 1 + r / s->codes_in_row;
	s->current_column = columns[current_column];
}

/***************************
 * Testcases
 **************************/

#define _PPP_TEST(cnt,len, col, row, code)			\
mpz_set_ui(s.counter, (cnt)); s.code_length = (len);		\
ppp_calculate(&s);						\
buf1 = mpz_get_str(NULL, 10, s.counter);			\
buf2 = mpz_get_str(NULL, 10, s.current_card);			\
ppp_get_passcode(&s, s.counter, passcode);			\
printf("ppp_testcase[%2d]: ", test++);				\
printf("cnt=%10s len=%2d in_row=%d pos=%d%c[%8s] code=%16s",	\
       buf1, s.code_length, s.codes_in_row, s.current_row,	\
       s.current_column, buf2, passcode);			\
if (s.current_row == (row) && s.current_column == (col)		\
    && strcmp(passcode, (code)) == 0)				\
	printf(" PASSED\n"); else printf(" FAILED\n\n");	\
free(buf1); free(buf2);


static void _ppp_testcase_authenticate(const char *passcode)
{
	int retval;

	const char *prompt = NULL;

	/* OTP State */
	state s;

	/* Module options */

	/* Enforced makes any user without an .otpasswd config
	 * fail to login */
	int enforced = 0;	/* Do we enforce OTP logons? */

	printf("*** Authenticate testcase\n");
	print_init(PRINT_NOTICE, 1, 1, "/tmp/otpasswd_dbg");
	
	/* Initialize state with given username, and default config file */
	if (state_init(&s, NULL, ".otpasswd_testcase") != 0) {
		/* This will fail if we're unable to locate home directory */
		printf("STATE_INIT FAILED\n");
		print_fini();
		return;
	}

	/* Using locking load state, increment counter, and store new state */
	retval = state_load_inc_store(&s);
	switch (retval) {
	case 0:
		printf("LOAD_INC_STORE=OK\n");
		/* Everything fine */
		break;

	case STATE_DOESNT_EXISTS:
		if (enforced == 0) {
			/* Not enforced - ignore */
			printf("IGNORING - NO DIR\n");
			goto cleanup;
		} else {
			printf("ENFORCING AND NO DIRECTORY\n");
			goto cleanup;
		}

		
	default: /* Any other problem - error */
		printf("STATE_LOAD_INC_STORE FAILED\n");
		goto cleanup;
	}


	/* Generate prompt */
	ppp_calculate(&s);
	prompt = ppp_get_prompt(&s);
	if (!prompt) {
		printf("GET_PROMPT FAILED\n");
		goto cleanup;
	}

	if (ppp_authenticate(&s, passcode) == 0) {
			
		/* Correctly authenticated */
		printf("AUTHENTICATION SUCCESSFULL\n");
		goto cleanup;
	}

	printf("AUTHENTICATION NOT SUCCESSFULL\n");

cleanup:
	state_fini(&s);
}

void ppp_testcase(void)
{
	char *buf1, *buf2;
	int test = 1;
	char passcode[17] = {0};

	/* Check calculations */
	state s;
	state_init(&s, NULL, NULL);

	_PPP_TEST(0, 4, 'A', 1, "NH7j");

	_PPP_TEST(34, 4, 'G', 5, "EXh5");
	_PPP_TEST(864197393UL+50UL, 4, 'E', 8, "u2Yp");

	/* length = 5 */
	_PPP_TEST(0UL, 5, 'A', 1, "NH7js");
	_PPP_TEST(124UL, 5, 'E', 5, "+S:HK");

	/* length = 16 */
	_PPP_TEST(574734UL, 16, 'A', 8, "wcLSDqSyXJqxxYyr");

	/*** Tests with other sequence_key ***/
	const unsigned char key_bin[32] = {
		0x80, 0x45, 0x32, 0x22,
		0x10, 0xFF, 0xEE, 0x00,
		0x00, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00,
		0x65, 0x75, 0x86, 0x98,
	};
	// 8045322210FFEE00000000000000000000000000000000000000000065758698
	num_from_bin(s.sequence_key, key_bin, 32);

	printf("New key: ");
	crypto_print_hex(key_bin, 32);
	/* length = 4 */
	_PPP_TEST(0, 4, 'A', 1, ":LJ%");

	_PPP_TEST(34, 4, 'G', 5, "#W++");
	_PPP_TEST(864197393UL+50UL, 4, 'E', 8, "BBaF");

	/* length = 5 */
	_PPP_TEST(0UL, 5, 'A', 1, ":LJ%@");
	_PPP_TEST(124UL, 5, 'E', 5, "rUiHE");

	/* length = 16 */
	_PPP_TEST(574734UL, 16, 'A', 8, "vaxZ5sXJryc?KCn8");

	/*** Try second alphabet ***/
	s.flags |= FLAG_ALPHABET_EXTENDED;
	printf("Second alphabet\n");
	_PPP_TEST(0, 7, 'A', 1, "Y*HJ;,(");
	_PPP_TEST(70+34, 7, 'A', 7, "Ao_\"e82");
	_PPP_TEST(70+36, 7, 'C', 7, "(&JV?E_");

	/* TODO FIXME: do some get_passcode_number testcases */

	state_fini(&s);


	/* Authenticate testcase */
	/* Create file with empty key */
	if (state_init(&s, NULL, ".otpasswd_testcase") != 0) {
		printf("ERROR WHILE CREATING TEST KEY\n");
		return;
	}
	state_store(&s);
	state_fini(&s);
	
	printf("Should succeed:\n");
	_ppp_testcase_authenticate("NH7j");
	printf("Should NOT succeed:\n");
	_ppp_testcase_authenticate("aSsD");
}