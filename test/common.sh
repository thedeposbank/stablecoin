#!/bin/bash

COMMON_SH=true

if [[ -z "$ENV_SH" ]] ; then
	source ./env.sh
fi

if [[ -z "$FUNCTIONS_SH" ]] ; then
	source ./functions.sh
fi

function erase() {
	sleep 3
	names="["
	for name in "$@"
	do
		names="$names\"$name\", "
	done
	names="${names%??}]"
	cleos -u $API_URL push action "$BANK_ACC" erase "[$names]" -p $BANK_ACC@active
}

function transfer {
	sleep 2
	from="$1"
	to="$2"
	qtty="$3"
	memo="$4"
	cleos -u $API_URL push action $BANK_ACC transfer "[\"$from\", \"$to\", \"$qtty\", \"$memo\"]" -p $from@active
}

