/**
 *  bank.cpp derived from eosio.token.cpp
 *  @copyright https://github.com/EOSIO/eos/blob/master/LICENSE
 */

#include <bank.hpp>
#include <eosio/crypto.hpp>
#include <eosio/print.hpp>
#include <cctype>
#include <cmath>

using namespace eosio;

ACTION bank::transfer( name    from,
						name    to,
						asset   quantity,
						string  memo )
{
	// print("transfer action. self: ", get_self(), " code: ", get_code(), "\n");

	check( from != to, "cannot transfer to self" );

#ifdef DEBUG
	check(has_auth(from) || has_auth(BANKACCOUNT), "sender or bank auth required");
#else
	require_auth( from );
#endif

	check( is_account( to ), "to account does not exist");
	auto sym = quantity.symbol.code();
	stats statstable( _self, sym.raw() );
	const auto& st = statstable.get( sym.raw(), "no stats for given symbol code" );

	require_recipient( from );
	require_recipient( to );

	check( quantity.is_valid(), "invalid quantity" );
	check( quantity.amount > 0, "must transfer positive quantity" );
	check( quantity.symbol == st.supply.symbol, "symbol precision mismatch" );
	check( memo.size() <= 256, "memo has more than 256 bytes" );

	if((from != BANKACCOUNT && to != BANKACCOUNT) || memo == "deny") {
		// regular transfer, get transfer fee
		uint64_t fee = 0;
		variables vars(BANKACCOUNT, SYSTEM_SCOPE.value);
		
		auto var_itr = vars.find(("fee.transfer"_n).value);
		if(var_itr != vars.end())
			fee = std::round(1e-10 * quantity.amount * var_itr->value);

#ifdef DEBUG
		auto payer = BANKACCOUNT;
#else
		auto payer = has_auth( to ) ? to : from;
#endif

		asset fee_tokens(fee, quantity.symbol);

		sub_balance( from, quantity + fee_tokens );
		add_balance( to, quantity, payer );
		if(fee != 0) {
			add_balance( st.issuer, fee_tokens, payer );
		}
		return;
	}
	if(from == BANKACCOUNT) {
		// service transfer, no fees (like "issue" action to other account)
		auto payer = has_auth( to ) ? to : from;

		sub_balance( from, quantity );
		add_balance( to, quantity, payer );
		return;
	}
	// here we have conversion transfer. let's check for limits
	variables sys_vars(BANKACCOUNT, SYSTEM_SCOPE.value);
	variables periodic_vars(BANKACCOUNT, PERIODIC_SCOPE.value);
	auto maxOrderSize = sys_vars.require_find("maxordersize"_n.value, "maxordersize not found")->value;
	double btcusd = periodic_vars.require_find("btcusd"_n.value)->value * 1e-8;

	if(to == BANKACCOUNT) {
		// seems this call is not needed since bank.on_transfer makes it anyway
		//check_main_switch();

		if(quantity.symbol == DUSD) {
			check(quantity.amount / dusdPrecision * btcusd <= maxOrderSize, "operation limit exceeded");

			if(memo == "Buy DPS") {
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

				return;
			}
			else {
				auto payer = has_auth( to ) ? to : from;
				sub_balance( from, quantity );
				add_balance( to, quantity, payer );

				asset dbtcQuantity = {dusd2satoshi(quantity), DBTC};

				if(memo == "Redeem for DBTC") {
					// exchange DUSD => DBTC
					action(
						permission_level{_self, "active"_n},
						CUSTODIAN, "transfer"_n,
						std::make_tuple(BANKACCOUNT, from, dbtcQuantity, memo)
					).send();
				}
				else {
					// exchange DUSD => BTC
					validate_btc_address(memo, BITCOIN_TESTNET);
					action(
						permission_level{_self, "active"_n},
						CUSTODIAN, "transfer"_n,
						std::make_tuple(BANKACCOUNT, CUSTODIAN, dbtcQuantity, memo)
					).send();
				}

				SEND_INLINE_ACTION(*this, retire, {{BANKACCOUNT, "active"_n}}, {quantity, memo});
			}
		}
		else if(quantity.symbol == DPS) {
			// redeem DPS for DUSD, DBTC or BTC

			asset dusdQuantity = dps2dusd(quantity);
			check(dusdQuantity.amount / dusdPrecision * btcusd <= maxOrderSize, "operation limit exceeded");
			
			auto payer = has_auth( to ) ? to : from;
			// transfer DPS to issuer.
			sub_balance(from, quantity);
			add_balance(BANKACCOUNT, quantity, payer);

			if(memo == "Redeem for DUSD") {
				// exchange DPS => DUSD at nominal price
				SEND_INLINE_ACTION(*this, transfer, {{BANKACCOUNT, "active"_n}}, {BANKACCOUNT, from, dusdQuantity, "DPS for DUSD sell"});
			}
			else {
				// redeem for DBTC or BTC
				SEND_INLINE_ACTION(*this, retire, {{BANKACCOUNT, "active"_n}}, {dusdQuantity, memo});

				asset dbtcQuantity = {dusd2satoshi(dusdQuantity), DBTC};

				if(memo == "Redeem for DBTC") {
					// exchange DUSD => DBTC
					action(
						permission_level{_self, "active"_n},
						CUSTODIAN, "transfer"_n,
						std::make_tuple(BANKACCOUNT, from, dbtcQuantity, memo)
					).send();
				}
				else {
					// exchange DUSD => BTC
					validate_btc_address(memo, BITCOIN_TESTNET);
					action(
						permission_level{_self, "active"_n},
						CUSTODIAN, "transfer"_n,
						std::make_tuple(BANKACCOUNT, CUSTODIAN, dbtcQuantity, memo)
					).send();
				}
			}
		}
		else fail("arbitrary transfer to bank account");
	}
}

ACTION bank::ontransfer(name from, name to, asset quantity, const string& memo) {
	print("bank 'ontransfer'. self: ", get_self(), " first receiver: ", get_first_receiver());
	check_main_switch();

	check_on_transfer(from, to, quantity, memo);

	if(to == BANKACCOUNT && quantity.symbol == DBTC && !is_hex256(memo)) {
		//variables sys_vars(BANKACCOUNT, SYSTEM_SCOPE.value);
		//auto maxOrderSize = sys_vars.require_find("maxordersize"_n.value, "maxordersize not found")->value;
		//check(quantity.amount <= maxOrderSize, "operation limit exceeded");

		name buyer;
		string buyerStr, tokenStr;

		size_t end = memo.find(' ');
		buyerStr = memo.substr(0, end);
		if(end != std::string::npos) {
			size_t begin = memo.find_first_not_of(' ', end);
			end = memo.find(' ', begin);
			tokenStr = memo.substr(begin, end);
		}

		if(from == CUSTODIAN) {
			buyer = name(buyerStr);
		}
		else {
			check(buyerStr == "Buy", "memo format for token buy: 'Buy <token name>'");
			buyer = from;
		}
		symbol_code token(tokenStr);
		asset dusdQuantity = satoshi2dusd(quantity.amount);

		if(token == DUSD.code()) {
			SEND_INLINE_ACTION(*this, issue, {{BANKACCOUNT, "active"_n}}, {buyer, dusdQuantity, "DUSD for DBTC"});
		}
		else if(token == DPS.code()) {
			asset dusdToDevFund, dusdToReserve;
			asset dpsQuantity = dusd2dps(dusdQuantity);
			splitToDev(dusdQuantity, dusdToReserve, dusdToDevFund);

			SEND_INLINE_ACTION(*this, issue, {{BANKACCOUNT, "active"_n}}, {BANKACCOUNT, dusdQuantity, "DPS for DBTC"});
			SEND_INLINE_ACTION(*this, transfer, {{BANKACCOUNT, "active"_n}}, {BANKACCOUNT, DEVELACCOUNT, dusdToDevFund, "DPS for DBTC"});
			SEND_INLINE_ACTION(*this, transfer, {{BANKACCOUNT, "active"_n}}, {BANKACCOUNT, buyer, dpsQuantity, "DPS for DBTC"});
		}
		else fail("unknown token requested");
	}
}

ACTION bank::issue( name to, asset quantity, string memo ) {
	token::issue(to, quantity, memo);
	if(quantity.symbol == DUSD && memo != "supply balancing")
		balanceSupply();
}

ACTION bank::retire( asset quantity, string memo ) {
	token::retire(quantity, memo);
	if(quantity.symbol == DUSD && memo != "supply balancing")
		balanceSupply();
}

ACTION bank::setvar(name scope, name varname, int64_t value) {
	token::setvar(scope, varname, value);

	// balance DUSD supply:
	// issue or retire tokens to keep supply equal to current USD value of BTC reserves
	// do it only if reserve balances and BTC/USD rate variables are set and any of them is changed by this action
	if( scope == PERIODIC_SCOPE && (varname == "btcusd"_n || varname == "btc.bitmex"_n))
		balanceSupply();
}

asset bank::dusd2dps(asset dusd) {
	check(dusd.symbol == DUSD, "wrong symbol in dusd2dps()");
	stats dpsStats(_self, DPS.code().raw());
	asset dpsSupply = dpsStats.require_find(DPS.code().raw())->supply;

	variables vars(BANKACCOUNT, SYSTEM_SCOPE.value);
	double rate = dusdPrecision / dpsPrecision * 1e-8 * vars.require_find(("dps.price"_n).value, "undefined DPS sale price")->value;

	return {static_cast<int64_t>(std::round(dusd.amount / rate)), DPS};
}

asset bank::dps2dusd(asset dps) {
	check(dps.symbol == DPS, "wrong symbol in dps2dusd()");
	variables vars(BANKACCOUNT, SYSTEM_SCOPE.value);

	auto redeemEnableTime = vars.require_find(("dps.redeem"_n).value, "undefined dps redeem enable time")->value;
	check(current_time_point().time_since_epoch().count() >= redeemEnableTime, "dps redeem not enabled");

	double redeemFee = 0.0;
	auto fee_itr = vars.find(("dps.fee"_n).value);
	if(fee_itr != vars.end())
		redeemFee = fee_itr->value * 1e-10;

	stats dps_stats(_self, DPS.code().raw());
	accounts issuer_balances(_self, BANKACCOUNT.value);
	
	asset reserveFund = issuer_balances.require_find(DUSD.code().raw())->balance;
	asset dpsInCirculation = 
		dps_stats.require_find(DPS.code().raw())->supply -
		issuer_balances.require_find(DPS.code().raw())->balance;

	double rate = ((1.0 - redeemFee) * reserveFund.amount) / dpsInCirculation.amount;
	return {static_cast<int64_t>(std::round(rate * dps.amount)), DUSD};
}

asset bank::satoshi2dusd(int64_t satoshi_amount) {
	variables sys_vars(BANKACCOUNT, SYSTEM_SCOPE.value);
	variables periodic_vars(BANKACCOUNT, PERIODIC_SCOPE.value);
	// "btcusd", "fee.mint" variables are stored in scale 1e8
	double mintFee = 1e-8 * sys_vars.require_find(("fee.mint"_n).value, "fee.mint (mint fee in percent) variable not found")->value;
	double rate = (100.0 - mintFee) * 1e-10 * periodic_vars.require_find(("btcusd"_n).value, "btcusd (exchange rate) variable not found")->value;
	int64_t amount = std::round(rate * satoshi_amount / 1e6); // hardcode: DUSD precision is 2
	return {amount, DUSD};
}

int64_t bank::dusd2satoshi(asset dusd) {
	check(dusd.symbol == DUSD, "wrong symbol in dusd2satoshi()");
	variables sys_vars(BANKACCOUNT, SYSTEM_SCOPE.value);
	variables periodic_vars(BANKACCOUNT, PERIODIC_SCOPE.value);
	// "btcusd", "fee.redeem" variables are stored in scale 1e8
	double redeemFee = 1e-8 * sys_vars.require_find(("fee.redeem"_n).value, "fee.redeem (redemption fee) variable not found")->value;
	double rate = (100 + redeemFee) * 1e-10 * periodic_vars.require_find(("btcusd"_n).value, "btcusd (exchange rate) variable not found")->value;
	int64_t satoshi_amount = std::round(1e6 * dusd.amount / rate); // hardcode: DUSD precision is 2
	return satoshi_amount;
}

void bank::splitToDev(const asset& quantity, asset& toReserve, asset& toDev) {
	variables vars(BANKACCOUNT, SYSTEM_SCOPE.value);
	double devRatio = 0.0; // default development ratio = 0
	auto var_itr = vars.find(("dev.percent"_n).value);
	if(var_itr != vars.end())
		devRatio = 1e-10 * var_itr->value;

	toDev = asset(std::round(quantity.amount * devRatio / (1 + devRatio)), quantity.symbol);
	toReserve = quantity - toDev;
}

void bank::balanceSupply() {
	// TODO: consider in-flight redeem transactions to bitmex account

	variables vars(_self, PERIODIC_SCOPE.value);

	auto itr = vars.find("btc.bitmex"_n.value);
	if(itr == vars.end()) return;
	int64_t bitmexSatoshis = itr->value;

	itr = vars.find("btcusd"_n.value);
	if(itr == vars.end()) return;

	accounts accnt(CUSTODIAN, BANKACCOUNT.value);
	auto accnt_itr = accnt.find(DBTC.code().raw());
	if(accnt_itr == accnt.end()) return;

	// hardcode: DUSD and BTC precision difference is -6, BTC/USD rate is 8 digits up
	int64_t targetSupplyCents = std::round(1e-14 * (accnt_itr->balance.amount + bitmexSatoshis) * itr->value);

	variables sysvars(_self, SYSTEM_SCOPE.value);

	int64_t maxSupplуErrorCents = 0;
	itr = sysvars.find("maxsupperror"_n.value);
	if(itr != sysvars.end()) maxSupplуErrorCents = itr->value / 1000000;

	stats statstable(_self, DUSD.code().raw());
	auto& st = statstable.get(DUSD.code().raw());

	int64_t supplyErrorCents = st.supply.amount - targetSupplyCents;
	if(supplyErrorCents && supplyErrorCents >= maxSupplуErrorCents) {
		SEND_INLINE_ACTION(*this, retire, {{_self, "active"_n}}, {{supplyErrorCents, DUSD}, "supply balancing"});
	}
	else if(supplyErrorCents && supplyErrorCents <= -maxSupplуErrorCents) {
		SEND_INLINE_ACTION(*this, issue, {{_self, "active"_n}}, {BANKACCOUNT, {-supplyErrorCents, DUSD}, "supply balancing"});
	}
}
