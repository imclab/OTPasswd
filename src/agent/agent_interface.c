/**********************************************************************
 * otpasswd -- One-time password manager and PAM module.
 * Copyright (C) 2009-2013 by Tomasz bla Fortuna <bla@thera.be>
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
 *
 **********************************************************************/

#include <stdlib.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#include "ppp.h" /* Error handling mostly */

#include "nls.h"
#include "agent_private.h"
#include "print.h"

/* Check if given exists; if not, try two defaults, return existing path and return
 * 0 or return AGENT_ERR_INIT_EXECUTABLE */
static int _get_agent_executable(const char *agent, const char **agent_path)
{
	int ret;
	int i;
	struct stat st;
	const char *agents[] = { 
		agent,
#if DEBUG
#warning Utility will search for agent in its current directory!
		"./agent_otp",
#endif
		"/usr/bin/agent_otp",

		"/usr/local/bin/agent_otp",
		NULL };

	*agent_path = NULL;

	if (agent)
		agents[1] = NULL; /* If given path, check only the one given. */

	/* First agent might be null */
	for (i=0; i<1 || agents[i]; i++) {
		if (agents[i] == NULL)
			continue;

		ret = stat(agents[i], &st);
		if (ret != 0) {
			print(PRINT_NOTICE, 
			      "Error while trying to read agent executable at %s\n", 
			      agents[i]);
			continue;
		}

		if (!S_ISREG(st.st_mode)) {
			print(PRINT_NOTICE, 
			      "Agent executable (%s) not a regular file.\n", 
			      agents[i]);
			continue;
		}

		if (!(st.st_mode & S_IXOTH)) {
			print(PRINT_NOTICE, 
			      "Agent executable (%s) found but has no execution rights for others.\n", 
			      agents[i]);
			continue;
		}

		/* Everything seems fine */
		print(PRINT_NOTICE, "Selected agent: %s\n", agents[i]);
		*agent_path = agents[i];
		return 0;
	}

	print(PRINT_ERROR, 
	      "Unable to locate a valid agent executable. Check your installation.\n"
	      "\n"
	      "To search in current directory (test execution in source directory) you need\n"
	      "to compile in DEBUG mode\n");

	return AGENT_ERR_INIT_EXECUTABLE;
}

int agent_connect(agent **a_out, const char *agent_executable)
{
	int ret = 1;
	/* in[1]  - stdout of agent;  
	 * out[0] - stdin of agent; 
	 * in/out - naming from perspective of parent
	 */
	int in[2] = {-1, -1};
	int out[2] = {-1, -1};
	agent *a;
	*a_out = NULL;

	/* Allocate memory */
	a = malloc(sizeof(*a));
	if (!a)
		return AGENT_ERR_MEMORY;
	memset(a, 0, sizeof(*a));

	/* Initialize basic fields */
	a->error = 0;
	a->shdr.protocol_version = AGENT_PROTOCOL_VERSION;
	a->s = NULL;
	a->new_state = 0;

	/* Create pipes */
	if (pipe(in) != 0)
		goto cleanup;
		
	if (pipe(out) != 0)
		goto cleanup1;

	/* Verify that agent executable PATH exists */
	ret = _get_agent_executable(agent_executable, &agent_executable);
	if (ret != 0) {
		return ret;
	}

	a->pid = fork();

	if (a->pid == 0) {
		/* Prepare pipes */
		ret = close(in[0]);
		if (ret != 0) {
			print_perror(PRINT_ERROR, "Error while closing in[0]: ");
			exit(2);
		}

		ret = close(out[1]);
		if (ret != 0) {
			print_perror(PRINT_ERROR, "Error while closing out[1]: ");
			exit(2);
		}

		ret = close(0);
		if (ret != 0) {
			print_perror(PRINT_ERROR, "Error while closing stdin: ");
			exit(2);
		}

		ret = close(1);
		if (ret != 0) {
			print_perror(PRINT_ERROR, "Error while closing stdout: ");
			exit(2);
		}

		ret = close(2);
		if (ret != 0) {
			print_perror(PRINT_ERROR, "Error while closing stderr: ");
			exit(2);
		}

		/* fd 1 - stdout, fd 0 - stdin */
		if (dup(out[0]) == -1) {
			exit(6);
		}

		if (dup(in[1]) == -1) {
			exit(7);
		}

		free(a); a = NULL;

		/* Execute agent */
		execl(agent_executable, agent_executable, NULL);
		
		/* Failure */
		ret = agent_server(&a);
		if (ret != AGENT_OK) {
			/* Memory problem only possible */
			exit(1);
		}
			
		/* Sent a packet with information about death */
		agent_hdr_init(a, 0);
		agent_hdr_set_type(a, AGENT_REQ_INIT);
		agent_hdr_set_status(a, AGENT_ERR_INIT_EMERGENCY);
		agent_hdr_set_int(a, errno, 0);
		ret = agent_hdr_send(a);
		if (ret != AGENT_OK) {
			free(a);
			exit(4);
		}
		free(a);
		exit(5);
	}

	/* Close not our ends of pipes */
	close(in[1]); 
	in[1] = -1;
	
	close(out[0]);	
	out[0] = -1;

	a->in = in[0];
	a->out = out[1];


	/* TODO: Handle some signal? SIGPIPE? 
	 * Generally we should be able to die on SIGPIPE safely.
	 */

	/* Read message sent by server to indicate correct initialization
	 * or any initialization problems */
	ret = agent_wait(a);
	if (ret == 2) {
		ret = AGENT_ERR_SERVER_INIT;
		print(PRINT_MESSAGE, _("Error while waiting for agent intitial frame.\n"));
		goto cleanup1;
	} else if (ret != 0) {
		ret = AGENT_ERR_SERVER_INIT;
		print(PRINT_MESSAGE, _("Timeout while waiting for agent initialization frame.\n"));
		print(PRINT_MESSAGE, _("Possible cause of this problem involves wrong agent executable passed in configuration file.\n"));
		print(PRINT_MESSAGE, _("Try manually running agent executable to see where's the problem.\n"));		
		print(PRINT_MESSAGE, _("If you would want to send a bug report remember about gdb backtrace\n"));		
		print(PRINT_MESSAGE, _("and log created with strace: strace -f -o otpasswd_log <command you've tried>\n"));
		goto cleanup1;
	} else {
		ret = agent_hdr_recv(a);
		if (ret != 0) {
			/* This is an error visible when agent dies without being able
			 * to send any information back. Wrong executable etc.
			 */
			int status = 0;
			print(PRINT_ERROR, _("Error while reading initial data from agent: %s\n"), agent_strerror(ret));

			if (waitpid(a->pid, &status, WNOHANG) == a->pid) {
				print(PRINT_ERROR, _("Unable to start agent executable: %s\n"), agent_executable);
				if (WIFEXITED(status)) {
					int stat = WEXITSTATUS(status);
					print(PRINT_ERROR, _("Agent return value is: %d\n"), stat);
				}
			}

			print(PRINT_MESSAGE, _("Agent started but didn't sent any valid information back..\n"));
			print(PRINT_MESSAGE, _("Possible cause of this problem involves use of the wrong agent executable.\n"));
			print(PRINT_MESSAGE, _("Try manually running agent executable to see where's the problem.\n"));		
			print(PRINT_MESSAGE, _("If you would want to send a bug report remember about gdb backtrace\n"));		
			print(PRINT_MESSAGE, _("and log created with strace: strace -f -o otpasswd_log <command you've tried>\n"));
			goto cleanup1;
		}

		
		if (a->rhdr.type != AGENT_REQ_INIT) {
			print(PRINT_ERROR, _("Agent: Initial frame parsing error.\n"));
			print(PRINT_NOTICE, _("Agent: Header type equals %d instead of %d.\n"), a->rhdr.type, AGENT_REQ_INIT);
			ret = AGENT_ERR_SERVER_INIT;
			goto cleanup1;
		}

		ret = a->rhdr.status;
		if (ret == AGENT_ERR_INIT_EMERGENCY) {
			/* execl failed, we can show errno */
			print(PRINT_MESSAGE, _("There was an error when trying to run agent executable (%s)\n"), agent_executable);
			print(PRINT_MESSAGE, _("Check your installation and configuration.\n"));
			print(PRINT_MESSAGE, _("Probable cause: %s\n"), strerror(a->rhdr.int_arg));
			ret = AGENT_ERR_SERVER_INIT;
			goto cleanup1;
		} else if (ret == AGENT_ERR_INIT_CONFIGURATION) {
			print(PRINT_MESSAGE, _("Agent detected configuration problem: %s\n"), 
			      agent_strerror(a->rhdr.int_arg));
			print(PRINT_MESSAGE, _("Try running agent (agent_otp) with --check-config option to get more details\n"));
			goto cleanup1;
		} else if (ret == AGENT_ERR_INIT_PRIVILEGES) {
			print(PRINT_MESSAGE, _("Configuration problem was detected:\n"));
			print(PRINT_MESSAGE, _("DB=global option is set in config file but agent executable (agent_otp)\n"));
			print(PRINT_MESSAGE, _("doesn't have necessary SUID-root permissions.\n"));
			goto cleanup1;
		} else if (ret != 0) {
			print(PRINT_ERROR, _("Agent failed to initialize correctly: %s\n"), agent_strerror(ret));
			goto cleanup1;
		}
	}

	*a_out = a;
	return AGENT_OK;

cleanup1:
	if (in[0] != -1) close(in[0]);
	if (in[1] != -1) close(in[1]);
	if (out[0] != -1) close(out[0]);
	if (out[1] != -1) close(out[1]);

cleanup:
	free(a);
	return ret;
}


int agent_server(agent **a_out) 
{
	agent *a;

	*a_out = NULL;
	a = malloc(sizeof(*a));
	if (!a) {
		return AGENT_ERR_MEMORY;
	}
	memset(a, 0, sizeof(*a));

	a->username = NULL;
	a->shdr.protocol_version = AGENT_PROTOCOL_VERSION;
	a->s = NULL;
	a->new_state = 0;

	a->in = 0;
	a->out = 1;

	a->pid = -1;
	a->error = 0;

	*a_out = a;
	return AGENT_OK;
}

int agent_set_user(agent *a, const char *username)
{
	int ret;

	assert(username != NULL);
	if (a->username)
		free(a->username);
	a->username = strdup(username);

	if (a->pid == -1) {
		/* We are server. Don't inform anybody */
		return 0;
	}

	/* Inform agent */
	agent_hdr_init(a, 0);
	ret = agent_hdr_set_str(a, username);
	if (ret != AGENT_OK) {
		print(PRINT_CRITICAL, "Label too long to send, check limits in program.\n");
		return ret;
	}

	ret = agent_query(a, AGENT_REQ_USER_SET);
	return ret;
}

int agent_disconnect(agent *a)
{
	int tmp;
	int ret = 0;
	/* TODO: Send quit message if client
	 *       Wait for child to close? 
	 * Wait is not necessary, closing pipe will inform client 
	 * about disconnect.
	 */

	/* Close descriptors  */
	if (a->in != -1) {
		tmp = close(a->in);
		if (tmp != 0) {
			print_perror(PRINT_WARN, "Error while closing incoming descriptor:");
			ret = 1;
		}
	}

	if (a->out != -1) {
		tmp = close(a->out);
		if (tmp != 0) {
			print_perror(PRINT_WARN, "Error while closing outgoing descriptor:");
			ret += 1;
		}
	}

	if (a->username)
		free(a->username);

	if (a->s) {
		ppp_state_fini(a->s);
	}

	/* Free memory */
	memset(a, 0, sizeof(*a));
	free(a);

	return ret;
}

const char *agent_strerror(int error)
{
	switch (error) {
	case AGENT_OK:
		return _("No error");
	case AGENT_ERR:
		return _("Generic agent error.");

	case AGENT_ERR_REQ:
		return _("Coding error: Illegal request to agent.");
	case AGENT_ERR_REQ_ARG:
		return _("Coding error: Illegal request argument sent to agent.");

	case AGENT_ERR_INIT_CONFIGURATION:
		return _("Configuration error or unable to read configuration file.");
	case AGENT_ERR_INIT_PRIVILEGES:
		return _("Configuration problem: DB option set to 'global', but OTP agent is not SUID-root.");
	case AGENT_ERR_INIT_USER:
		return _("Unable to switch to selected user.");
	case AGENT_ERR_INIT_EXECUTABLE:
		return _("Unable to locate agent executable!");

	case AGENT_ERR_MEMORY:
		return _("Error while allocating memory.");
	case AGENT_ERR_SERVER_INIT:
		return _("Error during agent initialization.");
	case AGENT_ERR_PROTOCOL_MISMATCH:
		return _("Agent protocol mismatch. Reinstall software.");
	case AGENT_ERR_DISCONNECT:
		return _("Agent unexpectedly disconnected.");

	case AGENT_ERR_POLICY:
		return _("Requested action is denied by the policy.");
	case AGENT_ERR_POLICY_REGENERATION:
		return _("Key regeneration is denied by the policy.");
	case AGENT_ERR_POLICY_GENERATION:
		return _("Key generation is denied by the policy.");
	case AGENT_ERR_POLICY_DISABLED:
		return _("Changing disabled flag is denied by the policy.");

	case AGENT_ERR_POLICY_SALT:
		return _("Salt option setting is fixed by the policy.");
	case AGENT_ERR_POLICY_SHOW:
		return _("Show option setting is fixed by the policy.");


	case AGENT_ERR_MUST_CREATE_STATE:
		return _("Coding error: Must create state before generating key.");
	case AGENT_ERR_MUST_DROP_STATE:
		return _("Coding error: Must drop state before removing it.");
	case AGENT_ERR_NO_STATE:
		return _("Coding error: Action requires created/read state.");

	default:
		if (agent_is_agent_error(error))
			return _( ppp_get_error_desc(error) );
		return _( "Not an agent/PPP error." );
	}
	return NULL;
}

void agent_print_spass_errors(int errors) 
{
	if (errors & PPP_ERROR_SPASS_SHORT) {
		printf(_("Static password too short.\n"));
		errors &= ~PPP_ERROR_SPASS_SHORT;
	}
	if (errors & PPP_ERROR_SPASS_NO_DIGITS) {
		printf(_("Not enough digits in password.\n"));
		errors &= ~PPP_ERROR_SPASS_NO_DIGITS;
	}
	if (errors & PPP_ERROR_SPASS_NO_UPPERCASE) {
		printf(_("Not enough uppercase characters in password.\n"));
		errors &= ~PPP_ERROR_SPASS_NO_UPPERCASE;
	}
	if (errors & PPP_ERROR_SPASS_NO_SPECIAL) {
		printf(_("Not enough special characters in password.\n"));
		errors &= ~PPP_ERROR_SPASS_NO_SPECIAL;
	}
	if (errors & PPP_ERROR_SPASS_ILLEGAL_CHARACTER) {
		printf(_("Strange error indeed: Unsupported ascii character.\n"));
		errors &= ~PPP_ERROR_SPASS_ILLEGAL_CHARACTER;
	}
	if (errors & PPP_ERROR_SPASS_NON_ASCII) {
		printf(_("Non-ascii character in static password.\n"));
		errors &= ~PPP_ERROR_SPASS_NON_ASCII;
	}
	if (errors & PPP_ERROR_SPASS_POLICY) {
		printf(_("Static password change is denied by the policy.\n"));
		errors &= ~PPP_ERROR_SPASS_POLICY;
	}

	if (errors & PPP_ERROR_SPASS_SET) {
		printf(_("Static password set.\n"));
		errors &= ~PPP_ERROR_SPASS_SET;
	}
	if (errors & PPP_ERROR_SPASS_UNSET) {
		printf(_("Static password unset.\n"));
		errors &= ~PPP_ERROR_SPASS_UNSET;
	}

	if (errors) {
		print(PRINT_ERROR, 
		      "After printing all implemented messages bit-field still non-empty!\n"
		      "Value which last = %d\n", errors);
		assert(0);
	}
}

void agent_print_ppp_warnings(int warnings, int failures) 
{
	if (warnings & PPP_WARN_LAST_CARD) {
		printf(_("PPP WARNING: You've reached your last printed card.\n"));
		warnings &= ~PPP_WARN_LAST_CARD;
	}
	if (warnings & PPP_WARN_NOTHING_LEFT) {
		printf(_("PPP WARNING: You've ran out of printed passcards!\n"));
		warnings &= ~PPP_WARN_NOTHING_LEFT;
	}
	if (warnings & PPP_WARN_RECENT_FAILURES) {
		printf(_("PPP WARNING: There were %d recent failures.\n"), failures);
		warnings &= ~PPP_WARN_RECENT_FAILURES;
	}

	if (warnings) {
		print(PRINT_ERROR, 
		      "After printing all implemented warnings the bit-field still "
		      "is non-empty! \nValue which last = %d\n", warnings);
		assert(0);
	}
}


int agent_state_new(agent *a)
{
	return agent_query(a, AGENT_REQ_STATE_NEW);
}

int agent_state_load(agent *a)
{
	return agent_query(a, AGENT_REQ_STATE_LOAD);
}

int agent_state_store(agent *a)
{
	return agent_query(a, AGENT_REQ_STATE_STORE);
}

int agent_state_drop(agent *a)
{
	return agent_query(a, AGENT_REQ_STATE_DROP);
}



int agent_key_generate(agent *a)
{
	return agent_query(a, AGENT_REQ_KEY_GENERATE);
}

int agent_key_remove(agent *a)
{
	return agent_query(a, AGENT_REQ_KEY_REMOVE);
}


int agent_flag_add(agent *a, int flag)
{
	agent_hdr_init(a, 0);
	agent_hdr_set_int(a, flag, 0);
	return agent_query(a, AGENT_REQ_FLAG_ADD);
}

int agent_flag_clear(agent *a, int flag)
{
	agent_hdr_init(a, 0);
	agent_hdr_set_int(a, flag, 0);
	return agent_query(a, AGENT_REQ_FLAG_CLEAR);
}

int agent_flag_get(agent *a, int *flags)
{
	int ret;
	assert(flags != NULL);

	agent_hdr_init(a, 0);
	ret = agent_query(a, AGENT_REQ_FLAG_GET);
	if (ret != 0)
		return ret;
	*flags = agent_hdr_get_arg_int(a);
	return AGENT_OK;
}


int agent_get_num(agent *a, int field, num_t *num)
{
	int ret;
	agent_hdr_init(a, 0);
	agent_hdr_set_int(a, field, 0);

	ret = agent_query(a, AGENT_REQ_GET_NUM);
	if (ret != 0)
		return ret;
	*num = agent_hdr_get_arg_num(a);
	return AGENT_OK;
}

int agent_get_int(agent *a, int field, int *integer)
{
	int ret;
	agent_hdr_init(a, 0);
	agent_hdr_set_int(a, field, 0);

	ret = agent_query(a, AGENT_REQ_GET_INT);
	if (ret != 0)
		return ret;
	*integer = agent_hdr_get_arg_int(a);
	return AGENT_OK;
}

int agent_get_str(agent *a, int field, char **str)
{
	int ret;
	const char *tmp_str = NULL;
	assert(str != NULL);

	agent_hdr_init(a, 0);
	agent_hdr_set_int(a, field, 0);

	ret = agent_query(a, AGENT_REQ_GET_STR);
	if (ret != 0) {
		*str = NULL;
		return ret;
	}

	tmp_str = agent_hdr_get_arg_str(a);
	assert(tmp_str != NULL);

	*str = strdup(tmp_str);

	if (!*str) {
		return AGENT_ERR_MEMORY;
	}

	return AGENT_OK;

}

int agent_get_key(agent *a, unsigned char *key)
{
	int ret;
	const char *tmp_str = NULL;
	assert(key != NULL);

	agent_hdr_init(a, 0);
	agent_hdr_set_int(a, PPP_FIELD_KEY, 0);

	ret = agent_query(a, AGENT_REQ_GET_STR);
	if (ret != 0) {
		return ret;
	}

	tmp_str = agent_hdr_get_arg_str(a);
	assert(tmp_str != NULL);

	memcpy(key, tmp_str, 32);

	return AGENT_OK;
}


int agent_get_alphabet(agent *a, int id, const char **alphabet)
{
	int ret;
	assert(alphabet != NULL);

	agent_hdr_init(a, 0);
	agent_hdr_set_int(a, id, 0);

	ret = agent_query(a, AGENT_REQ_GET_ALPHABET);
	if (ret != 0 && ret != PPP_ERROR_POLICY) {
		*alphabet = NULL;
		return ret;
	}

	*alphabet = agent_hdr_get_arg_str(a);

	return ret;
}



/* Setters */
int agent_set_int(agent *a, int field, int integer)
{
	int ret;
	agent_hdr_init(a, 0);
	agent_hdr_set_int(a, field, integer);

	ret = agent_query(a, AGENT_REQ_SET_INT);
	if (ret != 0)
		return ret;
	else
		return AGENT_OK;
}

int agent_set_str(agent *a, int field, const char *str)
{
	int ret;
	agent_hdr_init(a, 0);
	agent_hdr_set_int(a, field, 0);
	ret = agent_hdr_set_str(a, str);
	if (ret != AGENT_OK) {
		print(PRINT_CRITICAL, "Label too long to send, check limits in program.\n");
		return ret;
	}

	ret = agent_query(a, AGENT_REQ_SET_STR);
	if (ret != 0)
		return ret;
	else
		return AGENT_OK;
}

int agent_set_spass(agent *a, const char *str, int remove_spass)
{
	int ret;
	agent_hdr_init(a, 0);
	agent_hdr_set_int(a, remove_spass ? 1 : 0, 0);

	if (remove_spass == 0) {
		int ret = agent_hdr_set_str(a, str);
		if (ret != AGENT_OK) {
			print(PRINT_CRITICAL, "Label too long to send, check limits in program.\n");
			return ret;
		}
	}

	ret = agent_query(a, AGENT_REQ_SET_SPASS);

	return ret;
}

int agent_get_warnings(agent *a, int *warnings, int *failures)
{
	int ret;
	agent_hdr_init(a, 0);
	agent_hdr_set_int(a, 0, 0);

	ret = agent_query(a, AGENT_REQ_GET_WARNINGS);
	*warnings = agent_hdr_get_arg_int(a);
	*failures = agent_hdr_get_arg_int2(a);

	return ret;
}

int agent_get_passcode(agent *a, const num_t counter, char *reply)
{
	int ret;
	const char *tmp_str = NULL;
	assert(reply != NULL);

	agent_hdr_init(a, 0);
	agent_hdr_set_num(a, &counter);

	ret = agent_query(a, AGENT_REQ_GET_PASSCODE);
	if (ret != AGENT_OK)
		return ret;

	tmp_str = agent_hdr_get_arg_str(a);
	assert(tmp_str != NULL);
	strncpy(reply, tmp_str, 16);
	reply[16] = '\0';
	return ret;
}

int agent_get_prompt(agent *a, const num_t counter, char **reply)
{
	int ret;
	const char *tmp_str = NULL;
	assert(reply != NULL);

	agent_hdr_init(a, 0);
	agent_hdr_set_num(a, &counter);

	ret = agent_query(a, AGENT_REQ_GET_PROMPT);
	if (ret != AGENT_OK)
		return ret;


	tmp_str = agent_hdr_get_arg_str(a);
	assert(tmp_str != NULL);
	*reply = strdup(tmp_str);
	if (!*reply)
		return AGENT_ERR_MEMORY;

	return ret;
}



int agent_authenticate(agent *a, const char *passcode)
{
	int ret;
	assert(passcode != NULL);

	agent_hdr_init(a, 0);

	ret = agent_hdr_set_str(a, passcode);
	if (ret != AGENT_OK) {
		print(PRINT_CRITICAL, "Passcode too long to send to agent.\n");
		return ret;
	}

	ret = agent_query(a, AGENT_REQ_AUTHENTICATE);
	return ret;
}

int agent_skip(agent *a, const num_t counter)
{
	int ret;

	agent_hdr_init(a, 0);

	agent_hdr_set_num(a, &counter);
	ret = agent_query(a, AGENT_REQ_SKIP);
	return ret;
}

int agent_update_latest_card(agent *a, const num_t latest_card)
{
	int ret;

	agent_hdr_init(a, 0);

	agent_hdr_set_num(a, &latest_card);
	ret = agent_query(a, AGENT_REQ_UPDATE_LATEST);
	return ret;
}

int agent_clear_recent_failures(agent *a)
{
	int ret;

	agent_hdr_init(a, 0);
	ret = agent_query(a, AGENT_REQ_CLEAR_RECENT_FAILURES);
	return ret;
}



