#!/bin/bash

COMMON_SH=true

if [[ -z "$ENV_SH" ]] ; then
	source ./env.sh
fi

if [[ -z "$FUNCTIONS_SH" ]] ; then
	source ./functions.sh
fi

# parameters: <variable name> <variable value>
setvar() {
	cleos -u $API_URL push action $BANK_ACC setvar "[\"system\", \"$1\", $2]" $ADMIN_ACC@active
	echo "set variable $1 with value $2"
}

# parameters: <variable name> <variable value>
setperiodic() {
	cleos -u $API_URL push action $BANK_ACC setvar "[\"periodic\", \"$1\", $2]" $ORACLE_ACC@active
}

# parameters: <variable name> <variable value>
setdbonds() {
	cleos -u $API_URL push action $BANK_ACC setvar "[\"dbonds\", \"$1\", $2]" $BANK_ACC@active
}

function erase_all() {
	sleep 3
	names="["
	for name in "$@"
	do
		names="$names\"$name\", "
	done
	names="${names%??}]"
	cleos -u $API_URL push action "$BANK_ACC" erase "[$names, [\"DPS\", \"DUSD\"], false]" -p $BANK_ACC@active
	cleos -u $API_URL push action "$BANK_ACC" erase "[[], [\"DPS\", \"DUSD\"], false]" -p $BANK_ACC@active
	cleos -u $API_URL push action "$BANK_ACC" erase "[[\"system\", \"periodic\", \"dbonds\", \"stat\"], [], true]" -p $BANK_ACC@active
}

function transfer {
	sleep 2
	from="$1"
	to="$2"
	qtty="$3"
	memo="$4"
	cleos -u $API_URL push action $BANK_ACC transfer "[\"$from\", \"$to\", \"$qtty\", \"$memo\"]" -p $from@active
}

function transfer_eos {
	sleep 2
	from="$1"
	to="$2"
	qtty="$3"
	memo="$4"
	cleos -u $API_URL push action eosio.token transfer "[\"$from\", \"$to\", \"$qtty\", \"$memo\"]" -p $from@active
}

function create_dps() {
	sleep 3
	cleos -u $API_URL push action "$BANK_ACC" create "[\"$BANK_ACC\", \"$dps_maximum_supply\"]" -p $BANK_ACC@active
}

function listdpssale() {
	sleep 3
	dps_total_supply="$1"
	dps_listed_price="$2"
	cleos -u $API_URL push action "$BANK_ACC" listdpssale "[\"$dps_total_supply\", \"$dps_listed_price\"]" -p $ADMIN_ACC@active
}
