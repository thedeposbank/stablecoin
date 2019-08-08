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

#define err 1e-7

bool lt(double x, double y){
	return x + err < y;
}

bool gt(double x, double y){
	return x - err > y;
}
bool eq(double x, double y){
	return !lt(x, y) && !gt(x,y);
}
bool leq(double x, double y){
	return !gt(x,y);
}
bool geq(double x, double y){
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

int64_t get_variable(const string & _name, const name & scope){
	variables table(BANKACCOUNT, scope.value);
	return table.get(name(_name).value, (_name + string(" variable not found")).c_str()).value;
}

time_point get_var_upd_time(const string & _name, const name & scope){
	variables table(BANKACCOUNT, scope.value);
	return table.get(name(_name).value, (_name + string(" variable not found")).c_str()).mtime;
}

bool is_dusd_mint(name from, name to, asset quantity, const string & memo){
	if(to == BANKACCOUNT && quantity.symbol == DBTC && match_memo(memo, "Buy DUSD"))
		return true;
	return false;
}

bool is_dusd_redeem(name from, name to, asset quantity, const string & memo){
	if(to == BANKACCOUNT && quantity.symbol == DUSD && match_memo(memo, "Redeem for DBTC"))
		return true;
	return false;
}

bool is_user_exchange(name from, name to, asset quantity, const string & memo){
	return is_dusd_mint(from, to, quantity, memo) || is_dusd_redeem(from, to, quantity, memo);
}

int64_t get_btc_price(){
	// returns price in cents
	int64_t value = get_variable("btcusd", PERIODIC_SCOPE) / 1e6;
	return value;
}

int64_t get_usd_value(asset quantity){
	//returns value in cents
	if(quantity.symbol == DBTC || quantity.symbol == BTC)
	{
		double btc_price = 1.0 * get_btc_price();
		double btc_amount = 1.0 * quantity.amount / 100000000;
		return int64_t(round(btc_amount * btc_price));
	}
	if(quantity.symbol == DUSD)
		return quantity.amount;
	fail("get_usd_value not supported with this asset");
	return 0;
}

int64_t get_balance(name user, const symbol token){
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

int64_t get_hedge_assets_value(){
	int64_t dbtc_balance = get_balance(BANKACCOUNT, DBTC);
	int64_t bitmex_balance = get_balance(BITMEXACC, BTC);
	return get_usd_value(asset(dbtc_balance, DBTC)) + get_usd_value(asset(bitmex_balance, BTC));
}

int64_t get_liquidity_pool_value(){
	return get_hedge_assets_value() - get_usd_value(asset(get_balance(BITMEXACC, BTC), BTC));
}

double get_hard_margin(double soft_margin){
	return 0.5 * soft_margin;
}

int64_t get_bank_assets_value(){
	int64_t btc_balance = get_balance(BITMEXACC, BTC) + get_balance(BANKACCOUNT, DBTC);
	return get_usd_value(asset(btc_balance, DBTC));
}

void set_stat(const string & stat, int64_t value){
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

int64_t get_bank_capital_value(){
	return get_balance(BANKACCOUNT, DUSD);
}

int64_t get_supply(const symbol & token){
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

int64_t bitmex_in_process_redeem_order_btc_amount(name user) {
	redeemOrders ord(CUSTODIAN, DBTC.code().raw());
	auto status_index = ord.get_index<"status"_n>();

	int64_t amount = 0;
	auto lower = status_index.lower_bound("accepted"_n.value);
	auto upper = status_index.upper_bound("accepted"_n.value);
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
	auto lower = status_index.lower_bound("accepted"_n.value);
	auto upper = status_index.upper_bound("accepted"_n.value);
	if(lower != status_index.end()) {
		do {
			if(lower->user == user)
				amount += lower->btc_amount;
		} while(lower++ != upper);
	}

	return amount;
}
