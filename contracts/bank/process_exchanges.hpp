#pragma once

#include <eosio/crypto.hpp>
#include <eosio/print.hpp>
#include <cctype>
#include <cmath>
#include <algorithm>
#include <bank.hpp>



using namespace eosio;
using namespace std;

void bank::process_regular_transfer(name from, name to, asset quantity, string memo){
	// regular transfer, get transfer fee
	uint64_t fee = std::round(1e-10 * quantity.amount * get_variable("fee.transfer", SYSTEM_SCOPE));

	#ifdef DEBUG
			auto payer = BANKACCOUNT;
	#else
			auto payer = has_auth( to ) ? to : from;
	#endif

	asset fee_tokens(fee, quantity.symbol);

	sub_balance( from, quantity + fee_tokens );
	add_balance( to, quantity, payer );
	if(fee != 0) {
		add_balance( BANKACCOUNT, fee_tokens, payer );
	}
}

void bank::process_service_transfer(name from, name to, asset quantity, string memo){
	// service transfer, no fees (like "issue" action to other account)
	auto payer = has_auth( to ) ? to : from;

	sub_balance( from, quantity );
	add_balance( to, quantity, payer );
}

void bank::process_redeem_DUSD_for_DPS(name from, name to, asset quantity, string memo){
	// exchange DUSD => DPS

	asset dusdToDevFund, dusdToReserve;
	asset dpsQuantity = dusd2dps(quantity);
	splitToDev(quantity, dusdToReserve, dusdToDevFund);

	auto payer = from;
	
	// transfer DUSD
	sub_balance(from, dusdToReserve);
	add_balance(BANKACCOUNT, dusdToReserve, payer);

	if(dusdToDevFund.amount != 0)
		SEND_INLINE_ACTION(*this, transfer, {{from, "active"_n}}, {from, DEVELACCOUNT, dusdToDevFund, memo});
	
	// transfer DPS
	SEND_INLINE_ACTION(*this, transfer, {{BANKACCOUNT, "active"_n}}, {BANKACCOUNT, from, dpsQuantity, "DPS for DUSD"});
}

void bank::process_redeem_DUSD_for_DBTC(name from, name to, asset quantity, string memo) {
	auto payer = has_auth( to ) ? to : from;
	sub_balance( from, quantity );
	add_balance( to, quantity, payer );

	asset dbtcQuantity = {dusd2satoshi(quantity), DBTC};

	// exchange DUSD => DBTC
	action(
		permission_level{_self, "active"_n},
		CUSTODIAN, "transfer"_n,
		std::make_tuple(BANKACCOUNT, from, dbtcQuantity, memo)
	).send();

	SEND_INLINE_ACTION(*this, retire, {{BANKACCOUNT, "active"_n}}, {quantity, memo});
}

void bank::process_redeem_DUSD_for_BTC(name from, name to, asset quantity, string btc_address){
	auto payer = has_auth( to ) ? to : from;
	sub_balance( from, quantity );
	add_balance( to, quantity, payer );

	asset dbtcQuantity = {dusd2satoshi(quantity), DBTC};
	// exchange DUSD => BTC
	action(
		permission_level{_self, "active"_n},
		CUSTODIAN, "transfer"_n,
		std::make_tuple(BANKACCOUNT, CUSTODIAN, dbtcQuantity, btc_address)
	).send();

	SEND_INLINE_ACTION(*this, retire, {{BANKACCOUNT, "active"_n}}, {quantity, btc_address});
}

void bank::process_redeem_DPS_for_DUSD(name from, name to, asset quantity, string memo){

	asset dusdQuantity = dps2dusd(quantity);
	
	auto payer = has_auth( to ) ? to : from;
	// transfer DPS to issuer.
	sub_balance(from, quantity);
	add_balance(BANKACCOUNT, quantity, payer);

	// exchange DPS => DUSD at nominal price
	SEND_INLINE_ACTION(*this, transfer, {{BANKACCOUNT, "active"_n}}, {BANKACCOUNT, from, dusdQuantity, "DPS for DUSD sell"});
}

void bank::process_redeem_DPS_for_DBTC(name from, name to, asset quantity, string memo){
	asset dusdQuantity = dps2dusd(quantity);
	
	auto payer = has_auth( to ) ? to : from;
	// transfer DPS to issuer.
	sub_balance(from, quantity);
	add_balance(BANKACCOUNT, quantity, payer);

	// redeem for DBTC or BTC
	SEND_INLINE_ACTION(*this, retire, {{BANKACCOUNT, "active"_n}}, {dusdQuantity, memo});

	asset dbtcQuantity = {dusd2satoshi(dusdQuantity), DBTC};
}

void bank::process_redeem_DPS_for_BTC(name from, name to, asset quantity, string memo){
	asset dusdQuantity = dps2dusd(quantity);
	
	auto payer = has_auth( to ) ? to : from;
	// transfer DPS to issuer.
	sub_balance(from, quantity);
	add_balance(BANKACCOUNT, quantity, payer);

	// redeem for DBTC or BTC
	SEND_INLINE_ACTION(*this, retire, {{BANKACCOUNT, "active"_n}}, {dusdQuantity, memo});

	asset dbtcQuantity = {dusd2satoshi(dusdQuantity), DBTC};
}

void bank::process_mint_DPS_for_DBTC(name buyer, asset dbtc_quantity) {
	asset dusd_quantity = satoshi2dusd(dbtc_quantity.amount);
	asset dusd_to_dev_fund, dusd_to_capital;
	asset dps_quantity = dusd2dps(dusd_quantity);
	splitToDev(dusd_quantity, dusd_to_capital, dusd_to_dev_fund);

	SEND_INLINE_ACTION(*this, issue, {{BANKACCOUNT, "active"_n}}, {BANKACCOUNT, dusd_quantity, "DPS for DBTC"});
	SEND_INLINE_ACTION(*this, transfer, {{BANKACCOUNT, "active"_n}}, {BANKACCOUNT, DEVELACCOUNT, dusd_to_dev_fund, "DPS for DBTC"});
	SEND_INLINE_ACTION(*this, transfer, {{BANKACCOUNT, "active"_n}}, {BANKACCOUNT, buyer, dps_quantity, "DPS for DBTC"});
}

void bank::process_mint_DUSD_for_DBTC(name buyer, asset dbtc_quantity) {
	asset dusd_quantity = satoshi2dusd(dbtc_quantity.amount);
	SEND_INLINE_ACTION(*this, issue, {{BANKACCOUNT, "active"_n}}, {buyer, dusd_quantity, "DUSD for DBTC"});
}

void bank::process_mint_DUSD_for_EOS(name buyer, asset eos_quantity) {
	asset dusd_quantity = eos2dusd(eos_quantity.amount);
	SEND_INLINE_ACTION(*this, issue, {{BANKACCOUNT, "active"_n}}, {buyer, dusd_quantity, "DUSD for EOS"});
}

void bank::process_redeem_DUSD_for_EOS(name from, name to, asset quantity, string memo){
	auto payer = has_auth( to ) ? to : from;
	sub_balance( from, quantity );
	add_balance( to, quantity, payer );

	asset eos_quantity = {dusd2eos(quantity), EOS};

	// exchange DUSD => EOS
	action(
		permission_level{_self, "active"_n},
		EOSIOTOKEN, "transfer"_n,
		std::make_tuple(BANKACCOUNT, from, eos_quantity, memo)
	).send();

	SEND_INLINE_ACTION(*this, retire, {{BANKACCOUNT, "active"_n}}, {quantity, memo});
}