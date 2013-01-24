/*
 * Copyright (c) 2011, 2012, 2013 LEVAI Daniel
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *	* Redistributions of source code must retain the above copyright
 *	notice, this list of conditions and the following disclaimer.
 *	* Redistributions in binary form must reproduce the above copyright
 *	notice, this list of conditions and the following disclaimer in the
 *	documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL LEVAI Daniel BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/


#include "common.h"
#include "commands.h"


extern xmlDocPtr	db;
extern xmlNodePtr	keychain;

#ifndef _READLINE
extern EditLine		*e;
#endif

extern char		batchmode;


void
cmd_list(const char *e_line, command *commands)
{
	xmlNodePtr	db_node = NULL, list_keychain = NULL;
	xmlChar		*key = NULL, *cname = NULL;

	int		idx = 0;

	char		*line = NULL;
	char		pager = 0, *cmd = NULL, rc = 0;


	if (getenv("KC_DEBUG")) {
		xmlSaveFormatFileEnc("-", db, "UTF-8", XML_SAVE_FORMAT);
		printf("#BEGIN\n");
	}

	line = strdup(e_line);

	cmd = strtok(line, " ");			/* get the command name */
	if (	strcmp(cmd, "plist") == 0  ||
		strcmp(cmd, "pls") == 0)
		pager = 20;

	cname = BAD_CAST strtok(NULL, " ");		/* assign the command's parameter */
	if (cname) {
		list_keychain = find_keychain(cname, 0);	/* list the specified keychain */
		if (!list_keychain) {
			puts("Keychain not found.");
			free(line); line = NULL;
			return;
		}
	} else
		list_keychain = keychain;		/* list the current keychain */

	if (!batchmode  &&  pager) {
#ifndef _READLINE
		/* clear the prompt temporarily */
		if (el_set(e, EL_PROMPT, el_prompt_null) != 0) {
			perror("el_set(EL_PROMPT)");
		}
		if (el_set(e, EL_UNBUFFERED, 1) != 0) {
			perror("el_set(EL_UNBUFFERED)");
			return;
		}
#else
		rl_prep_terminal(1);
#endif
	}

	db_node = list_keychain->children;
	while (db_node) {
		if (db_node->type == XML_ELEMENT_NODE) {	/* we only care about ELEMENT nodes */
			key = xmlGetProp(db_node, BAD_CAST "name");
			printf("%d. %s\n", idx++, key);
			xmlFree(key); key = NULL;

			/* pager */
			if (	!batchmode  &&  pager  &&
				(idx % pager == 0)) {

				rc = 0;
				while (	rc != ' '  &&  rc != 13  &&
					rc != 4  &&  rc != 'q') {
#ifndef _READLINE
					el_getc(e, &rc);
#else
					rc = rl_read_key();
#endif
				}
				if (rc == 4  ||  rc == 'q')
					break;
			}
		}

		db_node = db_node->next;
	}

	if (!batchmode  &&  pager) {
#ifndef _READLINE
		/* re-enable the default prompt */
		if (el_set(e, EL_PROMPT, prompt_str) != 0) {
			perror("el_set(EL_PROMPT)");
		}
		el_set(e, EL_UNBUFFERED, 0);
#else
		rl_deprep_terminal();
#endif
	}

	if (idx == 0)
		puts("Empty keychain.");

	free(line); line = NULL;

	if (getenv("KC_DEBUG"))
		printf("#END\n");
} /* cmd_list() */
