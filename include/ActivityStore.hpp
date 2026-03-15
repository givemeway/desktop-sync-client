#pragma once

#include "types.hpp"
#include <mutex>
namespace sync_app {

class ActivityStore {
private:
  std::map<std::string, SyncItem> m_activities;
  std::mutex m_activityMutex;
  using activityType = std::map<std::string, SyncItem>;

public:
  void addActivity(const std::string &key, const SyncItem &syncItem) {
    std::lock_guard<std::mutex> lock(m_activityMutex);
    m_activities[key] = syncItem;
  }

  bool updateActivity(const std::string &key, const SyncItem &syncItem) {
    std::lock_guard<std::mutex> lock(m_activityMutex);
    auto it = m_activities.find(key);
    if (it != m_activities.end()) {
      m_activities[key] = syncItem;
      return true;
    } else {
      return false;
    }
  }

  bool removeActivity(const std::string &key) {
    std::lock_guard<std::mutex> lock(m_activityMutex);
    auto it = m_activities.find(key);
    if (it != m_activities.end()) {
      m_activities.erase(key);
      return true;
    } else {
      return false;
    }
  }

  activityType getActivities() {
    std::lock_guard<std::mutex> lock(m_activityMutex);
    return m_activities;
  }

  std::optional<SyncItem> getActivity(const std::string &key) {
    std::lock_guard<std::mutex> lock(m_activityMutex);
    auto it = m_activities.find(key);
    if (it != m_activities.end()) {
      return it->second;
    } else {
      return std::nullopt;
    }
  }
};

} // namespace sync_app
