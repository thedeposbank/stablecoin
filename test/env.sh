ENV_SH=true

export DBONDS=thedbondsacc
export TESTACC=depostest114
export TEST_ACC=$TESTACC
export BUYER=depostest115
export BANK_ACC=thedeposbank
export CUSTODIAN_ACC=deposcustody
export ADMIN_ACC=deposadmin11
export ORACLE_ACC=deposoracle1
export DEVELACC=deposdevelop
export DEVEL_ACC=$DEVELACC
export API_URL="http://jungle2.cryptolions.io"

export dps_maximum_supply="1000000.00000000 DPS"

export CPPFLAGS="-D_LIBCPP_NO_EXCEPTIONS -DDEBUG -DBITCOIN_TESTNET=true"
# wait_at_each_step=true

$HOME/bin/unlock_wallet
