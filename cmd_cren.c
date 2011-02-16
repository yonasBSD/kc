/*
 * Copyright (c) 2011 LEVAI Daniel
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


#include <stdarg.h>

#include "common.h"
#include "commands.h"


extern xmlNodePtr	keychain;
extern char		dirty;


void
cmd_cren(EditLine *e, ...)
{
	va_list		ap;

	History 	*eh = NULL;

	xmlNodePtr	db_node = NULL;
	xmlChar		*cname_locale = NULL, *cname = NULL;

	command		*commands = NULL;

	const char	*e_line = NULL;
	char		*line = NULL;
	int		e_count = 0;


	va_start(ap, e);

	line = va_arg(ap, char *);
	line[strlen(line) - 1] = '\0';		/* remove the newline character from the end */

	eh = va_arg(ap, History *);
	va_arg(ap, BIO *);
	commands = va_arg(ap, command *);

	va_end(ap);

	strtok(line, " ");	/* remove the command from the line */
	cname_locale = BAD_CAST strtok(NULL, " ");	/* assign the command's parameter */
	if (!cname_locale) {
		puts(commands->usage);
		return;
	}

	cname = convert_utf8(cname_locale, 0);

	db_node = find_keychain(cname);
	if (db_node) {
		// disable history temporarily
		if (el_set(e, EL_HIST, history, NULL) != 0) {
			perror("el_set(EL_HIST)");
		}


		// set the new edit prompt
		if (el_set(e, EL_CLIENTDATA, "RENAME keychain") != 0) {
			perror("el_set(EL_CLIENTDATA)");
		}
		// if we edit an existing entry, push the current value to the edit buffer
		free(cname);
		cname = xmlGetProp(db_node, BAD_CAST "name");
		cname_locale = convert_utf8(cname, 1);
		el_push(e, (const char *)cname_locale);
		free(cname_locale); cname_locale = NULL;

		e_line = el_gets(e, &e_count);
		if (!e_line) {
			perror("input");
			return;
		} else
			cname_locale = BAD_CAST e_line;

		cname_locale[xmlStrlen(cname_locale) - 1] = '\0';		// remove the newline character from the end
		cname = convert_utf8(cname_locale, 0);


		xmlSetProp(db_node, BAD_CAST "name", cname);

		free(cname);

		// change back to the default prompt
		if (el_set(e, EL_CLIENTDATA, "") != 0) {
			perror("el_set(EL_CLIENTDATA)");
		}
		// re-enable history
		if (el_set(e, EL_HIST, history, eh) != 0) {
			perror("el_set(EL_HIST)");
		}

		dirty = 1;
	} else {
		printf("keychain '%s' not found.\n", cname_locale);
		free(cname);
	}
} /* cmd_cren() */
