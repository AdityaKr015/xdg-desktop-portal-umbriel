#pragma once

#include "dbus/request.h"

#include <memory>

namespace sdbus {
class IObject;
}

namespace xdpu {

struct Config;

class SettingsPortal {
public:
  SettingsPortal(sdbus::IObject& object, const Config& config);
  ~SettingsPortal();

  SettingsPortal(const SettingsPortal&) = delete;
  SettingsPortal& operator=(const SettingsPortal&) = delete;
  SettingsPortal(SettingsPortal&&) = delete;
  SettingsPortal& operator=(SettingsPortal&&) = delete;

  void onConfigChanged(const Config& oldCfg, const Config& newCfg);

private:
  struct Impl;
  std::unique_ptr<Impl> m_impl;
};

} // namespace xdpu
