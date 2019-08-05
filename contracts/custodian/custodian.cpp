/**
 *  bank.cpp derived from eosio.token.cpp
 *  @copyright https://github.com/EOSIO/eos/blob/master/LICENSE
 */

#include <custodian.hpp>
#include <utility.hpp>
#include <limitations.hpp>
#include <eosio/crypto.hpp>
#include <eosio/print.hpp>
#include <cctype>
#include <cmath>

using namespace eosio;

ACTION custodian::transfer( name    from,
						name    to,
						asset   quantity,
						string  memo )
{
	print("\ngot here 0");
	
	check_main_switch();

	check( from != to, "cannot transfer to self" );
	require_auth( from );
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

	if(to == CUSTODIAN && sym == DBTC.code()) {
		validate_btc_address(memo, BITCOIN_TESTNET);
		redeemOrders ord(_self, sym.raw());

		ord.emplace(_self, [&](auto& o) {
			o.id         = ord.available_primary_key();
			o.user       = from;
			o.status     = "new"_n;
			o.btc_amount = quantity.amount;
			o.btc_txid   = uint256_t();
			o.mtime      = current_time_point().time_since_epoch().count();
			o.btc_address = memo;
		});
	}

	auto payer = has_auth( to ) ? to : from;

	sub_balance( from, quantity );
	add_balance( to, quantity, payer );
}

ACTION custodian::ontransfer(name from, name to, asset quantity, const string& memo) {
	print("custodian 'ontransfer'. self: ", get_self(), " first receiver: ", get_first_receiver());

	if(to == CUSTODIAN) {
		if(quantity.symbol == DUSD || quantity.symbol == DPS) {
			fail("for redemption, send DPS and DUSD to 'thedeposbank' account");
		}
	}
}

ACTION custodian::mint(name user, symbol_code sym, int64_t satoshi_amount, const string& btc_txid) {
	check(sym.is_valid(), "invalid symbol name");
	check(sym == DBTC.code() || sym == DUSD.code() || sym == DPS.code(), "unknown token symbol");
	
	check_main_switch();

	uint256_t txid_bin = hex2bin(btc_txid);

	stats statstable(_self, DBTC.code().raw());

	auto stat_it = statstable.find(DBTC.code().raw());
	check( stat_it != statstable.end(), "token with symbol does not exist, create token before issue" );
	const auto& st = *stat_it;
	require_auth(CUSTODIAN);

	mintOrders ord(_self, sym.raw());

	if(user == CUSTODIAN) {
		// processing hedge tx from bitmex to custody
		auto status_index = ord.get_index<"status"_n>();
		const auto& order = status_index.get("new.bitmex"_n.value, "no new bitmex order");
		ord.modify(order, same_payer, [&](auto& o) {
			o.status = "processing"_n;
			o.btc_txid = txid_bin;
		});
	} else {

		// processing mint order
		auto txid_index = ord.get_index<"btctxid"_n>();
		auto itr = txid_index.find(txid_bin);

#ifdef DEBUG
		// special case for deleting mint orders: satoshi_amount == -1
		if(satoshi_amount == -1) {
			check(itr != txid_index.end(), "no record to erase!");
			ord.erase(*itr);
			return;
		}
#endif

		check(itr == txid_index.end(), "duplicate mint!");

		ord.emplace(CUSTODIAN, [&](auto& o) {
			o.id         = ord.available_primary_key();
			o.user       = user;
			o.status     = "new"_n;
			o.btc_amount = satoshi_amount;
			o.btc_txid   = txid_bin;
			o.mtime      = current_time_point().time_since_epoch().count();
		});
	}

	asset dbtcQuantity(satoshi_amount, DBTC);

	if(sym == DBTC.code()) {
		SEND_INLINE_ACTION(*this, issue, {{CUSTODIAN, "active"_n}}, {user, dbtcQuantity, btc_txid});
	}
	else {
		string memo = user.to_string() + " " + sym.to_string();
		SEND_INLINE_ACTION(*this, issue, {{CUSTODIAN, "active"_n}}, {BANKACCOUNT, dbtcQuantity, memo});
	}
}

ACTION custodian::redeem(symbol_code sym, uint64_t order_id, const string& btc_txid) {
	require_auth(CUSTODIAN);

	check_main_switch();
	
	redeemOrders ord(_self, sym.raw());
	auto& order = *(ord.require_find(order_id, "order not found"));

	uint256_t txid_bin = hex2bin(btc_txid);

#ifdef DEBUG
	if(txid_bin == uint256_t()) {
		// for txid == 0 there is special case: delete order
		ord.erase(order);
		return;
	}
#endif

	check(order.status == "new"_n || order.status == "new.bitmex"_n, "redeem order is not new");

	ord.modify(order, same_payer, [&](auto& o) {
		o.status = "processing"_n;
		o.btc_txid = txid_bin;
	});

	asset dbtcQuantity(order.btc_amount, DBTC);

	SEND_INLINE_ACTION(*this, retire, {{CUSTODIAN, "active"_n}}, {dbtcQuantity, btc_txid});
}

ACTION custodian::balancehedge() {
	// do we need to authorize? it seems, no.
	// CPU & bandwidth are paid by caller, RAM is spent only for processed orders

	auto btcPrice = get_btc_price();

	variables sys_vars(BANKACCOUNT, SYSTEM_SCOPE.value);
	double bitmexTarget = sys_vars.get("bitmex.trg"_n.value, "bitmex.trg not found").value * 1e-10;
	double bitmexMin    = sys_vars.get("bitmex.min"_n.value, "bitmex.min not found").value * 1e-10;
	double bitmexMax    = sys_vars.get("bitmex.max"_n.value, "bitmex.max not found").value * 1e-10;

	variables periodic_vars(BANKACCOUNT, PERIODIC_SCOPE.value);
	int64_t bitmexAmount = 1e-8 * btcPrice * periodic_vars.get("btc.bitmex"_n.value, "btc.bitmex not found").value;
	int64_t fullAmount   = bitmexAmount + get_hedge_assets_value();
	int64_t targetAmount = bitmexTarget * fullAmount;

	if(bitmexAmount < bitmexMin * fullAmount) {
		redeemOrders ord(_self, DBTC.code().raw());
		auto status_index = ord.get_index<"status"_n>();

		// if accepted order exists, do nothing
		if(status_index.find("acc.bitmex"_n.value) != status_index.end())
			return;
		
		int64_t orderAmount = 1e8 * (targetAmount - bitmexAmount) / btcPrice;
		// check if new order exists already
		auto balanceOrderItr = status_index.find("new.bitmex"_n.value);
		if(balanceOrderItr == status_index.end()) {
			ord.emplace(CUSTODIAN, [&](auto& o) {
				o.id          = ord.available_primary_key();
				o.user        = CUSTODIAN;
				o.status      = "new.bitmex"_n;
				o.btc_amount  = orderAmount;
				o.btc_txid    = uint256_t();
				o.mtime       = current_time_point().time_since_epoch().count();
				o.btc_address = string();
			});
		} else {
			ord.modify(*balanceOrderItr, same_payer, [&](auto& o) {
				o.btc_amount  = orderAmount;
			});
		}
	}

	if(bitmexAmount > bitmexMax * fullAmount) {
		mintOrders ord(_self, DBTC.code().raw());
		auto status_index = ord.get_index<"status"_n>();

		// if accepted order exists, do nothing
		if(status_index.find("acc.bitmex"_n.value) != status_index.end())
			return;

		int64_t orderAmount = 1e8 * (bitmexAmount - targetAmount) / btcPrice;
		// check if new order exists already
		auto balanceOrderItr = status_index.find("new.bitmex"_n.value);
		if(balanceOrderItr == status_index.end()) {
			ord.emplace(CUSTODIAN, [&](auto& o) {
				o.id          = ord.available_primary_key();
				o.user        = CUSTODIAN;
				o.status      = "new.bitmex"_n;
				o.btc_amount  = orderAmount;
				o.btc_txid    = uint256_t();
				o.mtime       = current_time_point().time_since_epoch().count();
			});
		} else {
			ord.modify(*balanceOrderItr, same_payer, [&](auto& o) {
				o.btc_amount  = orderAmount;
			});
		}
	}
}
