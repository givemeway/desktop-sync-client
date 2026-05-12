#include <string>
namespace sync_app {

class Authentication {

public:
  Authentication();
  ~Authentication();
  std::string login(const std::string &username, const std::string &password);
};

} // namespace sync_app
