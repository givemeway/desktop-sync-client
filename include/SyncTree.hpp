#include "types.hpp"
#include <memory>
#include <mutex>
#include <unordered_map>
#include <vector>
namespace sync_app {

struct SyncNode : public std::enable_shared_from_this<SyncNode> {
public:
  std::string name;
  std::string inode;
  std::string uuid = "";
  std::string hash = "";
  std::string last_modified = "";
  std::string lastSynced = "";

  bool isDir;
  bool isDirty = false;

  std::weak_ptr<SyncNode> parent;
  std::unordered_map<std::string, std::shared_ptr<SyncNode>> children;
  SyncNode(std::string n, std::string i, bool d,
           std::shared_ptr<SyncNode> p = nullptr);
  SyncNode(std::string n, std::string i, bool d, SyncMetadata &meta,
           std::shared_ptr<SyncNode> p = nullptr);
  ~SyncNode();
  std::string getFullPath() const;
};

class SyncTree {

private:
  std::shared_ptr<SyncNode> m_root;
  std::recursive_mutex m_mtx;
  //  std::unordered_map<std::string, std::shared_ptr<SyncNode>> m_inodeTreeMap;
  std::vector<std::string> splitPath(const std::string &path);
  void printTree(std::shared_ptr<SyncNode> &node, int indent);

public:
  SyncTree(const std::string &syncPath, SyncMetadata &meta);
  SyncTree(const std::string &syncPath);
  ~SyncTree();
  std::shared_ptr<SyncNode> findByPath(const std::string &path);
  std::shared_ptr<SyncNode> findByInode(const std::string &inode);
  void insertPath(const std::string &path, const std::string &inode,
                  bool isDir);
  void insertSyncedPath(const SyncMetadata &meta, const std::string &path,
                        bool isDir);
  void insertInode(const std::string &path, const std::string &inode,
                   bool isDir);
  void renamePath(const std::string &newPath, const std::string &oldPath);
  void movePath(const std::string &newPath, const std::string &oldPath);
  void deletePath(const std::string &path);
  void print();
};

} // namespace sync_app
