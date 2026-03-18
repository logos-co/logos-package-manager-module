#pragma once

#include <QObject>
#include <QtPlugin>
#include "interface.h"
#include "logos_native_provider.h"
#include "logos_native_adapter.h"
#include "package_manager_impl.h"

class PackageManagerLoader : public QObject, public PluginInterface, public NativeProviderPlugin
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID NativeProviderPlugin_iid FILE "metadata.json")
    Q_INTERFACES(PluginInterface NativeProviderPlugin)

public:
    QString name() const override { return "package_manager"; }
    QString version() const override { return "1.0.0"; }
    NativeProviderObject* createNativeProviderObject() override { return new PackageManagerImpl(); }
};
