#pragma once

using namespace eosio;
using namespace std;

#include <eosio/eosio.hpp>
#include <eosio/asset.hpp>
#include <eosio/system.hpp>
#include <eosio/crypto.hpp>
#include <string>
#include <vector>

#include <utility.hpp>

void check_main_switch() {

	auto data_timestamp = get_var_upd_time("btcusd", PERIODIC_SCOPE);
	int64_t data_age = (current_time_point() - data_timestamp).to_seconds();
	int64_t max_data_age = get_variable("maxdataage", PERIODIC_SCOPE);

	auto btcusd = get_btc_price();
	auto btcusd_low = get_variable("btcusd.low", PERIODIC_SCOPE);
	auto btcusd_high = get_variable("btcusd.high", PERIODIC_SCOPE);

	auto sw_onchain = (data_age <= max_data_age) && (btcusd >= btcusd_low) && (btcusd <= btcusd_high);

	auto sw_service = get_variable("sw.service", SYSTEM_SCOPE);
	auto sw_manual  = get_variable("sw.manual", SYSTEM_SCOPE);

	check(sw_onchain && sw_service && sw_manual, "Anti-hack system is enabled. Conversions disabled, please, try later.");
}


void check_limits(name from, name to, asset quantity, const string& memo){

	if(to == BANKACCOUNT && is_user_exchange(from, to, quantity, memo)){ // develop such a function
		int64_t usd_value = get_usd_value(quantity); // develop such a function
		
		int64_t btc_price = get_btc_price();
		int64_t usd_volume_used = get_variable("volumeused", STAT_SCOPE);

		int64_t usd_order_maxlimit = get_variable("maxordersize", SYSTEM_SCOPE);
		int64_t abs_usage_max = get_variable("maxdayvol", SYSTEM_SCOPE);

		int64_t available_to_buy_dbtc = abs_usage_max - usd_volume_used;
		int64_t available_to_sell_dbtc = usd_volume_used + abs_usage_max;

		if(quantity.symbol == DBTC)
			check(usd_value < available_to_sell_dbtc, "total daily volume exceeded, try later");
		if(quantity.symbol == DUSD)
			check(usd_value < available_to_buy_dbtc, "total daily volume exceeded, try later");

		check(usd_value <= usd_order_maxlimit, "order maximum value exceeded, decrease the size");
	}
}

double check_bitmex_balance_ratio(){
	// returns share (of hedge assets) in format 0.*
	// if positive => exceeds maximum value
	// if negatime => below minimum value
	int64_t value_to_hedge = get_hedge_assets_value();
	int64_t btm_balance = get_usd_value(asset(get_balance(BITMEXACC, BTC), BTC));
	double maintainance_share = 1.0 * btm_balance / value_to_hedge;
	double btm_min = 1.0 * get_variable("bitmex.min", SYSTEM_SCOPE) / 10000000000;
	double btm_max = 1.0 * get_variable("bitmex.max", SYSTEM_SCOPE) / 10000000000;
	double btm_target = 1.0 * get_variable("bitmex.trg", SYSTEM_SCOPE) / 10000000000;

	if(maintainance_share < btm_min || maintainance_share > btm_max)
		return maintainance_share - btm_target;

	return 0.;
}



void check_liquidity(name from, name to, asset quantity, const string & memo){

	if(!is_dusd_redeem(from, to, quantity, memo))
		return;

	int64_t critical_liq_pool_value = get_critical_liq_pool_value();
	int64_t current_liq_pool = get_liquidity_pool_value();


	if(current_liq_pool - get_usd_value(quantity) < critical_liq_pool_value)
		fail("there is not enough liquidity for your order, reduce or try later");
}

void check_leverage(name from, name to, asset quantity, const string & memo){
	if(!is_dusd_mint(from, to, quantity, memo))
		return;

	double critical_bitmex_share = get_critical_btm_share();
	int64_t hedge_assets_value = get_hedge_assets_value();
	int64_t bitmex_balance_value = get_usd_value(asset(get_balance(BITMEXACC, BTC), BTC));
	int64_t critical_if_approved = int64_t(critical_bitmex_share * (hedge_assets_value + get_usd_value(quantity)));

	if(bitmex_balance_value < critical_if_approved)
		fail("at the moment minting is not available due to high demand, please, try later");
}

void check_capital(name from, name to, asset quantity, const string & memo){
	if(!is_dusd_mint(from, to, quantity, memo))
		return;
	int64_t bank_capital = get_bank_assets_value();
	int64_t dusd_supply = get_supply(DUSD);
	int64_t order_value = get_usd_value(quantity);
	double min_cap_share = 1.0 * get_variable("mincapshare", SYSTEM_SCOPE) / 10000000000;
	if(gt(min_cap_share * (dusd_supply + order_value), 1.0 * bank_capital))
		fail("System needs to increase bank capital. Please, try later.");
}

void update_statistics_on_trade(name from, name to, asset quantity, const string & memo){
	int64_t cur_volume_used = get_variable("volumeused", STAT_SCOPE);
	int64_t transaction_value = get_usd_value(quantity);
	
	if(is_dusd_mint(from, to, quantity, memo))
		set_stat("volumeused", (cur_volume_used + transaction_value) * 1000000);
	if(is_dusd_redeem(from, to, quantity, memo))
		set_stat("volumeused", (cur_volume_used - transaction_value) * 1000000);
}

void decay_used_volume(){
	int64_t max_abs_vol = get_variable("maxdayvol", SYSTEM_SCOPE);
	int64_t hourly_decay = int64_t((1.0 * max_abs_vol / 24) + 0.5);

	int64_t msec_in_hour = 3600000000;
	auto last_update_time = get_var_upd_time("volumeused", STAT_SCOPE).time_since_epoch().count();
	int64_t l_hour = last_update_time / msec_in_hour;
	int64_t r_hour = current_time_point().time_since_epoch().count() / msec_in_hour;
	int64_t n_hours = r_hour - l_hour;

	int64_t volume_used = get_variable("volumeused", STAT_SCOPE);
	int64_t sign = volume_used > 0 ? 1 : -1;
	int64_t delta = n_hours * hourly_decay;
	int64_t updated = volume_used * sign > delta ? volume_used - delta * sign : 0;

	set_stat("volumeused", updated);
}


void check_on_transfer(name from, name to, asset quantity, const string & memo){
	decay_used_volume();
	
	check_limits(from, to, quantity, memo);
	check_liquidity(from, to, quantity, memo);
	check_leverage(from, to, quantity, memo);
	check_capital(from, to, quantity, memo);

	update_statistics_on_trade(from, to, quantity, memo);
}
