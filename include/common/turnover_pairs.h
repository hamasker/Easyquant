#pragma once
// turnover 订阅管理 — 只订 trade 频道, 去重, 与做市订阅隔离

#include <string>
#include <unordered_set>
#include <vector>

struct TurnoverPairManager {
  std::unordered_set<std::string> subscribed; // 已订阅的 inst_str

  // 返回新增的 pair (未订阅过的)
  std::vector<std::string> filter_new(const std::vector<std::string> &pairs) {
    std::vector<std::string> out;
    for (auto &p : pairs) {
      if (subscribed.find(p) == subscribed.end()) {
        out.push_back(p);
        subscribed.insert(p);
      }
    }
    return out;
  }

  size_t size() const { return subscribed.size(); }
};
