/**
 *  bank.hpp derived from eosio.token.hpp
 *  @copyright https://github.com/EOSIO/eos/blob/master/LICENSE
 */
#pragma once

#include <eosio/asset.hpp>
#include <eosio/eosio.hpp>
#include <eosio/system.hpp>

#include <stable.coin.hpp>
#include <depostoken.hpp>
#include <limitations.hpp>

#include <string>
#include <vector>

using std::string;
using std::vector;

using namespace eosio;

CONTRACT bank : private token {
public:
	using token::token;

	ACTION create( name issuer, asset maximum_supply) {
		token::create(issuer, maximum_supply);
	}
	ACTION issue( name to, asset quantity, string memo );

	ACTION retire( asset quantity, string memo );

	ACTION transfer( name from, name to, asset quantity, string memo );

	ACTION open( name owner, const symbol& symbol, name ram_payer ) {
		token::open(owner, symbol, ram_payer);
	}

	ACTION close( name owner, const symbol& symbol ) {
		token::close(owner, symbol);
	}

	ACTION setvar(name scope, name varname, int64_t value);

	ACTION delvar(name scope, name varname) {
		token::delvar(scope, varname);
	}

	/*
	 * New token actions and methods
	 */

	/**
	 * Called by 'transfer' action notification of 'deposcustody' contract.
	 * Used to implement exchange DBTC => DUSD
	 * when <from> == 'deposcustody', see <memo> for account name to send DUSD to
	 * in future, when more stablecoins issued, <memo> will be '<symbol> <name>'
	 */
	[[eosio::on_notify("*::transfer")]]
	void ontransfer(name from, name to, asset quantity, const string& memo);

	// eosio.cdt bug workaround
    [[eosio::on_notify("dummy1234512::transfer")]]
    void dummy(name from, name to, asset quantity, const string& memo) {}

private:

	TABLE account {
		asset	 balance;

		uint64_t primary_key()const { return balance.symbol.code().raw(); }
	};

	TABLE currency_stats {
		asset	 supply;
		asset	 max_supply;
		name	 issuer;

		uint64_t primary_key()const { return supply.symbol.code().raw(); }
	};

	typedef eosio::multi_index< "accounts"_n, account > accounts;
	typedef eosio::multi_index< "stat"_n, currency_stats > stats;

	/**
	 * arbitrary data store. scopes:
	 *   "periodic" -- for setting by oracles
	 *   "system" -- for setting by admin
	 */
	TABLE variable {
		name       var_name;
		int64_t   value;
		time_point mtime;

		uint64_t primary_key()const { return var_name.value; }
	};
	
	typedef eosio::multi_index< "variables"_n, variable > variables;

	asset dusd2dps(asset dusd);
	asset dps2dusd(asset dps);
	asset satoshi2dusd(int64_t satoshi_amount);
	int64_t dusd2satoshi(asset dusd);
	void splitToDev(const asset& quantity, asset& toReserve, asset& toDev);
	void balanceSupply();
};
