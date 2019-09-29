#!/bin/bash

. ./env.sh
. ./functions.sh

function erase() {
	sleep 3
	cleos -u $API_URL push action "$BANK_ACC" erase '[]' -p $BANK_ACC@active
}

function listdpssale() {
	sleep 3
	cleos -u $API_URL push action "$BANK_ACC" listdpssale '["'$dps_total_supply'", "'$dps_listed_price'"]' -p $ADMIN_ACC@active
}

