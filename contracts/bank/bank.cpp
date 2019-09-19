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
using namespace dbonds;

ACTION bank::transfer(name from, name to, asset quantity, string memo)
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
#ifdef DEBUG
		else if(match_memo(memo, "debug"))
			process_regular_transfer(from, to, quantity, memo);
#endif
		else
			process_redeem_DUSD_for_BTC(from, to, quantity, memo);
	}
	else if(quantity.symbol == DPS) {
		// redeem DPS for DUSD, DBTC or BTC
		if(match_memo(memo, "Redeem for DUSD"))
			process_redeem_DPS_for_DUSD(from, to, quantity, memo);
		else if(match_memo(memo, "Redeem for DBTC"))
			process_redeem_DPS_for_DBTC(from, to, quantity, memo);
#ifdef DEBUG
		else if(match_memo(memo, "debug"))
			process_regular_transfer(from, to, quantity, memo);
#endif
		else
			process_redeem_DPS_for_BTC(from, to, quantity, memo);
	}
	else fail("arbitrary transfer to bank account");

	check_on_transfer(from, to, {quantity, BANKACCOUNT}, memo);
}

void bank::ontransfer(name from, name to, asset quantity, const string& memo) {
	name token_contract = get_first_receiver();

	if(from == _self)
		return;

	// if I recieve a dbond as collateral (payment was sent earlier)
	if(is_dbond_contract(token_contract)){
		balanceSupply();
	}
	// if DUSD mint request
	else if(is_dusd_mint_transfer(token_contract, from, quantity, memo)) {
		string buy_str, token_str;
		split_memo(memo, buy_str, token_str);
		// if request from user with via web-site with custodian involved
		if(from == CUSTODIAN) {
			process_mint_DUSD_for_DBTC(name{buy_str}, quantity);
		}
		// if on-chain request from user
		else {
			check(match_memo(buy_str, "buy"), "memo format for token buy: 'Buy <token name>'");
			process_mint_DUSD_for_DBTC(from, quantity);
		}
	}
	// if technical internal transaction (ex. rebalancing portfolio)
	else if(is_technical_transfer(token_contract, from, quantity, memo)) {
		balanceSupply();
	}
	else
		fail("transfer not allowed");
	
	check_on_transfer(from, to, {quantity, token_contract}, memo);
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
	authorized_dbonds dblist(_self, _self.value);
	auto existing = dblist.find(dbond_id.raw());
	check(existing == dblist.end(), "dbond with this dbond_id is authorized already");
	dblist.emplace(_self, [&](auto& db) {
		db.dbond = dbond_id;
		db.contract = dbond_contract;
	});
	action(
		permission_level{_self, "active"_n},
		dbond_contract, "confirmfcdb"_n,
		std::make_tuple(dbond_id)
	).send();
}

void bank::on_fcdb_trade_request(dbond_id_class dbond_id, name seller, name buyer, extended_asset recieved_asset, bool is_sell) {
	authorized_dbonds dblist(_self, _self.value);
	name dbond_contract = dblist.get(dbond_id.raw(), "unauthorized dbond").contract;

	if(!is_sell) {
	}

	fc_dbond_orders fcdb_orders(dbond_contract, dbond_id.raw());
	auto fcdb_peers_index = fcdb_orders.get_index<"peers"_n>();
	const auto& fcdb_order = fcdb_peers_index.get(dbonds::concat128(seller.value, buyer.value), "no order for this dbond_id, seller and buyer");

	extended_asset need_to_send;
	if(is_sell){
		need_to_send = fcdb_order.recieved_payment; // initialize extended asset
		need_to_send.quantity.amount = int64_t(1.0 * recieved_asset.quantity.amount / pow(10, recieved_asset.quantity.symbol.precision()) *
		 	fcdb_order.price.quantity.amount + 0.5);

		string memo = "buy " + dbond_id.to_string() + " from " + seller.to_string();
		SEND_INLINE_ACTION(*this, issue, {{_self, "active"_n}}, {dbond_contract, need_to_send.quantity, memo});
	}
	else{
		need_to_send = extended_asset(fcdb_order.recieved_quantity, dbond_contract);
		
		need_to_send.quantity.amount = int64_t(1.0 * recieved_asset.quantity.amount / pow(10, recieved_asset.quantity.symbol.precision()) /
		 	fcdb_order.price.quantity.amount + 0.5);

		accounts acc(dbond_contract, _self.value);
		auto row = acc.find(need_to_send.quantity.symbol.code().raw());
		if(row == acc.end())
			check(false, "no such dbond on balance");
		else
			need_to_send = min(need_to_send, extended_asset(row->balance, need_to_send.contract));
		

		string memo = string{"sell "} + dbond_id.to_string() + string{" to "} + seller.to_string();
		
		action(
			permission_level{_self, "active"_n},
			dbond_contract, "transfer"_n,
			std::make_tuple(_self, dbond_contract, need_to_send, memo)
		).send();
	}
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

	// variables vars(_self, PERIODIC_SCOPE.value);

	// auto itr = vars.find("btc.bitmex"_n.value);
	// if(itr == vars.end()) return;
	// int64_t bitmexSatoshis = itr->value;

	// itr = vars.find("btcusd"_n.value);
	// if(itr == vars.end()) return;

	// accounts accnt(CUSTODIAN, BANKACCOUNT.value);
	// auto accnt_itr = accnt.find(DBTC.code().raw());
	// if(accnt_itr == accnt.end()) return;

	// hardcode: DUSD and BTC precision difference is -6, BTC/USD rate is 8 digits up
	// int64_t targetSupplyCents = std::round(1e-14 * (accnt_itr->balance.amount + bitmexSatoshis) * itr->value);
	int64_t targetSupplyCents = get_bank_assets_value();

	variables sysvars(_self, SYSTEM_SCOPE.value);

	int64_t maxSupplуErrorCents = 0;
	auto itr = sysvars.find("maxsupperror"_n.value);
	if(itr != sysvars.end())
		maxSupplуErrorCents = itr->value / 1000000;

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
