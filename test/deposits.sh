#!/bin/bash

# set basic shell variables
. ./env.sh

# define functions for testing and commenting
. ./functions.sh

# define shell wrappings for some contract actions
. ./common.sh

print_deposit_info() {
	account=${1:-$TEST_ACC}
	cleos -u $API_URL get table $BANK_ACC $account deposits | jq '.rows[0]'
}

check_deposit() {
	sleep 1
	account=${1:-$TEST_ACC}
	amount_pattern="$2"
	lowest_pattern="$3"
	json=`cleos -u $API_URL get table $BANK_ACC $account deposits | jq '.rows[0]'`
	echo "$json"
	amount=`echo "$json" | jq -r .deposit_amount`
	lowest=`echo "$json" | jq -r .lowest_value`
	if [[ "$amount" = "$amount_pattern" && "$lowest" = "$lowest_pattern" ]] ; then
		return 0
	else
		return 1
	fi
}

update_deposit() {
	account=${1:-$TEST_ACC}
	cleos -u $API_URL push action $BANK_ACC upddeposit "[\"$account\"]" -p $account@active
}

close_deposit() {
	account=${1:-$TEST_ACC}
	cleos -u $API_URL push action $BANK_ACC closedeposit "[\"$account\"]" -p $account@active
}

wthdrdeposit() {
	account=${1:-$TEST_ACC}
	amount=${2:-"1.00 DUSD"}
	cleos -u $API_URL push action $BANK_ACC wthdrdeposit "[\"$account\", \"$amount\"]" -p $account@active
}

wait_periods() {
	start_seconds=`date +%s`
	start_period=$((start_seconds/12))
	periods=${1:-1}
	end_seconds=$(((start_period+periods)*12+2))
	title "wait $periods periods"
	while [[ `date +%s` -lt $end_seconds ]]
	do
		sleep 1
	done
}

wait_for_period_beginnig() {
	title "waiting for period beginning"
	seconds=`date +%s`
	seconds=$((seconds%12))
	while [[ $seconds -lt 3 || $seconds -gt 9 ]]
	do
		echo $seconds
		sleep 1
		seconds=`date +%s`
		seconds=$((seconds%12))
	done
}

# title "buy DUSD for EOS"
# must_pass "buy DUSD" transfer_eos $TEST_ACC $BANK_ACC "300.0000 EOS" "buy DUSD"

# title "set variables"
# setvar dpstunittime 100000000
# setvar dpstunitrate 1000000

wait_for_period_beginnig

title "send DUSD to deposit"
must_pass "deposit" transfer $TEST_ACC $BANK_ACC "1000.00 DUSD" "deposit"

title "check deposit info"
must_pass "initial deposit info" check_deposit $TEST_ACC "1000.00 DUSD" "0.00 DUSD"

wait_periods 1

title "update deposit"
must_pass "update deposit" update_deposit $TEST_ACC

title "check deposit info"
must_pass "first period update" check_deposit $TEST_ACC "1000.00 DUSD" "1000.00 DUSD"

wait_periods 1

title "update deposit"
must_pass "update deposit" update_deposit $TEST_ACC

title "check deposit info"
must_pass "one full period update" check_deposit $TEST_ACC "1010.00 DUSD" "1010.00 DUSD"

wait_periods 2

title "update deposit"
must_pass "update deposit" update_deposit $TEST_ACC

title "check deposit info"
must_pass "two more full periods update" check_deposit $TEST_ACC "1030.30 DUSD" "1030.30 DUSD"

title "top up deposit"
must_pass "top up deposit" transfer $TEST_ACC $BANK_ACC "100.00 DUSD" "deposit"

title "check deposit info"
must_pass "top up update" check_deposit $TEST_ACC "1130.30 DUSD" "1030.30 DUSD"

wait_periods 2

title "update deposit"
must_pass "update deposit" update_deposit $TEST_ACC

title "check after top up and two periods"
must_pass "two periods after top up" check_deposit $TEST_ACC "1152.01 DUSD" "1152.01 DUSD"

title "withdrawal test"
must_pass "withdrawal" wthdrdeposit $TEST_ACC "1100.00 DUSD"

title "overdrawn withdrawal test"
must_fail "overdrawn withdrawal" wthdrdeposit $TEST_ACC "1000.00 DUSD"

title "check after withdrawal"
must_pass "after withdrawal" check_deposit $TEST_ACC "52.01 DUSD" "52.01 DUSD"

title "close deposit"
must_pass "close deposit" close_deposit $TEST_ACC
