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
#include <stable.coin.hpp>

#define err 1e-7

bool le(double x, double y){
	return x + err < y;
}

bool ge(double x, double y){
	return x - err > y;
}
bool eq(double x, double y){
	return !le(x, y) && !ge(x,y);
}
bool leq(double x, double y){
	return !ge(x,y);
}
bool geq(double x, double y){
	return !le(x,y);
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
	if(to == BANKACCOUNT && quantity.symbol == DBTC && memo == "Buy DUSD")
		return true;
	return false;
}

bool is_dusd_redeem(name from, name to, asset quantity, const string & memo){
	if(to == BANKACCOUNT && quantity.symbol == DBTC && memo == "Redeem DUSD")
		return true;
	return false;
}

bool is_user_exchange(name from, name to, asset quantity, const string & memo){
	return is_dusd_mint(from, to, quantity, memo) || is_dusd_redeem(from, to, quantity, memo);
}

int64_t get_btc_price(){
	// returns price in cents
	int64_t value = get_variable("btcusd", PERIODIC_SCOPE) / 1000000;
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
	int64_t factor = 1000000;

	if(token == DBTC)
	{
		emitent = CUSTODIAN;
		factor = 1;
	}
	
	accounts acc(emitent, user.value);
	auto it = acc.find(token.code().raw());
	if(it == acc.end())
		return 0;
	return it->balance.amount / factor;
}

int64_t get_hedge_assets_value(){
	int64_t dbtc_balance = get_balance(BANKACCOUNT, DBTC);
	int64_t bitmex_balance = get_balance(BITMEXACC, BTC);
	return get_usd_value(asset(dbtc_balance, DBTC)) + get_usd_value(asset(bitmex_balance, BTC));
}

int64_t get_liquidity_pool_value(){
	return get_hedge_assets_value() - get_usd_value(asset(get_balance(BITMEXACC, BTC), BTC));
}

int64_t get_critical_liq_pool_value(){
	double btm_critical_max = 1.0 * get_variable("bitmex.max", SYSTEM_SCOPE) / 10000000000 / 2 + 0.5;
	return int64_t(round((1 -  btm_critical_max) * get_hedge_assets_value()));
}

double get_critical_btm_share(){
	return 0.5 * get_variable("bitmex.min", SYSTEM_SCOPE);
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

int64_t get_supply(const symbol token){
	if(token == DUSD || token == DPS){
		stats st = stats(BANKACCOUNT, token.raw());
		int64_t factor = token == DUSD ? 1000000 : 1;
		return st.get(token.raw()).supply.amount / factor;
	}
	if(token == DBTC){
		stats st = stats(CUSTODIAN, token.raw());
		return st.get(token.raw()).supply.amount;
	}
	fail("token of that type not supported");
	return 0;
}

