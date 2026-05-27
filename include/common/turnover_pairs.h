#pragma once
// turnover 订阅管理 — O(1) 判断, 只订 trade 频道

#include <cstdint>
#include <string>
#include <unordered_set>
#include <vector>

struct TurnoverPairManager {
  using UniInstID = uint16_t;

  std::unordered_set<std::string> subscribed_str;
  std::unordered_set<UniInstID> ids;

  std::vector<std::string> filter_new(const std::vector<std::string> &pairs) {
    std::vector<std::string> out;
    for (auto &p : pairs) {
      if (subscribed_str.find(p) == subscribed_str.end()) {
        out.push_back(p);
        subscribed_str.insert(p);
      }
    }
    return out;
  }

  void add_id(UniInstID id, const std::string &inst_str = "") {
    ids.insert(id);
    if (!inst_str.empty())
      subscribed_str.insert(inst_str);
  }
  bool contains(UniInstID id) const { return ids.find(id) != ids.end(); }
  size_t size() const { return ids.size(); }
};
