#include "dbus/settings.h"

#include "config/config.h"

#include <sdbus-c++/sdbus-c++.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdio>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace xdpu {

namespace {

constexpr char kInterface[] = "org.freedesktop.impl.portal.Settings";
constexpr char kAppearanceNamespace[] = "org.freedesktop.appearance";
constexpr char kNotFoundError[] = "org.freedesktop.portal.Error.NotFound";

template <typename T> using Dict = std::map<std::string, T>;
using SettingsMap = Dict<Dict<sdbus::Variant>>;
using Color = sdbus::Struct<double, double, double>;

std::string asciiLower(std::string value) {
  std::ranges::transform(value, value.begin(), [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
  return value;
}

uint32_t colorSchemeValue(std::string_view value) {
  const std::string lowered = asciiLower(std::string(value));
  if (lowered == "dark") {
    return 1;
  }
  if (lowered == "light") {
    return 2;
  }
  return 0;
}

std::optional<uint8_t> hexByte(char high, char low) {
  auto nibble = [](char c) -> std::optional<uint8_t> {
    if (c >= '0' && c <= '9') {
      return static_cast<uint8_t>(c - '0');
    }
    if (c >= 'a' && c <= 'f') {
      return static_cast<uint8_t>(10 + c - 'a');
    }
    if (c >= 'A' && c <= 'F') {
      return static_cast<uint8_t>(10 + c - 'A');
    }
    return std::nullopt;
  };

  const auto hi = nibble(high);
  const auto lo = nibble(low);
  if (!hi || !lo) {
    return std::nullopt;
  }
  return static_cast<uint8_t>((*hi << 4) | *lo);
}

std::optional<Color> accentColorValue(std::string_view value) {
  if (value.size() != 7 || value[0] != '#') {
    return std::nullopt;
  }

  const auto r = hexByte(value[1], value[2]);
  const auto g = hexByte(value[3], value[4]);
  const auto b = hexByte(value[5], value[6]);
  if (!r || !g || !b) {
    return std::nullopt;
  }

  constexpr double scale = 1.0 / 255.0;
  return Color{static_cast<double>(*r) * scale, static_cast<double>(*g) * scale, static_cast<double>(*b) * scale};
}

Dict<sdbus::Variant> appearanceSettings(const Config& config) {
  Dict<sdbus::Variant> values;
  values.emplace("color-scheme", sdbus::Variant{colorSchemeValue(config.settings.colorScheme)});
  if (const auto accent = accentColorValue(config.settings.accentColor)) {
    values.emplace("accent-color", sdbus::Variant{*accent});
  }
  values.emplace("contrast", sdbus::Variant{uint32_t{0}});
  return values;
}

bool namespaceFilterMatches(std::string_view filter, std::string_view name) {
  if (filter.empty()) {
    return true;
  }
  if (filter.size() >= 2 && filter.ends_with(".*")) {
    const std::string_view prefix = filter.substr(0, filter.size() - 1);
    return name.starts_with(prefix);
  }
  return filter == name;
}

bool namespaceListMatches(const std::vector<std::string>& namespaces, std::string_view name) {
  if (namespaces.empty()) {
    return true;
  }
  return std::ranges::any_of(namespaces, [&](const std::string& ns) { return namespaceFilterMatches(ns, name); });
}

std::optional<sdbus::Variant> readSetting(const Config& config, const std::string& namespaceName, const std::string& key) {
  if (namespaceName != kAppearanceNamespace) {
    return std::nullopt;
  }
  if (key == "color-scheme") {
    return sdbus::Variant{colorSchemeValue(config.settings.colorScheme)};
  }
  if (key == "accent-color") {
    if (const auto accent = accentColorValue(config.settings.accentColor)) {
      return sdbus::Variant{*accent};
    }
    return std::nullopt;
  }
  if (key == "contrast") {
    return sdbus::Variant{uint32_t{0}};
  }
  return std::nullopt;
}

bool variantSameSignatureAndValue(const sdbus::Variant& left, const sdbus::Variant& right) {
  if (std::string_view(left.peekValueType()) != std::string_view(right.peekValueType())) {
    return false;
  }
  if (left.containsValueOfType<uint32_t>()) {
    return left.get<uint32_t>() == right.get<uint32_t>();
  }
  if (left.containsValueOfType<Color>()) {
    return left.get<Color>() == right.get<Color>();
  }
  return false;
}

} // namespace

struct SettingsPortal::Impl {
  sdbus::IObject& object;
  Config config;

  Impl(sdbus::IObject& object, const Config& config) : object(object), config(config) {
    object
        .addVTable(sdbus::registerMethod("ReadAll")
                       .implementedAs([this](sdbus::Result<SettingsMap>&& result,
                                             const std::vector<std::string>& namespaces) {
                         SettingsMap values;
                         if (namespaceListMatches(namespaces, kAppearanceNamespace)) {
                           values.emplace(kAppearanceNamespace, appearanceSettings(this->config));
                         }
                         result.returnResults(values);
                       })
                       .withInputParamNames("namespaces")
                       .withOutputParamNames("value"),
                   sdbus::registerMethod("Read")
                       .implementedAs([this](sdbus::Result<sdbus::Variant>&& result, const std::string& namespaceName,
                                             const std::string& key) {
                         const auto value = readSetting(this->config, namespaceName, key);
                         if (!value) {
                           result.returnError(sdbus::Error{sdbus::Error::Name{kNotFoundError}, "Setting not found"});
                           return;
                         }
                         result.returnResults(*value);
                       })
                       .withInputParamNames("namespace", "key")
                       .withOutputParamNames("value"),
                   sdbus::registerSignal("SettingChanged")
                       .withParameters<std::string, std::string, sdbus::Variant>("namespace", "key", "value"),
                   sdbus::registerProperty("version").withGetter([]() { return uint32_t{2}; }))
        .forInterface(kInterface);
  }

  void emitChanged(const std::string& key, const sdbus::Variant& value) {
    try {
      object.emitSignal("SettingChanged").onInterface(kInterface).withArguments(std::string{kAppearanceNamespace}, key, value);
    } catch (const std::exception& error) {
      std::fprintf(stderr, "settings: failed to emit SettingChanged for %s: %s\n", key.c_str(), error.what());
    }
  }
};

SettingsPortal::SettingsPortal(sdbus::IObject& object, const Config& config)
    : m_impl(std::make_unique<Impl>(object, config)) {}

SettingsPortal::~SettingsPortal() = default;

void SettingsPortal::onConfigChanged(const Config& oldCfg, const Config& newCfg) {
  const auto oldColorScheme = readSetting(oldCfg, kAppearanceNamespace, "color-scheme");
  const auto newColorScheme = readSetting(newCfg, kAppearanceNamespace, "color-scheme");
  if (oldColorScheme && newColorScheme && !variantSameSignatureAndValue(*oldColorScheme, *newColorScheme)) {
    m_impl->emitChanged("color-scheme", *newColorScheme);
  }

  const auto oldAccent = readSetting(oldCfg, kAppearanceNamespace, "accent-color");
  const auto newAccent = readSetting(newCfg, kAppearanceNamespace, "accent-color");
  const bool accentChanged = static_cast<bool>(oldAccent) != static_cast<bool>(newAccent) ||
                             (oldAccent && newAccent && !variantSameSignatureAndValue(*oldAccent, *newAccent));
  if (accentChanged) {
    if (newAccent) {
      m_impl->emitChanged("accent-color", *newAccent);
    } else {
      m_impl->emitChanged("accent-color", sdbus::Variant{Color{-1.0, -1.0, -1.0}});
    }
  }

  m_impl->config = newCfg;
}

} // namespace xdpu
