#include "SyncTree.hpp"
#include "Utility.hpp"
#include <filesystem>
#include <iostream>
namespace fs = std::filesystem;

namespace sync_app {

SyncNode::SyncNode(std::string n, std::string i, bool d,
                   std::shared_ptr<SyncNode> p)
    : name(n), inode(i), isDir(d), parent(p) {};

SyncNode::~SyncNode() = default;

std::string SyncNode::getFullPath() const {

  std::vector<std::string> parts;
  auto current = shared_from_this();
  while (current) {
    if (!current->name.empty())
      parts.push_back(current->name);
    current = current->parent.lock();
  }

  std::reverse(parts.begin(), parts.end());
  std::string path = "";
  for (const auto &part : parts)
    path += "/" + part;
  return path.empty() ? "/" : path;
}

SyncTree::SyncTree(const std::string &syncPath)
    : m_root(
          std::make_shared<SyncNode>("/", Utility::getInode(syncPath), true)) {}

SyncTree::~SyncTree() {}

std::vector<std::string> SyncTree::splitPath(const std::string &path) {
  std::vector<std::string> parts;
  fs::path p(path);
  for (const auto &part : p) {
    std::string s = part.generic_string();
    if (s != "/" && s != "\\" && !s.empty())
      parts.push_back(s);
  }

  return parts;
}

void SyncTree::printTree(std::shared_ptr<SyncNode> &node, int indent) {
  if (!node)
    return;
  for (int i = 0; i < indent; ++i)
    std::cout << " ";

  std::cout << (node->isDir ? "[DIR] " : "[FILE] ")
            << (node->name.empty() ? "/ inode->" + node->inode
                                   : node->name + " inode->" + node->inode)
            << std::endl;
  for (auto &child : node->children) {
    printTree(child.second, indent + 1);
  }
}

void SyncTree::insertPath(const std::string &path, const std::string &inode,
                          bool isDir) {
  std::lock_guard<std::recursive_mutex> lock(m_mtx);

  auto parts = splitPath(path);
  auto current = m_root;

  for (size_t i = 0; i < parts.size(); ++i) {
    auto &name = parts[i];
    if (current->children.find(name) == current->children.end()) {
      current->children[name] =
          std::make_shared<SyncNode>(name, "", true, current);
    }
    current = current->children[name];
  }
  std::string name = parts.back();
  //  auto newNode = std::make_shared<SyncNode>(name, inode, isDir, current);
  current->name = name;
  current->inode = inode;
  current->isDir = isDir;
  current->isDirty = true;
  //  m_inodeTreeMap[inode] = newNode;
}

std::shared_ptr<SyncNode> SyncTree::findByPath(const std::string &path) {
  std::lock_guard<std::recursive_mutex> lock(m_mtx);
  auto parts = splitPath(path);
  auto current = m_root;
  for (auto &part : parts) {
    auto it = current->children.find(part);
    if (it == current->children.end())
      return nullptr;
    current = it->second;
  }
  return current;
}

std::shared_ptr<SyncNode> SyncTree::findByInode(const std::string &inode) {
  std::lock_guard<std::recursive_mutex> lock(m_mtx);

  std::function<std::shared_ptr<SyncNode>(const std::shared_ptr<SyncNode> &)>
      dfs = [&](const std::shared_ptr<SyncNode> &node)
      -> std::shared_ptr<SyncNode> {
    if (!node)
      return nullptr;
    if (node->inode == inode)
      return node;
    for (auto &[key, child] : node->children) {
      auto result = dfs(child);
      if (result)
        return result;
    }
    return nullptr;
  };

  return dfs(m_root);
}

void SyncTree::renamePath(const std::string &newPath,
                          const std::string &oldPath) {
  // /users/sandeep/desktop/original
  // /users/sandeep/desktop/original_renamed
  std::lock_guard<std::recursive_mutex> lock(m_mtx);
  auto node = findByPath(oldPath);
  // origin shared_prt will be fetched
  auto parts = splitPath(newPath);
  // [users,sandeep,desktop,origin_renamed]
  if (node) {
    auto parent = node->parent.lock();
    if (parent) {
      parent->children.erase(node->name);
      std::string newname = parts.back();
      node->name = newname;
      node->parent = parent;
      node->isDirty = true;
      parent->children[newname] = node;
    }
  }
}

void SyncTree::movePath(const std::string &newPath,
                        const std::string &oldPath) {
  // newPath = C:/Users/Sandeep Kumar/Desktop/sync_folder/moved/src_folder
  // oldPath = C:/Users/Sandeep Kumar/Desktop/sync_folder/src_folder

  std::lock_guard<std::recursive_mutex> lock(m_mtx);

  // find the parent Node of the newPath ==> /moved
  // find the parent Node of the oldPath ==> /
  // erase the /src_folder node from the parent /
  // add /src_folder node to new parent /moved

  auto oldParts = splitPath(oldPath);
  auto newParts = splitPath(newPath);

  std::vector<std::string> newPartsDst(newParts.begin(), newParts.end() - 1);

  std::string newPathDst = "";

  std::for_each(newPartsDst.begin(), newPartsDst.end(),
                [&](const std::string &part) { newPathDst += "/" + part; });

  newPathDst = newPathDst.empty() ? "/" : newPathDst;

  auto newParentNode = findByPath(newPathDst);
  auto oldNode = findByPath(oldPath);

  if (oldNode) {
    auto oldParentNode = oldNode->parent.lock();
    if (oldParentNode && newParentNode) {
      std::string name = oldParts.back();
      auto it = oldParentNode->children.find(name);
      if (it != oldParentNode->children.end()) {
        oldParentNode->children.erase(name);
        oldNode->parent = newParentNode;
        oldNode->isDirty = true;
        newParentNode->children[name] = oldNode;
      }
    }
  }
}

void SyncTree::deletePath(const std::string &path) {
  std::lock_guard<std::recursive_mutex> lock(m_mtx);
  auto parts = splitPath(path);
  auto node = findByPath(path);
  if (node) {
    auto parent = node->parent.lock();
    if (parent) {
      parent->children.erase(node->name);
    }
  }
}

void SyncTree::print() {
  std::lock_guard<std::recursive_mutex> lock(m_mtx);
  printTree(m_root, 0);
}

} // namespace sync_app
