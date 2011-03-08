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


#include "common.h"


extern xmlNodePtr	keychain;
extern char		*locale;

#ifdef _READLINE
xmlChar                 *_rl_helper_var = NULL;
#endif


xmlNodePtr
find_keychain(xmlChar *cname)
{
	xmlNodePtr	db_node = NULL;

	char		*inv = NULL;
	int		idx = -1, i = 0;


	// if we got a number
	idx = strtol((const char *)cname, &inv, 10);
	if (strncmp(inv, "\0", 1) != 0) {
		idx = -1;
	}


	db_node = keychain->parent->children;

	while (db_node) {
		if (db_node->type == XML_ELEMENT_NODE) {	// we only care about ELEMENT nodes
			if (idx >= 0) {		// if an index number was given in the parameter
				if (i++ == idx) {
					break;
				}
			} else {		// if keychain name was given in the parameter
				if (xmlUTF8Charcmp(xmlGetProp(db_node, BAD_CAST "name"), cname) == 0) {
					break;
				}
			}
		}

		db_node = db_node->next;
	}

	return(db_node);
} /* find_keychain() */


xmlNodePtr
find_key(int idx)
{
	xmlNodePtr	db_node = NULL;

	int		i = -1;


	db_node = keychain->children;

	while (db_node  &&  i < idx) {
		if (db_node->type == XML_ELEMENT_NODE)	// we only care about ELEMENT nodes
			i++;

		if (i != idx)	// if we've found it, don't jump to the next sibling
			db_node = db_node->next;
	}

	return(db_node);
} /* find_key */


char *
parse_newlines(char *line, char dir)		// dir(direction) convert "\n" to '\n' (dir=0), or backwards
{
	char		*ret = NULL;
	int		nlnum = 0, i = 0, j = 0, ret_len = 0;


	if (!line)
		return(strdup(""));


	if (dir) {
		// count the number of '\n' characters in the string, and use it later to figure how many bytes
		// will be the new string, with replaced newline characters.
		for (i=0; i < strlen(line); i++)
			if (line[i] == '\n')	// we got a winner...
				nlnum++;

		ret_len = strlen(line) + nlnum + 1;
	} else {
		// count the number of "\n" sequences in the string, and use it later to figure how many bytes
		// will be the new string, with replaced newline sequences.
		for (i=0; i < strlen(line); i++)
			if (line[i] == '\\'  &&  line[i+1] == '\\')	// the "\\n" case. the newline is escaped, so honor it
				i += 2;					// skip these. don't count them, because they are not newlines
			else
			if (line[i] == '\\'  &&  line[i+1] == 'n')	// we got a winner...
				nlnum++;

		ret_len = strlen(line) - nlnum + 1;
	}
	ret = malloc(ret_len); malloc_check(ret);


	if (dir) {
		// replace the real newline characters with newline sequences ("\n");
		for (i=0; i < strlen(line); i++) {
			if (line[i] == '\n') {			// we got a winner...
				ret[j++] = '\\';		// replace with NL character
				ret[j++] = 'n';			// replace with NL character
			} else
				ret[j++] = line[i];			// anything else will just go into the new string
		}
	} else {
		// replace the newline sequences with real newline characters ('\n');
		for (i=0; i < strlen(line); i++) {
			if (line[i] == '\\'  &&  line[i+1] == '\\') {	// the "\\n" case. the newline is escaped, so honor it
				ret[j++] = line[i];			// copy it as if nothing had happened
				ret[j++] = line[++i];
			} else
			if (line[i] == '\\'  &&  line[i+1] == 'n' ) {	// we got a winner...
				ret[j++] = '\n';			// replace with NL character
				i++;					// skip the 'n' char from "\n"
			} else
				ret[j++] = line[i];			// anything else will just go into the new string
		}
	}

	ret[ret_len - 1] = '\0';		// close that new string safe and secure.


	return(ret);	// return the result; we've worked on it hard.
} /* parse_newlines() */


int
digit_length(int idx)
{
	int	length = 1;


	while ((idx / 10) != 0) {
		idx /= 10;
		length++;
	}

	return(length);
} /* digit_length() */


xmlChar *
convert_utf8(char *string, char dir)		// 'dir'=direction, 0: locale=>utf8, 1: utf8=>locale
{
	iconv_t		iconv_ctx;
	size_t		conv_bytes = 0;
	size_t		out_size = 0;
	size_t		in_left = 0, out_left = 0;
	char		*out_ptr = NULL, *out_str = NULL, *in_ptr = NULL, *in_str = NULL;
	int		ret = 0;


	if (!string)
		return(xmlCharStrdup(""));


	in_str = strdup(string);
	in_ptr = in_str;

	if (strcmp(locale, "UTF-8") == 0)	// if there is no need to convert
		return(BAD_CAST in_str);


	if (dir)
		iconv_ctx = iconv_open(locale, "UTF-8");
	else
		iconv_ctx = iconv_open("UTF-8", locale);

	if (iconv_ctx < 0) {
		perror("character conversion error (iconv_open())");
		return(BAD_CAST in_str);
	}

	out_size = strlen(in_str) * 4;		// I don't care about this shitty, f*cked up library
	out_str = malloc(out_size + 1); malloc_check(out_str);
	out_ptr = out_str;
	out_left = out_size;
	in_left = strlen(in_str);
	while (in_left != 0) {
		ret = iconv(iconv_ctx, &in_ptr, &in_left, &out_ptr, &out_left);

		if (ret < 0) {
			switch (errno) {
				case E2BIG:
				case EINVAL:
				case EILSEQ:
					// I don't care about this shitty, f*cked up library
					printf("character conversion error (iconv()): '%s' to 'UTF-8'\n", locale);
					perror("iconv()");
					return(BAD_CAST in_str);
				break;
				default:
				break;
			}
		}
	}

	conv_bytes = out_size - out_left;
	out_str[conv_bytes] = '\0';

	if (debug)
		printf("string converted to UTF-8 (%zd bytes)\n", conv_bytes);


	if (iconv_close(iconv_ctx) != 0) {
		perror("iconv_close()");
		return(BAD_CAST in_str);
	}


	return(BAD_CAST out_str);
} /* convert_to_utf8() */


#ifdef _READLINE
void
_rl_push_buffer(void)
{
	rl_replace_line((const char *)_rl_helper_var, 0);
	rl_redisplay();
} /* _rl_push_buffer */
#endif
