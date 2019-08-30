/**
 *  bank.cpp derived from eosio.token.cpp
 *  @copyright https://github.com/EOSIO/eos/blob/master/LICENSE
 */

#include <bank.hpp>
#include <process_exchanges.hpp>

#include <eosio/crypto.hpp>
#include <eosio/print.hpp>
#include <cctype>
#include <cmath>
#include <algorithm>


using namespace eosio;
using namespace std;

ACTION bank::transfer( name    from,
						name    to,
						asset   quantity,
						string  memo )
{
	// print("transfer action. self: ", get_self(), " code: ", get_code(), "\n");

	check_transfer(from, to, quantity, memo);

	if((from != BANKACCOUNT && to != BANKACCOUNT) || memo == "deny") {
		process_regular_transfer(from, to, quantity, memo);
		return;
	}
	if(from == BANKACCOUNT) {
		process_service_transfer(from, to, quantity, memo);
		return;
	}


	check_main_switch();
	

	if(to == BANKACCOUNT && quantity.symbol == DUSD) {
		if(match_memo(memo,"Buy DPS"))
			process_redeem_DUSD_for_DPS(from, to, quantity, memo);
		else if(match_memo(memo, "Redeem for DBTC"))
			process_redeem_DUSD_for_DBTC(from, to, quantity, memo);
		else
			process_redeem_DUSD_for_BTC(from, to, quantity, memo);
	}
	else if(quantity.symbol == DPS) {
		// redeem DPS for DUSD, DBTC or BTC
		if(match_memo(memo, "Redeem for DUSD"))
			process_redeem_DPS_for_DUSD(from, to, quantity, memo);
		else if(match_memo(memo, "Redeem for DBTC"))
			process_redeem_DPS_for_DBTC(from, to, quantity, memo);
		else
			process_redeem_DPS_for_BTC(from, to, quantity, memo);
	}
	else fail("arbitrary transfer to bank account");

	check_on_transfer(from, to, quantity, memo);
}

ACTION bank::ontransfer(name from, name to, asset quantity, const string& memo) {
	print("bank 'ontransfer'. self: ", get_self(), " first receiver: ", get_first_receiver());
	print("bank 'ontransfer'. from: ", from.to_string(), " to: ", to.to_string(), " asset:", quantity);

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
			check(match_memo(buyerStr, "Buy"), "memo format for token buy: 'Buy <token name>'");
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
	
	check_on_transfer(from, to, quantity, memo);
	//this for check if I transfer frm thedeposbank somewhere else
	check_on_system_change();
}

ACTION bank::issue( name to, asset quantity, string memo ) {
	token::issue(to, quantity, memo);
	if(quantity.symbol == DUSD && memo != "supply balancing")
	{
		balanceSupply();
		check_on_system_change();
	}
	else
		check_on_system_change(true);
}

ACTION bank::retire( asset quantity, string memo ) {
	print("\n before retire");
	token::retire(quantity, memo);
	if(quantity.symbol == DUSD && memo != "supply balancing")
	{
		balanceSupply();
		check_on_system_change();
	}
	else
		check_on_system_change(true);
	print("\n after retire");
}

ACTION bank::setvar(name scope, name varname, int64_t value) {
	token::setvar(scope, varname, value);

	// balance DUSD supply:
	// issue or retire tokens to keep supply equal to current USD value of BTC reserves
	// do it only if reserve balances and BTC/USD rate variables are set and any of them is changed by this action
	if( scope == PERIODIC_SCOPE && (varname == "btcusd"_n || varname == "btc.bitmex"_n))
	{
		balanceSupply();
	}
	if(scope == SYSTEM_SCOPE)
		check_on_system_change(true); // change this to false
}

ACTION bank::authdbond(name dbond_contract, dbond_id_class dbond_id) {
	require_auth(ADMINACCOUNT);
	authorized_dbonds dblist(_self, dbond_contract.value);
	auto existing = dblist.find(dbond_id.raw());
	check(existing == dblist.end(), "dbond is authorized already");
	dblist.emplace(_self, [&](auto& db) {
		db.dbond = dbond_id;
	});
	action(
		permission_level{_self, "active"_n},
		dbond_contract, "confirmfcdb"_n,
		std::make_tuple(dbond_id)
	).send();
}

void bank::onfcdblist(name seller, asset quantity, extended_asset price) {
	dbond_id_class dbond_id = quantity.symbol.code();
	name dbond_contract = get_first_receiver();
	authorized_dbonds dblist(_self, dbond_contract.value);
	const auto& authdb = dblist.get(dbond_id.raw(), "anauthorized dbond on sale?");
	
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
		// in order not to fail transaction, let's retire not more than we have
		// using token::get_balance(), not ::get_balance
		int64_t to_retire = min(supplyErrorCents, get_balance(BANKACCOUNT, BANKACCOUNT, DUSD.code()).amount);
		if(to_retire == 0)
			return;

		SEND_INLINE_ACTION(*this, retire, {{_self, "active"_n}}, {{to_retire, DUSD}, "supply balancing"});
	}
	else if(supplyErrorCents && supplyErrorCents <= -maxSupplуErrorCents) {
		SEND_INLINE_ACTION(*this, issue, {{_self, "active"_n}}, {BANKACCOUNT, {-supplyErrorCents, DUSD}, "supply balancing"});
	}
}
