#include "OpenKneeboard/Tab.h"

namespace OpenKneeboard {

class Tab::Impl final {
 public:
  std::string Title;
};

Tab::Tab(const wxString& title) : p(new Impl {.Title = title.ToStdString()}) {
}

Tab::~Tab() {
}

std::string Tab::GetTitle() const {
  return p->Title;
}

void Tab::Reload() {
}

void Tab::OnGameEvent(const GameEvent&) {
}

}// namespace OpenKneeboard
