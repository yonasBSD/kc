#!/bin/sh -e

set -e


echo "test => $0"

PASSWORD=$(cat ${KC_PASSFILE})
NEWPASSWORD='123abc321ABC'

if printf "passwd\n${NEWPASSWORD}\n${NEWPASSWORD}\n" |${KC_RUN} -b -k ${KC_DB} -p ${KC_PASSFILE};then
	echo "$0 test ok (passwd)!"
else
	echo "$0 test failed (passwd)!"
	exit 1
fi

echo -n "${NEWPASSWORD}" > ${KC_PASSFILE}

if printf "passwd\n${PASSWORD}\n${PASSWORD}\n" |${KC_RUN} -b -k ${KC_DB} -p ${KC_PASSFILE};then
	echo "$0 test ok (orig passwd)!"
else
	echo "$0 test failed (orig passwd)!"
	exit 1
fi

echo -n "${PASSWORD}" > ${KC_PASSFILE}

if printf "passwd -P bcrypt\n${PASSWORD}\n${PASSWORD}\n" |${KC_RUN} -b -k ${KC_DB} -p ${KC_PASSFILE};then
	echo "$0 test ok (passwd bcrypt #1)!"
else
	echo "$0 test failed (passwd bcrypt #1)!"
	exit 1
fi

if [ -z "${SSH_AUTH_SOCK}" ];then
	echo "$0 test failed (ssh-agent is not running)!"
	exit 1
fi

ssh-add regress/test_id_rsa:2048_ssh
if printf "passwd -A ssh-rsa,PuBkEy_+*()[]{}';.!@#^&=-/|_comment-rsa:2048 -P bcrypt -e blowfish -m ecb\n${PASSWORD}\n${PASSWORD}\n" |${KC_RUN} -b -k ${KC_DB} -P bcrypt -p ${KC_PASSFILE};then
	echo "$0 test ok (SSH agent, passwd bcrypt, cipher blowfish, cipher mode ecb #2)!"
else
	echo "$0 test failed (SSH agent, passwd bcrypt, cipher blowfish, cipher mode ecb #2)!"
	exit 1
fi

# Also fake an invalid parameter
if printf "passwd --help\npasswd -P sha512 -e aes256 -m cbc\n${PASSWORD}\n${PASSWORD}\n" |${KC_RUN} -b -k ${KC_DB} -P bcrypt -e blowfish -m ecb -A "ssh-rsa,PuBkEy_+*()[]{}';.!@#^&=-/|_comment-rsa:2048";then
	echo "$0 test ok (SSH agent, passwd bcrypt #3)!"
else
	echo "$0 test failed (SSH agent, passwd bcrypt #3)!"
	exit 1
fi
ssh-add -d regress/test_id_rsa:2048_ssh

if echo "" |${KC_RUN} -b -k ${KC_DB} -p ${KC_PASSFILE};then
	echo "$0 test ok (reopen)!"
else
	echo "$0 test failed (reopen)!"
	exit 1
fi


exit 0
