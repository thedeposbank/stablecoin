#!/bin/bash

if [[ -z "$ENV_SH" ]] ; then
	source ./env.sh
fi

if [[ -z "$FUNCTIONS_SH" ]] ; then
	source ./functions.sh
fi

if [[ -z "$COMMON_SH" ]] ; then
	source ./common.sh
fi

title "Exchange EOS => DUSD"

must_pass "EOS => DUSD" transfer $TESTACC $BANK_ACC "10.0000 EOS" ""

title "Exchange DUSD => EOS"