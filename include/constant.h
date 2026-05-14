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

} // namespace constant