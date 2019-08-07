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

void on_lack_of_capital(){
	print("\n====== handle on_lack_of_capital");
	return;
}

void on_switcher_check_fail(){
	print("\n====== handle on_switcher_check_fail");
	return;
}

void on_lack_of_liquidity(){
	print("\n====== handle on_lack_of_liquidity");
	return;
}

void on_high_leverage(){
	print("\n====== handle on_high_leverage");
	return;
}