#pragma once

#include <array>
#include <string>
#include <unordered_map>
#include <vector>

namespace constant {

constexpr std::array<double, 24> mean_hourly_volumes = {0.0};
constexpr std::array<double, 24> mean_hourly_volumes2 = {0.0};

const std::array<std::string, 7> FOREX = {"aud", "busd", "cad", "chf",
                                          "eur", "gbp",  "paxg"};

const std::vector<std::string> available_forex = {
    "aud", "busd", "cad", "chf", "eur", "gbp", "paxg", "usdc", "usdt", "usd"};
const std::vector<std::string> available_digital = {
    "ada",  "atom", "avax",  "bnb",  "btc", "doge", "dot", "eth",
    "link", "ltc",  "matic", "shib", "sol", "xmr",  "xrp"};
const std::vector<std::string> available_coins = {
    "aud",  "busd",  "cad",  "chf", "eur",  "gbp", "paxg", "ada",
    "atom", "avax",  "bnb",  "btc", "doge", "dot", "eth",  "link",
    "ltc",  "matic", "shib", "sol", "xmr",  "xrp"};
const std::vector<std::string> usdt_keys = {"usdt_usd.krk", "usdt_eur.krk",
                                            "eur_usd.krk"};
const std::unordered_map<std::string, std::string> usdc_dict = {
    {"krk", "usdc_usd"}, {"bn", "usdc_usdt"}, {"cb", "usdt_usd"}};

const std::vector<std::string> instruments = {
    "usdt_usd_spot.krk",     "usdt_eur_spot.krk",     "eur_usd_spot.krk",
    "usdc_eur_spot.krk",     "usdc_usd_spot.krk",     "usdc_usdt_spot.krk",
    "gbp_usd_spot.krk",      "usdt_gbp_spot.krk",     "usdc_gbp_spot.krk",
    "eur_usd_cash.idealpro", "gbp_usd_cash.idealpro", "usdc_usdt_spot.bn",
    "usdt_usd_spot.cb",      "btc_usd_spot.krk",      "btc_eur_spot.krk",
    "btc_gbp_spot.krk",      "btc_usdc_spot.krk",     "btc_usdt_spot.krk",
    "btc_usdt_spot.bn",      "btc_usd_spot.cb",       "btc_usdt_spot.ok",
    "eth_usd_spot.krk",      "eth_eur_spot.krk",      "eth_gbp_spot.krk",
    "eth_usdt_spot.krk",     "eth_usdt_spot.bn",      "eth_usd_spot.cb",
    "eth_usdt_spot.ok",      "sol_usd_spot.krk",      "sol_eur_spot.krk",
    "sol_gbp_spot.krk",      "sol_usdt_spot.krk",     "sol_usdt_spot.bn",
    "sol_usd_spot.cb",       "sol_usdt_spot.ok",      "xrp_usd_spot.krk",
    "xrp_eur_spot.krk",      "xrp_gbp_spot.krk",      "xrp_usdt_spot.krk",
    "xrp_usdt_spot.bn",      "xrp_usd_spot.cb",       "xrp_usdt_spot.ok"};
const std::vector<std::string> instruments_swap = {
    "btc_usdt_swap.bn", "eth_usdt_swap.bn", "sol_usdt_swap.bn",
    "xrp_usdt_swap.bn"};
const std::vector<std::string> trading_currencies_str = {
    "usdt", "usdc", "usd", "eur", "gbp", "btc", "eth", "sol", "xrp"};
const std::vector<std::string> digital_currencies_str = {"btc", "eth", "sol",
                                                         "xrp"};

} // namespace constant