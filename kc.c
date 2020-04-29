/*
 * Copyright (c) 2011-2020 LEVAI Daniel
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
#ifdef _HAVE_YUBIKEY
#include "ykchalresp.h"
#endif

#include <signal.h>
#include <sys/types.h>

#ifdef BSD
#include <fcntl.h>
#endif

#if defined(__linux__)  ||  defined(__CYGWIN__)
#include <sys/file.h>
#endif


void		print_bio_chain(BIO *);
#ifndef _READLINE
unsigned char	el_tab_complete(EditLine *);
#else
char		*cmd_generator(const char *, int);
#endif


db_parameters	db_params;
BIO		*bio_chain = NULL;

command		*commands_first = NULL;

xmlDocPtr	db = NULL;
xmlNodePtr	keychain = NULL;
char		*keychain_start = NULL;
char		keychain_start_name = 0;

unsigned char	batchmode = 0;

#ifndef _READLINE
EditLine	*e = NULL;
History		*eh = NULL;
HistEvent	eh_ev;
#endif

char		prompt_context[30];


int
main(int argc, char *argv[])
{
#ifndef _READLINE
	int		e_count = 0;
	const char	*e_line = NULL;
#else
	char		*e_line = NULL;
#endif
	char		*line = NULL;

	char		*created = NULL;

	char		*buf = NULL;
	ssize_t		ret = -1;
	int		pos = 0;
	int		pass_file = -1;

	struct stat	st;
	char		*env_home = NULL;
	unsigned char	newdb = 0;
	char		*ssha_type = NULL, *ssha_comment = NULL;

	unsigned long int	ykchalresp = 0;
	char			*inv = NULL;

	xmlNodePtr	db_root = NULL;

	int		c = 0;
	size_t		len = 0;

	unsigned long int	count_keychains = 0, count_keys = 0;


#ifdef __OpenBSD__
	char		*pledges = "cpath exec fattr flock proc rpath stdio tty unix wpath";


	if (getenv("KC_DEBUG"))
	       printf("%s(): Pledging for '%s'\n", __func__, pledges);

	if (pledge(pledges, NULL) != 0) {
	       perror("ERROR: Unable to pledge()!");
	       quit(EXIT_FAILURE);
	}
#endif


	/* db_param defaults */
	db_params.ssha_type[0] = '\0';
	db_params.ssha_comment[0] = '\0';
	db_params.ykdev = 0;
	db_params.ykslot = 0;
	db_params.pass = NULL;
	db_params.pass_len = 0;
	db_params.db_filename = NULL;
	db_params.db_file = -1;
	db_params.pass_filename = NULL;
	db_params.dirty = 0;
	db_params.readonly = 0;

	len = strlen(DEFAULT_KDF) + 1;
	db_params.kdf = malloc(len); malloc_check(db_params.kdf);
	if (strlcpy(db_params.kdf, DEFAULT_KDF, len) >= len) {
		dprintf(STDERR_FILENO, "ERROR: Error while setting up default database parameters.\n");
		quit(EXIT_FAILURE);
	}

	len = strlen(DEFAULT_CIPHER) + 1;
	db_params.cipher = malloc(len); malloc_check(db_params.cipher);
	if (strlcpy(db_params.cipher, DEFAULT_CIPHER, len) >= len) {
		dprintf(STDERR_FILENO, "ERROR: Error while setting up default database parameters.\n");
		quit(EXIT_FAILURE);
	}

	len = strlen(DEFAULT_MODE) + 1;
	db_params.cipher_mode = malloc(len); malloc_check(db_params.cipher_mode);
	if (strlcpy(db_params.cipher_mode, DEFAULT_MODE, len) >= len) {
		dprintf(STDERR_FILENO, "ERROR: Error while setting up default database parameters.\n");
		quit(EXIT_FAILURE);
	}


	while ((c = getopt(argc, argv, "A:k:c:C:rp:P:e:m:y:bBvh")) != -1)
		switch (c) {
			case 'A':
				/* in case this parameter is being parsed multiple times */
				free(ssha_type); ssha_type = NULL;
				free(ssha_comment); ssha_comment = NULL;

				ssha_type = strndup(strsep(&optarg, ","), 11);
				if (ssha_type == NULL  ||  !strlen(ssha_type)) {
					dprintf(STDERR_FILENO, "ERROR: SSH key type is empty!\n");
					quit(EXIT_FAILURE);
				}
				if (	strncmp(ssha_type, "ssh-rsa", 7) != 0  &&
					strncmp(ssha_type, "ssh-ed25519", 11) != 0
				) {
					dprintf(STDERR_FILENO, "ERROR: SSH key type is unsupported: '%s'\n", ssha_type);
					quit(EXIT_FAILURE);
				}

				ssha_comment = strndup(optarg, 512);
				if (ssha_comment == NULL  ||  !strlen(ssha_comment)) {
					dprintf(STDERR_FILENO, "ERROR: SSH key comment is empty!\n");
					quit(EXIT_FAILURE);
				}

				if (strlcpy(db_params.ssha_type, ssha_type, sizeof(db_params.ssha_type)) >= sizeof(db_params.ssha_type)) {
					dprintf(STDERR_FILENO, "ERROR: Error while getting SSH key type.\n");
					quit(EXIT_FAILURE);
				}
				free(ssha_type); ssha_type = NULL;

				if (strlcpy(db_params.ssha_comment, ssha_comment, sizeof(db_params.ssha_comment)) >= sizeof(db_params.ssha_comment)) {
					dprintf(STDERR_FILENO, "ERROR: Error while getting SSH key comment.\n");
					quit(EXIT_FAILURE);
				}
				free(ssha_comment); ssha_comment = NULL;

				printf("Using (%s) %s identity for decryption\n", db_params.ssha_type, db_params.ssha_comment);
			break;
			case 'k':
				db_params.db_filename = optarg;
			break;
			case 'c':
				keychain_start = optarg;
			break;
			case 'C':
				keychain_start = optarg;
				keychain_start_name = 1;
			break;
			case 'r':
				db_params.readonly = 1;
			break;
			case 'p':
				db_params.pass_filename = optarg;
			break;
			case 'P':
				free(db_params.kdf); db_params.kdf = NULL;
				db_params.kdf = strdup(optarg); malloc_check(db_params.kdf);
			break;
			case 'e':
				free(db_params.cipher); db_params.cipher = NULL;
				db_params.cipher = strdup(optarg); malloc_check(db_params.cipher);
			break;
			case 'm':
				free(db_params.cipher_mode); db_params.cipher_mode = NULL;
				db_params.cipher_mode = strdup(optarg); malloc_check(db_params.cipher_mode);
			break;
			case 'y':
				ykchalresp = strtoul(optarg, &inv, 10);
				if (inv[0] == '\0') {
					if (ykchalresp <= 0  ||  optarg[0] == '-') {
						dprintf(STDERR_FILENO, "ERROR: YubiKey slot/device parameter is zero or negative.\n");
						quit(EXIT_FAILURE);
					} else if (ykchalresp > 29) {
						dprintf(STDERR_FILENO, "ERROR: YubiKey slot/device parameter is too high.\n");
						quit(EXIT_FAILURE);
					} else if (ykchalresp < 10) {
						db_params.ykslot = ykchalresp;
						db_params.ykdev = 0;
					} else {
						db_params.ykslot = ykchalresp / 10 ;
						db_params.ykdev = ykchalresp - (ykchalresp / 10 * 10);
					}
				} else {
					dprintf(STDERR_FILENO, "ERROR: Unable to convert the YubiKey slot/device parameter.\n");
					quit(EXIT_FAILURE);
				}

				if (db_params.ykslot > 2  ||  db_params.ykslot < 1  ) {
					dprintf(STDERR_FILENO, "ERROR: YubiKey slot number is not 1 or 2.\n");
					quit(EXIT_FAILURE);
				}

				printf("Using YubiKey slot #%d on device #%d\n", db_params.ykslot, db_params.ykdev);
			break;
			case 'b':
				batchmode = 1;
			break;
			case 'B':
				batchmode = 2;
			break;
			case 'v':
				version();
				exit(EXIT_SUCCESS);
			break;
			case 'h':
				/* FALLTHROUGH */
			default:
				printf( "%s [-k <file>] [-r] [-c/-C <keychain>] [-A <key type,key comment>] [-y <YubiKey slot><YubiKey device index>] [-p <file>] [-P <kdf>] [-e <cipher>] [-m <mode>] [-B/-b] [-v] [-h]\n\n", argv[0]);
				printf(	"-k <file>: Use file as database. The default is ~/.kc/default.kcd .\n"
					"-r: Open the database in read-only mode.\n"
					"-c/-C <keychain>: Start in <keychain>.\n"
					"-A <key type,key comment>: Use an SSH agent to provide password.\n"
					"-y <YubiKey slot><YubiKey device index>: Use a YubiKey to provide password.\n"
					"-p <file>: Read password from file.\n"
					"-P <kdf>: KDF to use.\n"
					"-e <cipher>: Encryption cipher.\n"
					"-m <mode>: Cipher mode.\n"
					"-B/-b: Batch mode.\n"
					"-v: Display version.\n"
					"-h: This help.\n");
				exit(EXIT_SUCCESS);
			break;
		}


	if (!db_params.db_filename) {
		/* using default database */

		env_home = getenv("HOME");

		len = strlen(env_home) + 1 + strlen(DEFAULT_DB_DIR) + 1;
		db_params.db_filename = malloc(len); malloc_check(db_params.db_filename);

		/* default db directory (create it, if it doesn't exist) */
		snprintf(db_params.db_filename, len, "%s/%s", env_home, DEFAULT_DB_DIR);

		if (stat(db_params.db_filename, &st) == 0) {
			if (!S_ISDIR(st.st_mode)) {
				dprintf(STDERR_FILENO, "ERROR: '%s' is not a directory!\n", db_params.db_filename);
				quit(EXIT_FAILURE);
			}
		} else {
			if (getenv("KC_DEBUG"))
				printf("%s(): creating '%s' directory\n", __func__, db_params.db_filename);

			if (mkdir(db_params.db_filename, 0777) != 0) {
				dprintf(STDERR_FILENO, "ERROR: Could not create '%s': %s\n", db_params.db_filename, strerror(errno));
				quit(EXIT_FAILURE);
			}
		}

		/* default db filename */
		len += 1 + strlen(DEFAULT_DB_FILENAME);
		db_params.db_filename = realloc(db_params.db_filename, len); malloc_check(db_params.db_filename);

		snprintf(db_params.db_filename, len, "%s/%s/%s", env_home, DEFAULT_DB_DIR, DEFAULT_DB_FILENAME);
	}

	/* This should be identical of what is in cmd_import.c */
	/* if db_filename exists */
	if (stat(db_params.db_filename, &st) == 0) {
		newdb = 0;

		printf("Opening '%s'\n",db_params.db_filename);

		if (!S_ISLNK(st.st_mode)  &&  !S_ISREG(st.st_mode)) {
			dprintf(STDERR_FILENO, "ERROR: '%s' is not a regular file or a link!\n", db_params.db_filename);
			quit(EXIT_FAILURE);
		}

		if (st.st_size == 0) {
			dprintf(STDERR_FILENO, "ERROR: '%s' is an empty file!\n", db_params.db_filename);
			quit(EXIT_FAILURE);
		}

		if (st.st_size <= IV_DIGEST_LEN + SALT_DIGEST_LEN + 2) {
			dprintf(STDERR_FILENO, "ERROR: '%s' is suspiciously small file!\n", db_params.db_filename);
			quit(EXIT_FAILURE);
		}

		db_params.db_file = open(db_params.db_filename, O_RDWR);
		if (db_params.db_file < 0) {
			perror("ERROR: open(database file)");
			quit(EXIT_FAILURE);
		}

		/* read the IV */
		pos = 0;
		do {
			ret = read(db_params.db_file, db_params.iv + pos, IV_DIGEST_LEN - pos);
			pos += ret;
		} while (ret > 0  &&  pos < IV_DIGEST_LEN);
		db_params.iv[pos] = '\0';

		if (ret < 0) {
			perror("ERROR: read IV(database file)");
			quit(EXIT_FAILURE);
		}
		if (pos != IV_DIGEST_LEN) {
			dprintf(STDERR_FILENO, "ERROR: Could not read IV from database file!\n");
			quit(EXIT_FAILURE);
		}

		if (getenv("KC_DEBUG"))
			printf("%s(): iv='%s'\n", __func__, db_params.iv);

		/* Fast-forward one byte after the current position,
		 * to skip the newline.
		 */
		lseek(db_params.db_file, 1, SEEK_CUR);

		/* read the salt */
		pos = 0;
		do {
			ret = read(db_params.db_file, db_params.salt + pos, SALT_DIGEST_LEN - pos);
			pos += ret;
		} while (ret > 0  &&  pos < SALT_DIGEST_LEN);
		db_params.salt[pos] = '\0';

		if (ret < 0) {
			perror("ERROR: read salt(database file)");
			quit(EXIT_FAILURE);
		}
		if (pos != SALT_DIGEST_LEN) {
			dprintf(STDERR_FILENO, "ERROR: Could not read salt from database file!\n");
			quit(EXIT_FAILURE);
		}

		if (getenv("KC_DEBUG"))
			printf("%s(): salt='%s'\n", __func__, db_params.salt);
	} else {
		newdb = 1;

		printf("Creating '%s'\n", db_params.db_filename);

		db_params.db_file = open(db_params.db_filename, O_RDWR | O_CREAT, 0600);
		if (db_params.db_file < 0) {
			perror("ERROR: open to create(database file)");
			quit(EXIT_FAILURE);
		}

		/* Generate iv/salt */
		if (kc_crypt_iv_salt(&db_params) != 1) {
			dprintf(STDERR_FILENO, "ERROR: Could not generate IV and/or salt!\n");
			quit(EXIT_FAILURE);
		}
	}

	printf("Using '%s' database.\n", db_params.db_filename);


	if (!db_params.readonly)
		if (flock(db_params.db_file, LOCK_NB | LOCK_EX) < 0) {
			if (getenv("KC_DEBUG"))
				printf("%s(): flock(database file)\n", __func__);

			dprintf(STDERR_FILENO, "ERROR: Could not lock the database file!\nMaybe another instance is using that database?\n");
			quit(EXIT_FAILURE);
		}


	bio_chain = kc_setup_bio_chain(db_params.db_filename, 0);
	if (!bio_chain) {
		dprintf(STDERR_FILENO, "ERROR: Could not setup bio_chain!\n");
		quit(EXIT_FAILURE);
	}
	if (getenv("KC_DEBUG"))
		print_bio_chain(bio_chain);


	/* Get the password one way or another */
	if (db_params.pass_filename) {	/* we were given a password file name */
		if (getenv("KC_DEBUG"))
			printf("%s(): opening password file\n", __func__);

		if (stat(db_params.pass_filename, &st) == 0) {
			if (!S_ISLNK(st.st_mode)  &&  !S_ISREG(st.st_mode)) {
				dprintf(STDERR_FILENO, "ERROR: '%s' is not a regular file or a link!\n", db_params.pass_filename);
				quit(EXIT_FAILURE);
			}
		} else {
			perror("ERROR: stat(password file)");
			quit(EXIT_FAILURE);
		}

		/* read in the password from the specified file */
		pass_file = open(db_params.pass_filename, O_RDONLY);
		if (pass_file < 0) {
			perror("ERROR: open(password file)");
			quit(EXIT_FAILURE);
		}

		db_params.pass = malloc(PASSWORD_MAXLEN + 1); malloc_check(db_params.pass);
		pos = 0;
		/* We read PASSWORD_MAXLEN plus one byte, to see if the password in the
		 * password file is longer than PASSWORD_MAXLEN.
		 */
		do {
			ret = read(pass_file, db_params.pass + pos, PASSWORD_MAXLEN + 1 - pos);
			pos += ret;
		} while (ret > 0  &&  pos < PASSWORD_MAXLEN + 1);

		if (ret < 0) {
			perror("ERROR: read(password file)");
			quit(EXIT_FAILURE);
		}
		if (pos == 0) {
			dprintf(STDERR_FILENO, "ERROR: Password file must not be empty!\n");
			quit(EXIT_FAILURE);
		}

		if (pos > PASSWORD_MAXLEN) {
			printf("WARNING: the password in '%s' is longer than the maximum allowed length (%d bytes) of a password, and it was truncated to %d bytes!\n\n", db_params.pass_filename, PASSWORD_MAXLEN, PASSWORD_MAXLEN);
			pos = PASSWORD_MAXLEN;
		}

		db_params.pass_len = pos;

		if (db_params.ykslot > 0) {
			if (db_params.pass_len > 64) {
				dprintf(STDERR_FILENO, "ERROR: Password cannot be longer than 64 bytes when using YubiKey challenge-response!\n");
			}
			if (!kc_ykchalresp(&db_params)) {
				dprintf(STDERR_FILENO, "ERROR: Error while doing YubiKey challenge-response!\n");
				quit(EXIT_FAILURE);
			}
		}

		if (close(pass_file) < 0)
			perror("ERROR: close(password file)");
	} else {
		if (strlen(db_params.ssha_type)) {
			/* use SSH agent to generate the password */
			if (!kc_ssha_get_password(&db_params))
				quit(EXIT_FAILURE);
		} else {
			/* ask for the new password */
			if (kc_password_read(&db_params, newdb) != 1)
				quit(EXIT_FAILURE);
		}
	}

	/* Setup cipher mode and turn on decrypting */
	if (	kc_crypt_key(&db_params) != 1  ||
		kc_crypt_setup(bio_chain, 0, &db_params) != 1
	) {
			dprintf(STDERR_FILENO, "ERROR: Could not setup decrypting!\n");
			quit(EXIT_FAILURE);
	}

	if (db_params.pass)
		memset(db_params.pass, '\0', db_params.pass_len);
	free(db_params.pass); db_params.pass = NULL;


	if (newdb) {	/* new database file */
		if (getenv("KC_DEBUG"))
			printf("%s(): empty database file\n", __func__);

		/* create a new document */
		db = xmlNewDoc(BAD_CAST "1.0");
		if (!db) {
			dprintf(STDERR_FILENO, "ERROR: Could not create XML document!\n");
			quit(EXIT_FAILURE);
		}

		db_root = xmlNewNode(NULL, BAD_CAST "kc");	/* THE root node */
		if (!db_root) {
			dprintf(STDERR_FILENO, "ERROR: Could not create root node!\n");
			quit(EXIT_FAILURE);
		}
		xmlDocSetRootElement(db, db_root);

		xmlAddChild(db_root, xmlNewText(BAD_CAST "\n\t"));

		keychain = xmlNewNode(NULL, BAD_CAST "keychain");	/* the first keychain ... */
		if (!keychain) {
			dprintf(STDERR_FILENO, "ERROR: Could not create default keychain!\n");
			quit(EXIT_FAILURE);
		}
		xmlAddChild(db_root, keychain);
		xmlNewProp(keychain, BAD_CAST "name", BAD_CAST "default");	/* ... its name is "default" */

		created = malloc(TIME_MAXLEN); malloc_check(created);
		snprintf(created, TIME_MAXLEN, "%d", (int)time(NULL));

		xmlNewProp(keychain, BAD_CAST "created", BAD_CAST created);
		xmlNewProp(keychain, BAD_CAST "modified", BAD_CAST created);
		xmlNewProp(keychain, BAD_CAST "description", BAD_CAST "");
		free(created); created = NULL;


		xmlAddChild(keychain, xmlNewText(BAD_CAST "\n\t"));

		xmlAddChild(db_root, xmlNewText(BAD_CAST "\n"));

		/* Mark the database as dirty, if it was created from scratch,
		 * to warn the user when exiting to save.
		 */
		db_params.dirty = 1;
	} else {	/* existing database file */
		pos = kc_db_reader(&buf, bio_chain);

		if (BIO_get_cipher_status(bio_chain) == 0) {
			dprintf(STDERR_FILENO, "ERROR: Failed to decrypt database file!\n");
			quit(EXIT_FAILURE);
		}

		if (getenv("KC_DEBUG"))
			db = xmlReadMemory(buf, pos, NULL, "UTF-8", XML_PARSE_NONET | XML_PARSE_PEDANTIC | XML_PARSE_RECOVER);
		else
			db = xmlReadMemory(buf, pos, NULL, "UTF-8", XML_PARSE_NONET | XML_PARSE_NOERROR | XML_PARSE_NOWARNING);
		if (!db) {
			dprintf(STDERR_FILENO, "ERROR: Could not parse XML document!\n");

			if (strcmp(db_params.cipher_mode, "cbc") != 0)
				dprintf(STDERR_FILENO, "ERROR: If you have specified cfb128 or ofb for chipher mode, this could also mean that either you have entered a wrong password or specified a cipher mode other than that the database was encrypted with!\n");

			quit(EXIT_FAILURE);
		}

		/* Validate the XML structure against our kc.dtd */
		if (!kc_validate_xml(db, 0)) {
			dprintf(STDERR_FILENO, "ERROR: Not a valid kc XML structure ('%s')!\n", db_params.db_filename);
			quit(EXIT_FAILURE);
		}

		db_root = xmlDocGetRootElement(db);
		if (!db_root) {
			dprintf(STDERR_FILENO, "ERROR: Could not find root node!\n");
			quit(EXIT_FAILURE);
		}
		if (!db_root->children  ||  !db_root->children->next) {
			dprintf(STDERR_FILENO, "ERROR: Could not find first keychain!\n");
			quit(EXIT_FAILURE);
		}


		/* Check if the number of keychains and keys per keychains are below the allowed maximum */
		keychain = db_root->children->next;
		while (keychain  &&  count_keychains < ITEMS_MAX) {
			if (keychain->type == XML_ELEMENT_NODE)	/* we only care about ELEMENT nodes */
				count_keychains++;

			/* if the chain has keys, count them */
			if (keychain->children) {
				if (keychain->children->next) {
					count_keys = count_elements(keychain->children->next);
					if (count_keys >= ITEMS_MAX) {
						dprintf(STDERR_FILENO, "ERROR: Number of keys in keychain '%s' is larger than the allowed maximum, %lu.\n", xmlGetProp(keychain, BAD_CAST "name"), ITEMS_MAX - 1);
						quit(EXIT_FAILURE);
					}
				}
			}

			keychain = keychain->next;
		}
		if (count_keychains >= ITEMS_MAX) {
			dprintf(STDERR_FILENO, "ERROR: Number of keychains in current database is larger than the allowed maximum, %lu.\n", ITEMS_MAX - 1);
			quit(EXIT_FAILURE);
		}


		/* We must init 'keychain' to the first keychain of the db, to
		 * be able to search in it
		 */
		keychain = db_root->children->next;
		if (keychain_start) {
			/* Start with the specified keychain */
			keychain = find_keychain(BAD_CAST keychain_start, keychain_start_name);
			if (keychain) {
				printf("Starting with keychain '%s'.\n", keychain_start);
			} else {
				printf("Keychain '%s' not found, starting with the first one!\n", keychain_start);
				keychain = db_root->children->next;
			}
		} else {
			/* Start with the first keychain */
			keychain = db_root->children->next;
		}
	}
	free(buf); buf = NULL;


	/* init and start the CLI */

	if (!batchmode)
		signal(SIGINT, SIG_IGN);

#ifndef _READLINE
	/* init editline */
	e = el_init("kc", stdin, stdout, stderr);
	if (!e) {
		perror("ERROR: el_init()");
		quit(EXIT_FAILURE);
	}

	/* init editline history */
	eh = history_init();
	if (!eh) {
		perror("ERROR: history_init()");
		quit(EXIT_FAILURE);
	}
	if (history(eh, &eh_ev, H_SETSIZE, 100) < 0)
		dprintf(STDERR_FILENO, "ERROR: history(H_SETSIZE): %s\n", eh_ev.str);

	if (history(eh, &eh_ev, H_SETUNIQUE, 1) < 0)
		dprintf(STDERR_FILENO, "ERROR: history(H_SETUNIQUE): %s\n", eh_ev.str);

	/* setup editline/history parameters */
	if (el_set(e, EL_PROMPT, prompt_str) != 0)
		perror("ERROR: el_set(EL_PROMPT)");

	if (el_set(e, EL_SIGNAL, 1) != 0)
		perror("ERROR: el_set(EL_SIGNAL)");

	if (el_set(e, EL_HIST, history, eh) != 0)
		perror("ERROR: el_set(EL_HIST)");

	if (el_set(e, EL_ADDFN, "TAB", "TAB completion", el_tab_complete) != 0)
		perror("ERROR: el_set(EL_ADDFN)");

	if (el_set(e, EL_BIND, "\t", "TAB", NULL) != 0)
		perror("ERROR: el_set(EL_BIND)");

	if (el_set(e, EL_BIND, "^R", "ed-redisplay", NULL) != 0)
		perror("ERROR: el_set(EL_BIND)");

	if (el_set(e, EL_BIND, "^E", "ed-move-to-end", NULL) != 0)
		perror("ERROR: el_set(EL_BIND)");

	if (el_set(e, EL_BIND, "^A", "ed-move-to-beg", NULL) != 0)
		perror("ERROR: el_set(EL_BIND)");

	if (el_set(e, EL_BIND, "^L", "ed-clear-screen", NULL) != 0)
		perror("ERROR: el_set(EL_BIND)");

#if !defined(__linux__)  &&  !defined(__CYGWIN__)
	if (el_source(e, NULL) != 0) {
		if (errno != 0) {
			if (errno != ENOENT)
				perror("ERROR: Error executing .editrc");
		} else
			dprintf(STDERR_FILENO, "ERROR: Error executing .editrc\n");
	}
#endif
	if (el_set(e, EL_EDITMODE, 1) != 0) {
		perror("ERROR: el_set(EL_EDITMODE)");
		quit(EXIT_FAILURE);
	}
#else
	/* init readline */
	if (rl_initialize() != 0) {
		perror("ERROR: rl_initialize()");
		quit(EXIT_FAILURE);
	}

	rl_catch_signals = 1;

	rl_readline_name = "kc";

	rl_completion_entry_function = cmd_generator;
#endif

	strlcpy(prompt_context, "", sizeof(prompt_context));


	/* create the command list */
	commands_init(&commands_first);

	if (db_params.readonly)
		puts("Database is read-only!");

	/* command loop */
	do {
#ifndef _READLINE
		e_line = el_gets(e, &e_count);
#else
		e_line = readline(prompt_str());
#endif
		if (e_line) {
#ifndef _READLINE
			line = strdup(e_line); malloc_check(line);
#else
			line = e_line; e_line = NULL;
#endif
			if (strlen(line) > 0) {
#ifndef _READLINE
				line[strlen(line) - 1] = '\0';		/* remove the newline character from the end */
				history(eh, &eh_ev, H_ENTER, line);
#else
				add_history(line);
#endif
				cmd_match(line);
			}

			free(line); line = NULL;
		} else
			cmd_quit(NULL, NULL);
	} while (1);

	/* NOTREACHED */
	return(0);
} /* main() */


void
#ifndef _READLINE
cmd_match(const char *e_line)
#else
cmd_match(char *e_line)
#endif
{
	unsigned long int	idx = 0, spice = 0;
	char			*line = NULL, *cmd = NULL, *inv = NULL;
	command			*commands = commands_first;


	if (e_line) {
		line = strdup(e_line); malloc_check(line);
	} else {
		return;
	}


	cmd = strtok(line, " ");	/* get the command name or index number */
	if (!cmd) {	/* probably an empty line */
		free(line); line = NULL;
		return;
	}

	errno = 0;
	idx = strtoul((const char *)cmd, &inv, 10);

	/* Special case, if only a number was entered,
	 * we display the appropriate entry, and if there
	 * is another number after it, use it as spice for jamming.
	 */
		/* Because there is no way to tell if the number was negative
		 * with strtoul(), check for the minus sign. This is sooo
		 * great... */
	if (inv[0] == '\0'  &&  errno == 0  &&  cmd[0] != '-') {
		/* We also check for a spice */
		cmd = strtok(NULL, " ");
		if (cmd) {
			errno = 0;
			spice = strtoul((const char *)cmd, &inv, 10);

			if (inv[0] == '\0'  &&  errno == 0  &&  cmd[0] != '-') {
				if (spice > 5)	/* 5 is the limit of spice */
					spice = 5;
			} else {
				puts("<number> [spice]");

				free(line); line = NULL;
				return;
			}
		}

		/* We got a key index, display the value */
		cmd_getnum(idx, spice);
	} else {
		while (commands) {
			/* Find an exact match */
			if (strcmp(commands->name, cmd) == 0)
				break;

			/* '[c]search' and '[c]/' commands can have flags as suffixes. */
			if (strncmp("search", cmd, 6) == 0)
				if (strncmp(commands->name, cmd, 6) == 0)
					break;
			if (strncmp("csearch", cmd, 7) == 0)
				if (strncmp(commands->name, cmd, 7) == 0)
					break;

			if (strncmp("/", cmd, 1) == 0)
				if (strncmp(commands->name, cmd, 1) == 0)
					break;
			if (strncmp("c/", cmd, 2) == 0)
				if (strncmp(commands->name, cmd, 2) == 0)
					break;

			commands = commands->next;	/* iterate through the linked list */
		}

		if (commands)
			commands->fn(e_line, commands);	/* we call the command's respective function here */
		else
			printf("Unknown command: '%s'\n", cmd);
	}

	free(line); line = NULL;
} /* cmd_match() */


#ifndef _READLINE
const char *
el_prompt_null(void)
{
	return("");
}
#endif

const char *
prompt_str(void)
{
	int		prompt_len = 0;
	static char	*prompt = NULL;
	xmlChar		*cname = NULL;


	cname = xmlGetProp(keychain, BAD_CAST "name");

	prompt_len = 1 + xmlStrlen(cname) + 2 + sizeof(prompt_context) + 2 + 1;
	prompt = realloc(prompt, prompt_len); malloc_check(prompt);

	snprintf(prompt, prompt_len, "<%s%% %s%c ", cname, prompt_context, (db_params.readonly ? '|':'>'));
	xmlFree(cname); cname = NULL;

	return(prompt);
} /* prompt_str() */


void
print_bio_chain(BIO *bio_chain)
{
	BIO	*bio_tmp = NULL;


	printf("%s(): bio_chain: ", __func__);
	bio_tmp = bio_chain;
	while (bio_tmp) {
		switch (BIO_method_type(bio_tmp)) {
			case (2|0x0400):
				printf("BIO_TYPE_FILE");
			break;
			case (8|0x0200):
				printf("BIO_TYPE_MD");
			break;
			case (10|0x0200):
				printf("BIO_TYPE_CIPHER");
			break;
			case (11|0x0200):
				printf("BIO_TYPE_BASE64");
			break;
			default:
				printf("%04X", BIO_method_type(bio_tmp));
			break;
		}
		bio_tmp = BIO_next(bio_tmp);
		if (!bio_tmp)
			break;
		printf("->");
	}
	puts("");
} /* print_bio_chain */


#ifndef _READLINE
unsigned char
el_tab_complete(EditLine *e)
{
	xmlNodePtr	db_node = NULL;
	xmlChar		*cname = NULL;

	char		*line_buf = NULL, *line_buf_copy = NULL,
			**match = NULL,
			*word_next = NULL, *word = NULL,
			*complete_max = NULL;
	const LineInfo	*el_lineinfo = NULL;
	command		*commands = commands_first;
	int		hits = 0, i = 0, j = 0, ref = 0, match_max = 0, word_len = 0, line_buf_len = 0;


	el_lineinfo = el_line(e);

	/*
	 * copy the buffer (ie. line) to our variable 'line_buf',
	 * because el_lineinfo->buffer is not NUL terminated
	 */
	line_buf_len = el_lineinfo->lastchar - el_lineinfo->buffer;
	line_buf = malloc(line_buf_len + 1); malloc_check(line_buf);
	memcpy(line_buf, el_lineinfo->buffer, line_buf_len);
	line_buf[line_buf_len] = '\0';


	/*
	 * We search after match(es) for only the last word in the line buffer,
	 * and for this we are prepared for space separated words.
	 * If the last character is a space, then we treat it as if the
	 * previous word would have been finished, and there was no new one to
	 * complete.
	 * We complete 'word' to a command name or a keychain name, depending
	 * on its position in the line buffer.
	 *
	 * It's a side-effect, that if (word == NULL) and (word_len == 0) when
	 * searching for matches, then it will match for eveything with
	 * strncmp().
	 * So we will get a full list of commands or keychain names which we
	 * wanted if the user was just pressing the completion key (eg. TAB)
	 * without something to be completed (eg. empty line, or right after a
	 * word and a space).
	 */

	if (line_buf_len > 0)
		if (line_buf[line_buf_len - 1] != ' ') {
			/*
			 * 'word' is the last word from 'line_buf'.
			 * 'line_buf' is the whole line entered so far into the
			 * edit buffer. We want to complete only the last word
			 * from the edit buffer.
			 */
			line_buf_copy = strdup(line_buf); malloc_check(line_buf_copy);	/* strtok() modifies its given string */
			word_next = strtok(line_buf_copy, " ");
			while (word_next) {
				word = word_next;
				word_next = strtok(NULL, " ");
			}

			if (word)
				word_len = strlen(word);
		}

	/*
	 * Although it is okay if (word == NULL) as long as (word_len == 0),
	 * make this a bit nicer and place an empty string in word if it would
	 * be NULL.
	 */
	if (!word) {
		word_len = 0;
		word = "";
	}


	/*
	 * Basically, the first word will only be completed to a command.
	 * The second (or any additional) word will be completed to a command
	 * only if the first word was 'help' (because help [command] shows the
	 * help for command), or (if the first word is something other than
	 * 'help') it will be completed to a keychain name (to serve as a
	 * parameter for the previously completed command).
	 */

	/* only search for a command name if this is not an already completed
	 * command (ie. there is no space in the line), OR if the already
	 * completed command is 'help'
	 */
	if (!strchr(line_buf, ' ')  ||  (strncmp(line_buf, "help ", 5) == 0))
		/* search for a command name */
		do {
			if (strncmp(word, commands->name, word_len) == 0) {
				hits++;

				match = realloc(match, hits * sizeof(char *)); malloc_check(match);
				match[hits - 1] = strdup(commands->name); malloc_check(match[hits - 1]);
			}
		} while ((commands = commands->next));	/* iterate through the linked list */

	/* only search for a keychain name if this is an already completed
	 * command (ie. there is space(s) in the line), AND it's not the 'help'
	 * command that has been completed previously
	 */
	if (strchr(line_buf, ' ')  &&  (strncmp(line_buf, "help ", 5) != 0)) {
		/* search for a keychain name */
		db_node = keychain->parent->children;
		while (db_node) {
			if (db_node->type == XML_ELEMENT_NODE) {	/* we only care about ELEMENT nodes */
				cname = xmlGetProp(db_node, BAD_CAST "name");

				if (strncmp(word, (char *)cname, word_len) == 0) {
					hits++;

					match = realloc(match, hits * sizeof(char *)); malloc_check(match);
					match[hits - 1] = strdup((char *)cname); malloc_check(match[hits - 1]);
				}

				xmlFree(cname); cname = NULL;
			}

			db_node = db_node->next;
		}
	}


	switch (hits) {
		case 0:	/* no match */
			printf("\a");
		break;
		case 1:
			/* print the word's remaining characters
			 * (remaining: the ones without the part (at the
			 * beginning) that we've entered already)
			 */
			el_push(e, match[0] + word_len);

			free(match[0]); match[0] = NULL;

			/* if this is not an already completed word
			 * (ie. there is no space at the end), then
			 * put a space after the completed word
			 */
			if (line_buf[line_buf_len - 1] != ' ')
				el_push(e, " ");
		break;
		default:	/* more than one match */
			match_max = strlen(match[0]);	/* just a starting value */
			for (ref = 0; ref < hits; ref++)	/* for every matched command, ... */
				for (i = ref + 1; i < hits; i++) {	/* ... we compare it with the next and the remaining commands */
					j = 0;
					while (	match[ref][j] != '\0'  &&  match[i][j] != '\0'  &&
						match[ref][j] == match[i][j])
						j++;

					/* We are searching for the character that is the
					 * last common one in all of the matched commands.
					 *
					 * We are looking for the smallest number, the smallest
					 * difference between the user supplied command fragment
					 * and the shortest command name fragment that is common
					 * in all of the matched commands.
					 */
					if (j < match_max)
						match_max = j;
				}

			j = 0;
			/* complete_max will be the string difference between
			 * the user supplied command fragment and the shortest
			 * command name fragment that is common in all of the
			 * matched commands.
			 * eg.: user: "imp", matched commands: "import" and
			 * "importxml" => complete_max = "ort".
			 */
			complete_max = malloc(word_len + match_max + 1); malloc_check(complete_max);
			for (i = word_len; i < match_max; i++)
				complete_max[j++] = match[0][i];

			complete_max[j] = '\0';

			/* We put the string difference in a char*, because
			 * el_push() only accepts that type.
			 * It would have been simpler to just push the
			 * characters into the line buffer one after another.
			 */
			el_push(e, complete_max);

			free(complete_max); complete_max = NULL;

			puts("");
			for (i = 0; i < hits; i++) {
				printf("%s ", match[i]);
				free(match[i]); match[i] = NULL;
			}
			puts("");
		break;
	}
	free(line_buf); line_buf = NULL;
	free(line_buf_copy); line_buf_copy = NULL;
	free(match); match = NULL;


	return(CC_REDISPLAY);
} /* el_tab_complete() */
#else
char *
cmd_generator(const char *text, int state)
{
	xmlNodePtr	db_node = NULL;
	xmlChar		*cname = NULL;

	char		*cmd = NULL;
	command		*commands = commands_first;
	long int	idx = 0;


	/* only search for a command name if this is not an already completed
	 * command (ie. there is no space in the line), OR if the already
	 * completed command is 'help'
	 */
	if (!strchr(rl_line_buffer, ' ')  ||  (strncmp(rl_line_buffer, "help ", 5) == 0))
		/* search for a command name */
		while (commands) {
			if (idx < state)
				idx++;
			else
				if (strncmp(text, commands->name, strlen(text)) == 0) {
					cmd = strdup(commands->name); malloc_check(cmd);
					return(cmd);
				}


			commands = commands->next;	/* iterate through the linked list */
		}

	/* only search for a keychain name if this is an already completed
	 * command (ie. there is space(s) in the line), AND it's not the 'help'
	 * command that has been completed previously
	 */
	if (strchr(rl_line_buffer, ' ')  &&  (strncmp(rl_line_buffer, "help ", 5) != 0)) {
		/* search for a keychain name */
		db_node = keychain->parent->children;
		while (db_node) {
			if (db_node->type == XML_ELEMENT_NODE) {	/* we only care about ELEMENT nodes */
				if (idx < state)
					idx++;
				else {
					cname = xmlGetProp(db_node, BAD_CAST "name");

					if (strncmp(text, (char *)cname, strlen(text)) == 0)
						return((char *)cname);

					xmlFree(cname); cname = NULL;
				}
			}

			db_node = db_node->next;
		}
	}

	return(NULL);
}
#endif


void
version(void)
{
	printf("%s %s\n", NAME, VERSION);
	puts("Compiled with "
#ifndef _READLINE
		"Editline"
#else
		"Readline"
#endif
#ifdef	_HAVE_PCRE
		", PCRE"
#endif
#ifdef	_HAVE_LIBSCRYPT
		", SCRYPT"
#endif
		" support.");
	puts("Written by LEVAI Daniel <leva@ecentrum.hu>");
	puts("Source, information, bugs: https://github.com/levaidaniel/kc");
} /* help */


void
quit(int retval)
{
	memset(db_params.key, '\0', KEY_LEN);

	if (db_params.pass)
		memset(db_params.pass, '\0', db_params.pass_len);
	free(db_params.pass); db_params.pass = NULL;

	if (getenv("KC_DEBUG"))
		printf("%s(): exiting...\n", __func__);

	if (bio_chain) {
		BIO_free_all(bio_chain);
		if (getenv("KC_DEBUG"))
			printf("%s(): closed bio_chain\n", __func__);
	}

	if (db_params.db_file > 0) {
		if (close(db_params.db_file) == 0) {
			if (getenv("KC_DEBUG"))
				printf("%s(): closed database file\n", __func__);
		} else
			perror("ERROR: close(database file)");
	}

#ifndef _READLINE
	if (eh) {
		history_end(eh);
		if (getenv("KC_DEBUG"))
			printf("%s(): closed editline history\n", __func__);
	}

	if (e) {
		el_end(e);
		if (getenv("KC_DEBUG"))
			printf("%s(): closed editline\n", __func__);
	}
#endif

	exit(retval);
} /* quit() */
