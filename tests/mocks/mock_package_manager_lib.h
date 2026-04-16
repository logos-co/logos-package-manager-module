#pragma once

// Test-side helpers that let tests register the structs the mocked
// PackageManagerLib should return. The mock implementation lives in
// mock_package_manager_lib.cpp and is wired in via the logos_test() CMake
// function (MOCK_C_SOURCES).
//
// Why a struct registry instead of `LogosCMockStore` returns?
//   - LogosCMockStore uses `returnsRaw` + memcpy which is only safe for
//     trivially-copyable types. Our struct returns carry std::string and
//     std::vector, so memcpy would be undefined behaviour.
//   - Recursive DependencyTreeNode would need a bespoke serialisation; a
//     file-static std::optional<DependencyTreeNode> sidesteps that.
//
// Registries reset automatically on each new LogosTestContext. The mock
// piggybacks on LogosCMockStore::reset() via a sentinel call so tests can
// be written exactly like the old JSON-returning pattern — construct a
// context, set the mocks, instantiate the impl.

#include <optional>
#include <vector>

#include "package_manager_lib.h"

void setMockInstalledPackages(std::vector<InstalledPackage> v);
void setMockInstalledModules(std::vector<InstalledPackage> v);
void setMockInstalledUiPlugins(std::vector<InstalledPackage> v);
void setMockDependencyTree(std::optional<DependencyTreeNode> tree);
void setMockDependentTree(std::optional<DependentTreeNode> tree);
