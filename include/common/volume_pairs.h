#pragma once
// 动态获取各交易所 Top 成交量 pairs (用于 FP 触发)

#include <string>
#include <vector>

std::vector<std::string> fetch_all_top_pairs(int per_exch);
