#pragma once

using namespace eosio;
using namespace std;

#include <eosio/eosio.hpp>
#include <eosio/asset.hpp>
#include <eosio/system.hpp>
#include <eosio/crypto.hpp>
#include <cmath>
#include <string>
#include <vector>
#include <cctype>
#include <stable.coin.hpp>
#include "dbonds_tables.hpp"

#define err 1e-7

bool lt(double x, double y) {
	return x + err < y;
}

bool gt(double x, double y) {
	return x - err > y;
}
bool eq(double x, double y) {
	return !lt(x, y) && !gt(x,y);
}
bool leq(double x, double y) {
	return !gt(x,y);
}
bool geq(double x, double y) {
	return !lt(x,y);
}

bool match_memo(const string& memo, const string& pattern, bool ignore_case = true) {
	if(!ignore_case)
		return memo == pattern;
	
	if(memo.size() != pattern.size())
		return false;
	for(auto i1 = memo.begin(), i2 = pattern.begin(); i1 != memo.end(); i1++, i2++)
		if(tolower(*i1) != tolower(*i2))
			return false;
	return true;
}

void split_memo(const string& memo, string& word1, string& word2) {
	size_t end = memo.find(' ');
	word1 = memo.substr(0, end);
	word2.clear();
	if(end != std::string::npos) {
		size_t begin = memo.find_first_not_of(' ', end);
		end = memo.find(' ', begin);
		word2 = memo.substr(begin, end);
	}
}

bool is_dusd_mint_transfer(name token_contract, name from, asset quantity, const string& memo) {
	if(memo.size() < 5)
		return false;
	return
		token_contract == CUSTODIAN &&
		quantity.symbol == DBTC &&
		memo.substr(memo.size() - 5) == " DUSD";
}

bool is_technical_transfer(name token_contract, name from, asset quantity, const string& memo) {
	return token_contract == CUSTODIAN && quantity.symbol == DBTC;
}

int64_t get_variable(const string & _name, const name & scope) {
	variables table(BANKACCOUNT, scope.value);
	return table.get(name(_name).value, (_name + string(" variable not found")).c_str()).value;
}

time_point get_var_upd_time(const string & _name, const name & scope) {
	variables table(BANKACCOUNT, scope.value);
	return table.get(name(_name).value, (_name + string(" variable not found")).c_str()).mtime;
}

bool is_dusd_mint(name from, name to, extended_asset quantity, const string & memo) {
	if( to == BANKACCOUNT
		&& quantity.quantity.symbol == DBTC
		&& quantity.contract == CUSTODIAN
		&& match_memo(memo, "Buy DUSD"))
		return true;
	return false;
}

bool is_dusd_redeem(name from, name to, extended_asset quantity, const string & memo) {
	if( to == BANKACCOUNT
		&& quantity.quantity.symbol == DUSD
		&& quantity.contract == BANKACCOUNT
		&& match_memo(memo, "Redeem for DBTC"))
		return true;
	return false;
}

bool is_user_exchange(name from, name to, extended_asset quantity, const string & memo) {
	return is_dusd_mint(from, to, quantity, memo) || is_dusd_redeem(from, to, quantity, memo);
}

int64_t get_btc_price() {
	// returns price in cents
	int64_t value = get_variable("btcusd", PERIODIC_SCOPE) / 1e6;
	return value;
}

int64_t get_usd_value(asset quantity) {
	// returns value in cents
	if(quantity.symbol == DBTC || quantity.symbol == BTC)
	{
		double btc_price = 1.0 * get_btc_price();
		double btc_amount = 1.0 * quantity.amount / 100000000;
		return int64_t(round(btc_amount * btc_price));
	}
	if(quantity.symbol == DUSD)
		return quantity.amount;
	if(quantity.symbol == EOS) {
		double eos_price = get_variable("eosusd", PERIODIC_SCOPE) * 1e-6;
		double eos_amount = quantity.amount * 1e-4;
		return int64_t(round(eos_amount * eos_price));
	}
	fail("get_usd_value not supported with this asset");
}

int64_t get_usd_value(extended_asset quantity) {
	// returns value in cents
	if((quantity.quantity.symbol == DBTC || quantity.quantity.symbol == BTC) && quantity.contract == CUSTODIAN)
	{
		double btc_price = 1.0 * get_btc_price();
		double btc_amount = 1.0 * quantity.quantity.amount / 100000000;
		return int64_t(round(btc_amount * btc_price));
	}
	if(quantity.quantity.symbol == DUSD && quantity.contract == BANKACCOUNT)
		return quantity.quantity.amount;
	if(quantity.quantity.symbol == EOS && quantity.contract == EOSIOTOKEN){
		double eos_price = get_variable("eosusd", PERIODIC_SCOPE) * 1e-6;
		double eos_amount = quantity.quantity.amount * 1e-4;
		return int64_t(round(eos_amount * eos_price));
	}
	// TODO: "quantity" may be dbond:
	extended_asset dbond_price = dbonds::get_price(quantity.contract, quantity.quantity.symbol.code());
	check(dbond_price.contract == BANKACCOUNT && dbond_price.quantity.symbol == DUSD, "get_usd_value not supported with this asset");
	return quantity.quantity.amount * dbond_price.quantity.amount / pow(10, quantity.quantity.symbol.precision());
}

int64_t get_balance(name user, const symbol token) {
	// returns balance:
	// BTC/DBTC in satoshi
	// USD/DUSD in cents
	if(token == BTC){
		variables periodic(BANKACCOUNT, PERIODIC_SCOPE.value);
		if(user == BITMEXACC){	
			return periodic.get("btc.bitmex"_n.value, "btc.bitmex variable not found").value;
		}
		if(user == CUSTODIAN){
			return periodic.get("btc.cold"_n.value, "btc.cold variable not found").value;
		}
	}

	// default case token = DUSD
	name emitent = BANKACCOUNT;

	if(token == DBTC)
		emitent = CUSTODIAN;
	
	accounts acc(emitent, user.value);
	auto it = acc.find(token.code().raw());
	print("\n balance ", user, it->balance);
	if(it == acc.end())
		return 0;
	return it->balance.amount;
}

int64_t get_hedge_assets_value() {
	int64_t dbtc_balance = get_balance(BANKACCOUNT, DBTC);
	int64_t bitmex_balance = get_balance(BITMEXACC, BTC);
	return get_usd_value(asset(dbtc_balance, DBTC)) + get_usd_value(asset(bitmex_balance, BTC));
}

int64_t get_liquidity_pool_value() {
	return get_hedge_assets_value() - get_usd_value(asset(get_balance(BITMEXACC, BTC), BTC));
}

double get_hard_margin(double soft_margin) {
	return 0.5 * soft_margin;
}

bool is_dbond_contract(name contract) {
	variables dbonds_contracts(BANKACCOUNT, DBONDS_SCOPE.value);
	auto existing = dbonds_contracts.find(contract.value);
	return existing != dbonds_contracts.end();
}

int64_t get_dbonds_assets_value() {
	int64_t result = 0;
	variables dbonds_contracts(BANKACCOUNT, DBONDS_SCOPE.value);
	// iterate over dbonds contracts
	for(const auto& dbonds_contract : dbonds_contracts) {
		vector<asset> dbond_assets;
		dbonds::get_holder_dbonds(dbonds_contract.var_name, BANKACCOUNT, dbond_assets);
		int64_t one_contract_dbonds_value = 0;
		// iterate over dbonds owned by bank
		for(const auto& db : dbond_assets) {
			extended_asset price = dbonds::get_price(dbonds_contract.var_name, db.symbol.code());
			if(price.contract == BANKACCOUNT && price.quantity.symbol == DUSD)
				one_contract_dbonds_value += db.amount * price.quantity.amount / pow(10, db.symbol.precision());
		}
		// save value of all dbonds for each dbonds contract
		dbonds_contracts.modify(dbonds_contract, BANKACCOUNT, [&](auto& v) {
			v.value = one_contract_dbonds_value;
			v.mtime = current_time_point();
		});
		result += one_contract_dbonds_value;
	}
	return result;
}

int64_t get_bank_assets_value() {
	// calculate BTC value
	int64_t btc_balance = get_balance(BITMEXACC, BTC) + get_balance(BANKACCOUNT, DBTC);

	return get_usd_value(asset(btc_balance, DBTC)) + get_dbonds_assets_value();
}

void set_stat(const string & stat, int64_t value) {
	variables stats(BANKACCOUNT, STAT_SCOPE.value);
	auto stat_itr = stats.find(name(stat).value);

	if(stat_itr == stats.end()) {
		stats.emplace(BANKACCOUNT, [&](auto& new_stat) {
			new_stat.var_name = name(stat);
			new_stat.value = value;
			new_stat.mtime = current_time_point();
		});
	}
	else{
		stats.modify(stat_itr, BANKACCOUNT, [&](auto& var) {
			var.value = value;
			var.mtime = current_time_point();
		});
	}
}

int64_t get_bank_capital_value() {
	return get_balance(BANKACCOUNT, DUSD);
}

int64_t get_supply(const symbol & token) {
	print("\ngot here 15");
	if(token == DUSD || token == DPS){
		stats st = stats(BANKACCOUNT, token.code().raw());
		return st.get(token.code().raw()).supply.amount;
	}
	if(token == DBTC){
		stats st = stats(CUSTODIAN, token.code().raw());
		return st.get(token.code().raw()).supply.amount;
	}
	fail("token of that type not supported");
	return 0;
}



asset dusd2dps(asset dusd) {
	check(dusd.symbol == DUSD, "wrong symbol in dusd2dps()");
	stats dpsStats(BANKACCOUNT, DPS.code().raw());
	asset dpsSupply = dpsStats.require_find(DPS.code().raw())->supply;

	double rate = dusdPrecision / dpsPrecision * 1e-8 * get_variable("dps.price", PERIODIC_SCOPE);

	return {static_cast<int64_t>(std::round(dusd.amount / rate)), DPS};
}

asset dps2dusd(asset dps) {
	check(dps.symbol == DPS, "wrong symbol in dps2dusd()");

	auto redeemEnableTime = get_variable("dps.redeem", SYSTEM_SCOPE);
	check(current_time_point().time_since_epoch().count() >= redeemEnableTime, "dps redeem not enabled");

	double redeemFee = 0.0;
	auto fee_raw = get_variable("dps.fee", SYSTEM_SCOPE);
	redeemFee = fee_raw * 1e-10;

	stats dps_stats(BANKACCOUNT, DPS.code().raw());
	accounts issuer_balances(BANKACCOUNT, BANKACCOUNT.value);
	
	asset reserveFund = issuer_balances.require_find(DUSD.code().raw())->balance;
	asset dpsInCirculation = 
		dps_stats.require_find(DPS.code().raw())->supply -
		issuer_balances.require_find(DPS.code().raw())->balance;

	double rate = ((1.0 - redeemFee) * reserveFund.amount) / dpsInCirculation.amount;
	return {static_cast<int64_t>(std::round(rate * dps.amount)), DUSD};
}

asset satoshi2dusd(int64_t satoshi_amount) {
	variables sys_vars(BANKACCOUNT, SYSTEM_SCOPE.value);
	variables periodic_vars(BANKACCOUNT, PERIODIC_SCOPE.value);
	// "btcusd", "fee.mint" variables are stored in scale 1e8
	double mintFee = 1e-8 * sys_vars.require_find(("fee.mint"_n).value, "fee.mint (mint fee in percent) variable not found")->value;
	double rate = (100.0 - mintFee) * 1e-10 * periodic_vars.require_find(("btcusd"_n).value, "btcusd (exchange rate) variable not found")->value;
	int64_t amount = std::round(rate * satoshi_amount / 1e6); // hardcode: DUSD precision is 2
	return {amount, DUSD};
}

int64_t dusd2satoshi(asset dusd) {
	check(dusd.symbol == DUSD, "wrong symbol in dusd2satoshi()");
	variables sys_vars(BANKACCOUNT, SYSTEM_SCOPE.value);
	variables periodic_vars(BANKACCOUNT, PERIODIC_SCOPE.value);
	// "btcusd", "fee.redeem" variables are stored in scale 1e8
	double redeemFee = 1e-8 * sys_vars.require_find(("fee.redeem"_n).value, "fee.redeem (redemption fee) variable not found")->value;
	double rate = (100 + redeemFee) * 1e-10 * periodic_vars.require_find(("btcusd"_n).value, "btcusd (exchange rate) variable not found")->value;
	int64_t satoshi_amount = std::round(1e6 * dusd.amount / rate); // hardcode: DUSD precision is 2
	return satoshi_amount;
}

int64_t bitmex_in_process_redeem_order_btc_amount(name user) {
	redeemOrders ord(CUSTODIAN, DBTC.code().raw());
	auto status_index = ord.get_index<"status"_n>();

	int64_t amount = 0;
	auto lower = status_index.lower_bound("processing"_n.value);
	auto upper = status_index.upper_bound("processing"_n.value);
	if(lower != status_index.end()) {
		do {
			if(lower->user == user)
				amount += lower->btc_amount;
		} while(lower++ != upper);
	}

	return amount;
}

int64_t bitmex_in_process_mint_order_btc_amount(name user) {
	mintOrders ord(CUSTODIAN, DBTC.code().raw());
	auto status_index = ord.get_index<"status"_n>();

	int64_t amount = 0;
	auto lower = status_index.lower_bound("processing"_n.value);
	auto upper = status_index.upper_bound("processing"_n.value);
	if(lower != status_index.end()) {
		do {
			if(lower->user == user)
				amount += lower->btc_amount;
		} while(lower++ != upper);
	}

	return amount;
}

uint64_t pow(uint64_t x, uint64_t p) {
	if(p == 0)
		return 1;
	if(p & 1) {
		return x * pow(x, p-1);
	}
	else {
		uint64_t res = pow(x, p/2);
		return res * res;
	}
}
