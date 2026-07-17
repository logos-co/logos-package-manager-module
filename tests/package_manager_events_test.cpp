// Qt-free test bodies for PackageManagerImpl's `logos_events:` methods.
//
// Production bodies are codegen-emitted in package_manager_events.cpp (Qt
// marshaling); the unit/integration tests don't link that. These forwarders
// route each event's single string payload to logos_test's active capture
// (logos_test::EventCapture / ScopedEventSink — see <logos_test.h>). Linked
// into both test executables so package_manager_impl.cpp's emit calls resolve.

#include <logos_test.h>
#include "package_manager_impl.h"

using logos_test::recordEvent;

void PackageManagerImpl::corePluginFileInstalled(const std::string& path)    { recordEvent("corePluginFileInstalled", path); }
void PackageManagerImpl::uiPluginFileInstalled(const std::string& path)      { recordEvent("uiPluginFileInstalled", path); }
void PackageManagerImpl::corePluginUninstalled(const std::string& name)      { recordEvent("corePluginUninstalled", name); }
void PackageManagerImpl::uiPluginUninstalled(const std::string& name)        { recordEvent("uiPluginUninstalled", name); }
void PackageManagerImpl::beforeUninstall(const std::string& payload)         { recordEvent("beforeUninstall", payload); }
void PackageManagerImpl::beforeUpgrade(const std::string& payload)           { recordEvent("beforeUpgrade", payload); }
void PackageManagerImpl::beforeInstall(const std::string& payload)           { recordEvent("beforeInstall", payload); }
void PackageManagerImpl::beforeMultiUninstall(const std::string& payload)    { recordEvent("beforeMultiUninstall", payload); }
void PackageManagerImpl::uninstallCancelled(const std::string& payload)      { recordEvent("uninstallCancelled", payload); }
void PackageManagerImpl::upgradeCancelled(const std::string& payload)        { recordEvent("upgradeCancelled", payload); }
void PackageManagerImpl::installCancelled(const std::string& payload)        { recordEvent("installCancelled", payload); }
void PackageManagerImpl::multiUninstallCancelled(const std::string& payload) { recordEvent("multiUninstallCancelled", payload); }
void PackageManagerImpl::upgradeUninstallDone(const std::string& payload)    { recordEvent("upgradeUninstallDone", payload); }
void PackageManagerImpl::installApproved(const std::string& payload)         { recordEvent("installApproved", payload); }
