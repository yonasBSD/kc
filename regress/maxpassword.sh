#!/bin/sh -e

set -e


echo "test => $0"

OLDPASSWORD=$(cat ${KC_PASSFILE})
LONGPASSWORD=${OLDPASSWORD}${OLDPASSWORD}'weuhaoifoixaomwxoiughcsweuhaoifoixaomwxoiughcsweuhaoifoixaomwxoiughcsweuhaoifoixaomwxoiughcsweuhaoifoixaomwxoiughcsweuhaoifoixaomwxoiughcsweuhaoifoixaomwxoiughcsweuhaoifoixaomwxoiughcsweuhaoifoixaomwxoiughcsweuhaoifoixaomwxoiughcsweuhaoifoixaomwxoiughcsweuhaoifoixaomwxoiughcsweuhaoifoixaomwxoiughcsweuhaoifoixaomwxoiughcsweuhaoifoixaomwxoiughcsweuhaoifoixaomwxoiughcsweuhaoifoixaomwxoiughcsweuhaoifoixaomwxoiughcsweuhaoifoixaomwxoiughcsweuhaoifoixaomwxoiughcsweuhaoifoixaomwxoiughcsweuhaoifoixaomwxoiughcsweuhaoifoixaomwxoiughcsweuhaoifoixaomwxoiughcsweuhaoifoixaomwxoiughcsweuhaoifoixaomwxoiughcsweuhaoifoixaomwxoiughcsweuhaoifoixaomwxoiughcsweuhaoifoixaomwxoiughcsweuhaoifoixaomwxoiughcsweuhaoifoixaomwxoiughcsweuhaoifoixaomwxoiughcsweuhaoifoixaomwxoiughcsweuhaoifoixaomwxoiughcsweuhaoifoixaomwxoiughcsweuhaoifoixaomwxoiughcsweuhaoifoixaomwxoiughcsweuhaoifoixaomwxoiughcsweuhaoifoixaomwxoiughcsweuhaoifoixaomwxoiughcsweuhaoifoixaomwxoiughcsweuhaoifoixaomwxoiughcsweuhaoifoixaomwxoiughcs'

if printf "passwd\n${LONGPASSWORD}\n${LONGPASSWORD}\n" |${KC_RUN} -b -k ${KC_DB} -p ${KC_PASSFILE};then
	echo "$0 test ok (password change #1)!"
else
	echo "$0 test failed (password change #1)!"
	exit 1
fi

echo "${LONGPASSWORD}" > ${KC_PASSFILE}

MAXPASSLEN=$(grep -E -e"#define[[:space:]]PASSWORD_MAXLEN" common.h |cut -d"	" -f3)

reopen_with_long_pw=$(echo "" |${KC_RUN} -b -k ${KC_DB} -p ${KC_PASSFILE} |grep -E -v -e '^<default% >' -e "^Opening '${KC_DB}'" -e "^Using '${KC_DB}' database." |grep -E -e '^WARNING: ')
if [ "$reopen_with_long_pw" = "WARNING: the password in '${KC_PASSFILE}' is longer than the maximum allowed length (${MAXPASSLEN}) of a password, and it was truncated to ${MAXPASSLEN} characters!" ];then
	echo "$0 test ok (reopen)!"
else
	echo "$0 test failed (reopen)!"
	exit 1
fi


if printf "passwd\n${OLDPASSWORD}\n${OLDPASSWORD}\n" |${KC_RUN} -b -k ${KC_DB} -p ${KC_PASSFILE};then
	echo "$0 test ok (password change #2)!"
else
	echo "$0 test failed (password change #2)!"
	exit 1
fi

echo "${OLDPASSWORD}" > ${KC_PASSFILE}

exit 0
