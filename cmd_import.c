/*
 * Copyright (c) 2011-2014 LEVAI Daniel
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
 * DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDERS AND CONTRIBUTORS BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/


#include "common.h"
#include "commands.h"
#include "ssha.h"

#ifdef BSD
#include <fcntl.h>
#endif

#if defined(__linux__)  ||  defined(__CYGWIN__)
#include <sys/file.h>
#endif


extern db_parameters	db_params;
extern xmlDocPtr	db;
extern xmlNodePtr	keychain;


void
cmd_import(const char *e_line, command *commands)
{
	BIO		*bio_chain = NULL;

	struct stat	st;
	char		*ssha_type = NULL, *ssha_comment = NULL;

	db_parameters	db_params_new;

	xmlDocPtr	db_new = NULL;
	xmlNodePtr	db_root = NULL, db_root_new = NULL, entry_new = NULL,
			keychain_new = NULL, keychain_cur = NULL;
	xmlChar		*cname = NULL;

	int		c = 0, largc = 0;
	char		**largv = NULL;
	char		*line = NULL;
	char		*buf = NULL;
	char		append = 0, xml = 0;
	ssize_t		ret = -1;
	int		pos = 0;

	unsigned long int	count_keychains = 0, count_keys = 0, count_keys_new = 0;


	/* initial db_params parameters of the imported database */
	db_params_new.ssha_type[0] = '\0';
	db_params_new.ssha_comment[0] = '\0';
	db_params_new.pass = NULL;
	db_params_new.pass_len = 0;
	db_params_new.db_filename = NULL;
	db_params_new.db_file = -1;
	db_params_new.pass_filename = NULL;
	db_params_new.kdf = strdup(db_params.kdf);
	if (!db_params_new.kdf) {
		perror("Could not duplicate the KDF");
		goto exiting;
	}
	db_params_new.cipher = strdup(db_params.cipher);
	if (!db_params_new.cipher) {
		perror("Could not duplicate the cipher");
		goto exiting;
	}
	db_params_new.cipher_mode = strdup(db_params.cipher_mode);
	if (!db_params_new.cipher_mode) {
		perror("Could not duplicate the cipher mode");
		goto exiting;
	}
	db_params_new.dirty = 0;
	db_params_new.readonly = 0;


	/* Parse the arguments */
	line = strdup(e_line);
	if (!line) {
		perror("Could not duplicate the command line");
		goto exiting;
	}
	larg(line, &largv, &largc);
	free(line); line = NULL;

	optind = 0;
	while ((c = getopt(largc, largv, "A:k:P:e:m:")) != -1)
		switch (c) {
			case 'A':
				/* in case this parameter is being parsed multiple times */
				free(ssha_type); ssha_type = NULL;
				free(ssha_comment); ssha_comment = NULL;

				ssha_type = strndup(strsep(&optarg, ","), 11);
				if (ssha_type == NULL  ||  !strlen(ssha_type)) {
					dprintf(STDERR_FILENO, "SSH key type is empty!\n");
					goto exiting;
				}
				if (	strncmp(ssha_type, "ssh-rsa", 7) != 0  &&
					strncmp(ssha_type, "ssh-ed25519", 11) != 0
				) {
					dprintf(STDERR_FILENO, "SSH key type is unsupported: '%s'\n", ssha_type);
					goto exiting;
				}

				ssha_comment = strndup(optarg, 512);
				if (ssha_comment == NULL  ||  !strlen(ssha_comment)) {
					dprintf(STDERR_FILENO, "SSH key comment is empty!\n");
					goto exiting;
				}

				if (strlcpy(db_params_new.ssha_type, ssha_type, sizeof(db_params_new.ssha_type)) >= sizeof(db_params_new.ssha_type)) {
					dprintf(STDERR_FILENO, "Error while getting SSH key type.\n");
					goto exiting;
				}

				if (strlcpy(db_params_new.ssha_comment, ssha_comment, sizeof(db_params_new.ssha_comment)) >= sizeof(db_params_new.ssha_comment)) {
					dprintf(STDERR_FILENO, "Error while getting SSH key type.\n");
					goto exiting;
				}

				printf("Using (%s) %s identity for decryption\n", db_params_new.ssha_type, db_params_new.ssha_comment);
			break;
			case 'k':
				free(db_params_new.db_filename); db_params_new.db_filename = NULL;
				db_params_new.db_filename = strdup(optarg);
				if (!db_params_new.db_filename) {
					perror("Could not duplicate the database file name");
					goto exiting;
				}
			break;
			case 'P':
				free(db_params_new.kdf); db_params_new.kdf = NULL;
				db_params_new.kdf = strdup(optarg);
				if (!db_params_new.kdf) {
					perror("Could not duplicate the KDF");
					goto exiting;
				}
			break;
			case 'e':
				free(db_params_new.cipher); db_params_new.cipher = NULL;
				db_params_new.cipher = strdup(optarg);
				if (!db_params_new.cipher) {
					perror("Could not duplicate the cipher");
					goto exiting;
				}
			break;
			case 'm':
				free(db_params_new.cipher_mode); db_params_new.cipher_mode = NULL;
				db_params_new.cipher_mode = strdup(optarg);
				if (!db_params_new.cipher_mode) {
					perror("Could not duplicate the cipher mode");
					goto exiting;
				}
			break;
			default:
				puts(commands->usage);
				goto exiting;
			break;
		}

	if (strncmp(largv[0], "append", 6) == 0)	/* command is 'append' or 'appendxml' */
		append = 1;

	if (strcmp(largv[0] + 6, "xml") == 0)
		xml = 1;


	if (!db_params_new.db_filename)
		goto exiting;


	puts("Reading database...");


	if (xml) {
		/* plain text XML database import */

		if (getenv("KC_DEBUG"))
			db_new = xmlReadFile(db_params_new.db_filename, "UTF-8", XML_PARSE_NONET | XML_PARSE_PEDANTIC | XML_PARSE_RECOVER);
		else
			db_new = xmlReadFile(db_params_new.db_filename, "UTF-8", XML_PARSE_NONET | XML_PARSE_NOERROR | XML_PARSE_NOWARNING);

		if (!db_new) {
			xmlGenericError(xmlGenericErrorContext, "Failed to parse XML from '%s'", db_params_new.db_filename);
			if (errno == 0)
				puts("");
			else
				printf(": %s\n", strerror(errno));

			goto exiting;
		}
	} else {
		/* encrypted database import */

		/* This should be identical of what is in kc.c */
		if (stat(db_params_new.db_filename, &st) == 0) {

			printf("Opening '%s'\n",db_params_new.db_filename);

			if (!S_ISLNK(st.st_mode)  &&  !S_ISREG(st.st_mode)) {
				printf("'%s' is not a regular file or a link!\n", db_params_new.db_filename);

				goto exiting;
			}

			if (st.st_size == 0) {
				printf("'%s' is an empty file!\n", db_params_new.db_filename);

				goto exiting;
			}

			if (st.st_size <= IV_DIGEST_LEN + SALT_DIGEST_LEN + 2) {
				printf("'%s' is suspiciously small file!\n", db_params_new.db_filename);

				goto exiting;
			}

			db_params_new.db_file = open(db_params_new.db_filename, O_RDONLY);
			if (db_params_new.db_file < 0) {
				perror("open(database file)");

				goto exiting;
			}

			/* read the IV */
			pos = 0;
			do {
				ret = read(db_params_new.db_file, db_params_new.iv + pos, IV_DIGEST_LEN - pos);
				pos += ret;
			} while (ret > 0  &&  pos < IV_DIGEST_LEN);
			db_params_new.iv[pos] = '\0';

			if (ret < 0) {
				perror("import: read IV(database file)");

				goto exiting;
			}
			if (pos != IV_DIGEST_LEN) {
				puts("Could not read IV from database file!");

				goto exiting;
			}

			if (getenv("KC_DEBUG"))
				printf("%s(): import: iv='%s'\n", __func__, db_params_new.iv);

			/* Fast-forward one byte after the current position,
			 * to skip the newline.
			 */
			lseek(db_params_new.db_file, 1, SEEK_CUR);

			/* read the salt */
			pos = 0;
			do {
				ret = read(db_params_new.db_file, db_params_new.salt + pos, SALT_DIGEST_LEN - pos);
				pos += ret;
			} while (ret > 0  &&  pos < SALT_DIGEST_LEN);
			db_params_new.salt[pos] = '\0';

			if (ret < 0) {
				perror("import: read salt(database file)");

				goto exiting;
			}
			if (pos != SALT_DIGEST_LEN) {
				puts("Could not read salt from database file!");

				goto exiting;
			}

			if (getenv("KC_DEBUG"))
				printf("%s(): import: salt='%s'\n", __func__, db_params_new.salt);
		} else {
			perror("Could not open database file");

			goto exiting;
		}

		bio_chain = kc_setup_bio_chain(db_params_new.db_filename, 0);
		if (!bio_chain) {
			puts("Could not setup bio_chain!");

			goto exiting;
		}

		/* get the password */
		if (strlen(db_params_new.ssha_type)) {
			/* use OpenSSH agent to generate the password */
			if (!kc_ssha_get_password(&db_params_new))
				goto exiting;
		} else {
			/* ask for the password */
			if (kc_password_read(&db_params_new, 0) != 1)
				goto exiting;
		}

		/* Setup cipher mode and turn on decrypting */
		ret = kc_crypt_key(&db_params_new)  &&  kc_crypt_setup(bio_chain, 0, &db_params_new);

		/* from here on now, we don't need to store the key or the password text anymore */
		memset(db_params_new.key, '\0', KEY_LEN);

		if (db_params_new.pass)
			memset(db_params_new.pass, '\0', db_params_new.pass_len);
		free(db_params_new.pass); db_params_new.pass = NULL;

		/* kc_crypt_key() and kc_crypt_setup() check from a few lines above */
		if (!ret) {
			puts("Could not setup decrypting!");

			goto exiting;
		}


		ret = kc_db_reader(&buf, bio_chain);
		if (getenv("KC_DEBUG"))
			printf("%s(): read %d bytes\n", __func__, (int)ret);

		if (BIO_get_cipher_status(bio_chain) == 0  &&  ret > 0) {
			puts("Failed to decrypt import file!");

			free(buf); buf = NULL;
			goto exiting;
		}

		BIO_free_all(bio_chain); bio_chain = NULL;

		if (getenv("KC_DEBUG"))
			db_new = xmlReadMemory(buf, (int)ret, NULL, "UTF-8", XML_PARSE_NONET | XML_PARSE_PEDANTIC | XML_PARSE_RECOVER);
		else
			db_new = xmlReadMemory(buf, (int)ret, NULL, "UTF-8", XML_PARSE_NONET | XML_PARSE_NOERROR | XML_PARSE_NOWARNING);

		free(buf); buf = NULL;

		if (!db_new) {
			puts("Could not parse XML document!");

			goto exiting;
		}
	}


	puts("Checking database...");


	if (!kc_validate_xml(db_new)) {
		printf("Not a valid kc XML structure ('%s')!\n", db_params_new.db_filename);

		xmlFreeDoc(db_new);
		goto exiting;
	}


	db_root = xmlDocGetRootElement(db);		/* the existing db root */
	db_root_new = xmlDocGetRootElement(db_new);	/* the new db root */
	if (db_root_new->children->next == NULL) {
		puts("Cannot import an empty database!");

		xmlFreeDoc(db_new);
		goto exiting;
	}


	puts("Counting keys and keychains...");

	if (append) {
		/* Extract the keychain from the document being appended */
		keychain_new = db_root_new->children->next;

		/* Count the keychains in the current database */
		count_keychains = count_elements(keychain->parent->children);

		/* We would like to append every keychain that is in the source database,
		 * hence the loop.
		 */
		while (keychain_new) {
			if (keychain_new->type == XML_ELEMENT_NODE) {	/* we only care about ELEMENT nodes */
				cname = xmlGetProp(keychain_new, BAD_CAST "name");
				keychain_cur = find_keychain(cname, 1);	/* search for an existing keychain name in the current db */
				xmlFree(cname); cname = NULL;

				/* If an existing keychain name is encountered,
				 * append the entries from the imported keychain to
				 * the existing keychain, and don't add a duplicate
				 * keychain.
				 */
				if (keychain_cur) {
					/* Range check
					 * See, if the new keys would fit into the existing keychain.
					 */
					count_keys = count_elements(keychain_cur->children->next);
					count_keys_new = count_elements(keychain_new->children->next);
					while (count_keys_new > 0  &&  count_keys < ITEMS_MAX - 1) {
						count_keys++;
						count_keys_new--;
					}

					if (count_keys_new == 0  &&  count_keys <= ITEMS_MAX - 1) {
						entry_new = keychain_new->children->next;
						while (entry_new) {
							if (entry_new->type == XML_ELEMENT_NODE) {	/* we only care about ELEMENT nodes */
								xmlAddChild(keychain_cur, xmlNewText(BAD_CAST "\t"));
								xmlAddChild(keychain_cur, xmlCopyNode(entry_new, 1));
								xmlAddChild(keychain_cur, xmlNewText(BAD_CAST "\n\t"));
							}

							entry_new = entry_new->next;
						}
					} else {
						printf("Keys from keychain '%s' would not fit in the existing keychain; did not append.\n", xmlGetProp(keychain_new, BAD_CAST "name"));
					}
				} else {
					/* Range check
					 * See, if the new keychain would fit into the current database.
					 */
					if (count_keychains + 1 <= ITEMS_MAX - 1) {
						/* Create a non-existing keychain */
						xmlAddChild(db_root, xmlNewText(BAD_CAST "\t"));
						xmlAddChild(db_root, xmlCopyNode(keychain_new, 1));
						xmlAddChild(db_root, xmlNewText(BAD_CAST "\n"));

						count_keychains++;
					} else {
						printf("Can not create new keychain: maximum number of keychains reached, %lu.\n", ITEMS_MAX - 1);
					}
				}
			}

			keychain_new = keychain_new->next;
		}

		xmlFreeDoc(db_new);

		puts("Append finished.");
	} else {
		/* Range checks */

		keychain_new = db_root_new->children->next;

		/* Iterate through keychains and count the keys in them */
		while (keychain_new  &&  count_keychains < ITEMS_MAX) {
			/* Count the keys in the keychain */
			if (count_elements(keychain_new->children) >= ITEMS_MAX - 1) {
				printf("Cannot import: maximum number of keys reached, %lu.\n", ITEMS_MAX - 1);

				xmlFreeDoc(db_new);
				goto exiting;
			}

			if (keychain_new->type == XML_ELEMENT_NODE)	/* we only care about ELEMENT nodes */
				count_keychains++;

			keychain_new = keychain_new->next;
		}

		/* Finished scanning the keys in the new keychains, now lets evaluate the number of keychains */
		if (count_keychains >= ITEMS_MAX - 1) {
			printf("Cannot import: maximum number of keychains reached, %lu.\n", ITEMS_MAX - 1);

			xmlFreeDoc(db_new);
			goto exiting;
		}


		keychain = db_root_new->children->next;

		xmlFreeDoc(db);
		db = db_new;

		puts("Import finished.");
	}

	db_params.dirty = 1;

exiting:
	for (c = 0; c <= largc; c++) {
		free(largv[c]); largv[c] = NULL;
	}
	free(largv); largv = NULL;

	free(ssha_type); ssha_type = NULL;
	free(ssha_comment); ssha_comment = NULL;

	if (bio_chain) {
		BIO_free_all(bio_chain);
		bio_chain = NULL;
	}

	if (db_params_new.db_file >= 0)
		close(db_params_new.db_file);

	if (db_params_new.pass) {
		memset(db_params_new.pass, '\0', db_params_new.pass_len);
		free(db_params_new.pass); db_params_new.pass = NULL;
	}
	free(db_params_new.kdf); db_params_new.kdf = NULL;
	free(db_params_new.cipher); db_params_new.cipher = NULL;
	free(db_params_new.cipher_mode); db_params_new.cipher_mode = NULL;
	free(db_params_new.db_filename); db_params_new.db_filename = NULL;
} /* cmd_import() */
