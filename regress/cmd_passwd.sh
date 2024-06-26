#!/bin/sh -e

set -e


echo "test => $0"

PASSWORD=$(cat ${KC_PASSFILE})
NEWPASSWORD='123abc321ABC'

if printf "passwd\n${NEWPASSWORD}\n${NEWPASSWORD}\n" |${KC_RUN} -b -k ${KC_DB} -p ${KC_PASSFILE};then
	echo "$0 test ok (password file)!"
else
	echo "$0 test failed (password file)!"
	exit 1
fi

echo -n "${NEWPASSWORD}" > ${KC_PASSFILE}

if printf "passwd\n${PASSWORD}\n${PASSWORD}\n" |${KC_RUN} -b -k ${KC_DB} -p ${KC_PASSFILE};then
	echo "$0 test ok (new password)!"
else
	echo "$0 test failed (new password)!"
	exit 1
fi

echo -n "${PASSWORD}" > ${KC_PASSFILE}

if printf "passwd -P bcrypt\n${PASSWORD}\n${PASSWORD}\n" |${KC_RUN} -b -k ${KC_DB} -p ${KC_PASSFILE};then
	echo "$0 test ok (password file #2)!"
else
	echo "$0 test failed (password file #2)!"
	exit 1
fi


typeset -i SKIP=0
if [ -z "${SSH_AUTH_SOCK}" ];then
	echo "$0 SSH agent test skipped (ssh-agent is not running)!"
	SKIP=1
fi

typeset -i RETVAL=0
which ssh-add  ||  RETVAL=$?
if [ $RETVAL -eq 0 ];then
	SSH_ADD=$(which ssh-add)
else
	echo "$0 SSH agent test skipped (cannot find ssh-add(1))!"
	SKIP=1
fi


if [ $SKIP -eq 0 ];then
	ssh-add regress/test_id_rsa:2048_ssh
	if printf "passwd -A ssh-rsa,PuBkEy_+*()[]{}';.!@#^&=-/|_comment-rsa:2048,password -P bcrypt -e blowfish -m cfb\n${PASSWORD}\n${PASSWORD}\n" |${KC_RUN} -b -k ${KC_DB} -P bcrypt -p ${KC_PASSFILE};then
		echo "$0 test ok (bcrypt KDF)!"
	else
		echo "$0 test failed (bcrypt KDF)!"
		exit 1
	fi

	if printf "${PASSWORD}\npasswd -A ssh-rsa,PuBkEy_+*()[]{}';.!@#^&=-/|_comment-rsa:2048 -P bcrypt -e blowfish -m cfb\n" |${KC_RUN} -b -k ${KC_DB} -P bcrypt -e blowfish -m cfb -A "ssh-rsa,PuBkEy_+*()[]{}';.!@#^&=-/|_comment-rsa:2048,password";then
		echo "$0 test ok (SSH agent with password, bcrypt KDF, blowfish cipher, cfb cipher mode)!"
	else
		echo "$0 test failed (SSH agent with password, bcrypt KDF, blowfish cipher, cfb cipher mode)!"
		exit 1
	fi

	# Also fake an invalid parameter
	if printf "passwd --help\npasswd -P sha512 -e aes256 -m cbc\n${PASSWORD}\n${PASSWORD}\n" |${KC_RUN} -b -k ${KC_DB} -P bcrypt -e blowfish -m cfb -A "ssh-rsa,PuBkEy_+*()[]{}';.!@#^&=-/|_comment-rsa:2048";then
		echo "$0 test ok (SSH agent, bcrypt KDF, blowfish cipher, cfb cipher mode)!"
	else
		echo "$0 test failed (SSH agent, bcrypt KDF, blowfish cipher, cfb cipher mode)!"
		exit 1
	fi
	ssh-add -d regress/test_id_rsa:2048_ssh

	if printf "passwd -R 1000\n${PASSWORD}\n${PASSWORD}\n" |${KC_RUN} -b -k ${KC_DB} -p ${KC_PASSFILE};then
		echo "$0 test ok (password, sha512 KDF, aes256 cipher, cbc cipher mode)!"
	else
		echo "$0 test failed (password, sha512 KDF, aes256 cipher, cbc cipher mode)!"
		exit 1
	fi

	if printf "passwd -P bcrypt\n${PASSWORD}\n${PASSWORD}\n" |${KC_RUN} -R 1000 -b -k ${KC_DB} -p ${KC_PASSFILE};then
		echo "$0 test ok (password, sha512 KDF (1000 rounds), aes256 cipher, cbc cipher mode)!"
	else
		echo "$0 test failed (password, sha512 KDF (1000 rounds), aes256 cipher, cbc cipher mode)!"
		exit 1
	fi
fi

if printf "passwd -e blowfish -P bcrypt -K 16\n${PASSWORD}\n${PASSWORD}\n" |${KC_RUN} -b -k ${KC_DB} -P bcrypt -p ${KC_PASSFILE};then
	echo "$0 test ok (change to blowfish cipher, bcrypt KDF w/ 16 byte key length)!"
else
	echo "$0 test failed (change to blowfish cipher, bcrypt KDF w/ 16 byte key length)!"
	exit 1
fi

if printf "passwd -e chacha20 -P sha3 -K 32\n${PASSWORD}\n${PASSWORD}\n" |${KC_RUN} -b -k ${KC_DB} -e blowfish -P bcrypt -K 16 -p ${KC_PASSFILE};then
	echo "$0 test ok (change to sha3 KDF, chacha20 cipher from bcrypt KDF, blowfish cipher)!"
else
	echo "$0 test failed (change to sha3 KDF, chacha20 cipher from bcrypt KDF, blowfish cipher)!"
	exit 1
fi

if ${KC_RUN} -v |grep -q -E -e '^Compiled with .*argon2id';then
	if printf "passwd -e aes256 -P argon2id -1 1 -2 512000 -R 1\n${PASSWORD}\n${PASSWORD}\n" |${KC_RUN} -b -k ${KC_DB} -e chacha20 -P sha3 -K 32 -p ${KC_PASSFILE};then
		echo "$0 test ok (change to argon2id KDF, aes256 cipher from sha3 KDF, chacha20 cipher)!"
	else
		echo "$0 test failed (change to argon2id KDF, aes256 cipher from sha3 KDF, chacha20 cipher)!"
		exit 1
	fi

	if printf "passwd -P sha512\n${PASSWORD}\n${PASSWORD}\n" |${KC_RUN} -b -k ${KC_DB} -P argon2id -1 1 -2 512000 -R 1 -p ${KC_PASSFILE};then
		echo "$0 test ok (reset to sha512 KDF from argon2id KDF)!"
	else
		echo "$0 test failed (reset to sha512 KDF from argon2id KDF)!"
		exit 1
	fi
else
	if printf "passwd -e aes256 -P sha512\n${PASSWORD}\n${PASSWORD}\n" |${KC_RUN} -b -k ${KC_DB} -e chacha20 -P sha3 -K 32 -p ${KC_PASSFILE};then
		echo "$0 test ok (reset to sha512 KDF, aes256 cipher from sha3 KDF, chacha20 cipher)!"
	else
		echo "$0 test failed (reset to sha512 KDF, aes256 cipher from sha3 KDF, chacha20 cipher)!"
		exit 1
	fi
fi


exit 0
