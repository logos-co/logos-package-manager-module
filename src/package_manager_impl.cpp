#include "package_manager_impl.h"
#include <package_manager_lib.h>
#include <lgx.h>
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <limits>

// ---------------------------------------------------------------------------
// Struct → LogosMap / LogosList conversion helpers
// ---------------------------------------------------------------------------
//
// Wire format is identical to what PackageManagerLib / the lgpm CLI emit via
// package_manager_json.cpp's nlohmann ADL hooks. We re-hand-roll here (rather
// than reuse those hooks) so the unit tests can compile against the stub
// header in tests/stubs/package_manager_lib.h without pulling in the lib's
// JSON module.
//
// Keep these in sync with package_manager_json.cpp's to_json definitions.
// ---------------------------------------------------------------------------

namespace {

LogosMap toLogosMap(const Hashes& h)
{
    LogosMap m = LogosMap::object();
    m["root"] = h.root;
    return m;
}

LogosMap toLogosMap(const InstalledPackage& p)
{
    LogosMap m = LogosMap::object();
    m["name"]         = p.name;
    m["version"]      = p.version;
    m["description"]  = p.description;
    m["type"]         = p.type;
    m["category"]     = p.category;
    m["author"]       = p.author;
    m["license"]      = p.license;
    m["icon"]         = p.icon;
    m["view"]         = p.view;

    LogosList deps = LogosList::array();
    for (const auto& d : p.dependencies) deps.push_back(d);
    m["dependencies"] = deps;

    m["hashes"]       = toLogosMap(p.hashes);
    m["installType"]  = std::string(installTypeToString(p.installType));
    m["installDir"]   = p.installDir;
    m["mainFilePath"] = p.mainFilePath;
    return m;
}

LogosList toLogosList(const std::vector<InstalledPackage>& v)
{
    LogosList out = LogosList::array();
    for (const auto& p : v) out.push_back(toLogosMap(p));
    return out;
}

// Flat per-node projection — just the node's own fields, no `children`.
// Shared between the flat list APIs (resolveFlatDependencies /
// resolveFlatDependents) and the tree APIs (where each recursive step is
// "this node's fields plus its children").
LogosMap toFlatLogosMap(const DependencyTreeNode& n)
{
    LogosMap m = LogosMap::object();
    m["name"]   = n.name;
    m["status"] = std::string(dependencyStatusToString(n.status));
    if (n.status == DependencyStatus::Installed) {
        m["version"]     = n.version;
        m["installType"] = std::string(installTypeToString(n.installType));
    } else {
        m["version"]     = "";
        m["installType"] = "";
    }
    return m;
}

LogosMap toFlatLogosMap(const DependentTreeNode& n)
{
    LogosMap m = LogosMap::object();
    m["name"]        = n.name;
    m["version"]     = n.version;
    m["type"]        = n.type;
    m["installType"] = std::string(installTypeToString(n.installType));
    m["installDir"]  = n.installDir;
    return m;
}

// Depth-clipped tree serialisation — root always emitted with its own
// fields; `maxDepth` bounds how far we recurse into `children`. Used by
// resolveDependencies/resolveDependents: maxDepth=1 for !recursive (root
// + direct children with empty children arrays), maxDepth=INT_MAX for
// the full tree.
template <typename Node>
LogosMap toLogosTreeMap(const Node& n, int maxDepth)
{
    LogosMap m = toFlatLogosMap(n);
    LogosList children = LogosList::array();
    if (maxDepth > 0) {
        for (const auto& c : n.children)
            children.push_back(toLogosTreeMap(c, maxDepth - 1));
    }
    m["children"] = children;
    return m;
}

template <typename Node>
LogosList toFlatLogosList(const std::vector<Node>& v)
{
    LogosList out = LogosList::array();
    for (const auto& n : v) out.push_back(toFlatLogosMap(n));
    return out;
}

} // namespace

PackageManagerImpl::PackageManagerImpl()
    : m_lib(nullptr)
{
    m_lib = new PackageManagerLib();
}

PackageManagerImpl::~PackageManagerImpl()
{
    // Signal any running worker thread to exit, then join it before
    // tearing down state it might still reference (m_pendingAction,
    // emitEvent). The lock is taken briefly to publish m_ackShutdown and
    // bump m_ackGeneration atomically; notify + join happen outside the
    // lock so the worker can re-acquire and exit its wait_for.
    {
        std::lock_guard<std::mutex> lk(m_stateMutex);
        m_ackShutdown = true;
        ++m_ackGeneration;
    }
    m_ackCv.notify_all();
    if (m_ackThread.joinable()) m_ackThread.join();

    delete m_lib;
    m_lib = nullptr;
}

LogosMap PackageManagerImpl::installPlugin(const std::string& pluginPath, bool skipIfNotNewerVersion)
{
    std::string errorMsg;
    std::string installedPluginPath;
    bool isCoreModule = false;
    std::string result = m_lib->installPluginFile(
        pluginPath, errorMsg, skipIfNotNewerVersion,
        &installedPluginPath, &isCoreModule
    );

    bool success = !result.empty();

    if (success && !installedPluginPath.empty() && emitEvent) {
        if (isCoreModule) {
            emitEvent("corePluginFileInstalled", installedPluginPath);
        } else {
            emitEvent("uiPluginFileInstalled", installedPluginPath);
        }
    }

    // Get signature info for the response
    auto sigResult = m_lib->verifyPackageSignature(pluginPath);

    std::string stem = std::filesystem::path(pluginPath).stem().string();

    LogosMap response;
    response["name"] = stem;
    response["path"] = success ? installedPluginPath : std::string();
    response["isCoreModule"] = isCoreModule;
    if (!success) {
        response["error"] = errorMsg;
    }

    // Add signature info
    if (sigResult.is_signed) {
        bool valid = sigResult.signature_valid && sigResult.package_valid;
        response["signatureStatus"] = valid ? std::string("signed") : std::string("invalid");
        response["signerDid"] = sigResult.signer_did;
        if (!sigResult.signer_name.empty())
            response["signerName"] = sigResult.signer_name;
        if (!sigResult.signer_url.empty())
            response["signerUrl"] = sigResult.signer_url;
        if (!sigResult.trusted_as.empty())
            response["trustedAs"] = sigResult.trusted_as;
    } else if (!sigResult.error.empty()) {
        response["signatureStatus"] = std::string("error");
        response["signatureError"] = sigResult.error;
    } else {
        response["signatureStatus"] = std::string("unsigned");
    }

    return response;
}

LogosMap PackageManagerImpl::inspectPackage(const std::string& lgxPath)
{
    LogosMap result;

    lgx_package_t pkg = lgx_load(lgxPath.c_str());
    if (!pkg) {
        result["error"] = std::string("Failed to load LGX package: ")
                        + (lgx_get_last_error() ? lgx_get_last_error() : "unknown");
        return result;
    }

    const char* rawName    = lgx_get_name(pkg);
    const char* rawVersion = lgx_get_version(pkg);
    const char* rawDesc    = lgx_get_description(pkg);
    const char* rawManifest = lgx_get_manifest_json(pkg);

    std::string pkgName    = rawName    ? rawName    : "";
    std::string pkgVersion = rawVersion ? rawVersion : "";

    result["name"]    = pkgName;
    result["version"] = pkgVersion;
    result["description"] = rawDesc ? std::string(rawDesc) : "";

    // Extract type, category, and root content hash from the embedded
    // manifest. The root hash (Merkle tree root over the package content,
    // `manifest.hashes.root`) is the same identifier PMU renders when
    // browsing the online catalog — surfacing it here lets the install
    // confirmation dialog show a stable per-release fingerprint.
    if (rawManifest) {
        try {
            auto doc = LogosMap::parse(rawManifest);
            result["type"]     = doc.value("type", "");
            result["category"] = doc.value("category", "");
            if (doc.contains("hashes") && doc["hashes"].is_object()) {
                result["rootHash"] = doc["hashes"].value("root", "");
            } else {
                result["rootHash"] = "";
            }
        } catch (...) {
            result["type"]     = "";
            result["category"] = "";
            result["rootHash"] = "";
        }
    }

    // Available platform variants.
    const char** variants = lgx_get_variants(pkg);
    LogosList variantList = LogosList::array();
    if (variants) {
        for (int i = 0; variants[i]; ++i)
            variantList.push_back(std::string(variants[i]));
        lgx_free_string_array(variants);
    }
    result["variants"] = variantList;

    lgx_free_package(pkg);

    // Signature verification — standalone, no install side effects.
    auto sig = m_lib->verifyPackageSignature(lgxPath);
    if (sig.is_signed) {
        bool valid = sig.signature_valid && sig.package_valid;
        result["signatureStatus"] = valid ? std::string("signed")
                                          : std::string("invalid");
        result["signerDid"]  = sig.signer_did;
        result["signerName"] = sig.signer_name;
    } else if (!sig.error.empty()) {
        result["signatureStatus"] = std::string("error");
    } else {
        result["signatureStatus"] = std::string("unsigned");
    }

    // Check if this package is already installed.
    bool isAlreadyInstalled = false;
    std::string installedVersion;
    std::string installedHash;
    std::vector<InstalledPackage> scan = m_lib->getInstalledPackages();
    for (const auto& entry : scan) {
        if (entry.name == pkgName) {
            isAlreadyInstalled = true;
            installedVersion = entry.version;
            // Passthrough from the installed manifest.json; same field PMU
            // reads in the online catalog (`manifest.hashes.root`).
            installedHash = entry.hashes.root;
            break;
        }
    }
    result["isAlreadyInstalled"] = isAlreadyInstalled;
    result["installedVersion"]   = installedVersion;
    result["installedHash"]      = installedHash;

    // If already installed, compute reverse dependents so the dialog can
    // show what would be affected by an upgrade.
    if (isAlreadyInstalled) {
        auto deps = installedDependentsNames(pkgName);
        LogosList depList = LogosList::array();
        for (const auto& d : deps) depList.push_back(d);
        result["installedDependents"] = depList;
    }

    return result;
}

LogosList PackageManagerImpl::getInstalledPackages()
{
    return toLogosList(m_lib->getInstalledPackages());
}

LogosList PackageManagerImpl::getInstalledModules()
{
    return toLogosList(m_lib->getInstalledModules());
}

LogosList PackageManagerImpl::getInstalledUiPlugins()
{
    return toLogosList(m_lib->getInstalledUiPlugins());
}

LogosMap PackageManagerImpl::uninstallPackage(const std::string& packageName)
{
    return doUninstall(packageName);
}

LogosMap PackageManagerImpl::doUninstall(const std::string& packageName)
{
    // Inspect the package before removal so we know whether to emit a core or UI event.
    std::vector<InstalledPackage> scan = m_lib->getInstalledPackages();
    std::string moduleType;
    for (const auto& entry : scan) {
        if (entry.name == packageName) {
            moduleType = entry.type;
            break;
        }
    }

    UninstallResult r = m_lib->uninstallPackage(packageName);

    LogosMap response;
    response["success"] = r.success;
    if (!r.success) {
        response["error"] = r.errorMsg;
    } else {
        LogosList removed = LogosList::array();
        for (const auto& f : r.removedFiles) removed.push_back(f);
        response["removedFiles"] = removed;

        if (emitEvent) {
            const std::string eventName =
                (moduleType == "core")
                    ? "corePluginUninstalled"
                    : "uiPluginUninstalled";
            emitEvent(eventName, packageName);
        }
    }
    return response;
}

LogosMap PackageManagerImpl::resolveDependencies(const std::string& packageName, bool recursive)
{
    // Unknown roots surface as nullopt from the library; keep an empty
    // object on the wire so callers can `.contains(...)` without branching.
    auto tree = m_lib->resolveDependencies(packageName);
    if (!tree) return LogosMap::object();
    // maxDepth=1 clips to root + direct children (children with empty
    // `children` arrays); INT_MAX walks the full tree.
    return toLogosTreeMap(*tree, recursive ? std::numeric_limits<int>::max() : 1);
}

LogosMap PackageManagerImpl::resolveDependents(const std::string& packageName, bool recursive)
{
    // Same shape treatment as resolveDependencies — the library returns a
    // tree, we either clip it at depth 1 or walk the full reverse subtree.
    auto tree = m_lib->resolveDependents(packageName);
    if (!tree) return LogosMap::object();
    return toLogosTreeMap(*tree, recursive ? std::numeric_limits<int>::max() : 1);
}

LogosList PackageManagerImpl::resolveFlatDependencies(const std::string& packageName, bool recursive)
{
    // Flat list of per-node maps (no `children`). recursive=false emits
    // only the root's direct children; recursive=true emits every
    // descendant, BFS-ordered and deduped by name (via DependencyTreeNode::flatten()).
    auto tree = m_lib->resolveDependencies(packageName);
    if (!tree) return LogosList::array();
    return recursive ? toFlatLogosList(tree->flatten())
                     : toFlatLogosList(tree->children);
}

LogosList PackageManagerImpl::resolveFlatDependents(const std::string& packageName, bool recursive)
{
    auto tree = m_lib->resolveDependents(packageName);
    if (!tree) return LogosList::array();
    return recursive ? toFlatLogosList(tree->flatten())
                     : toFlatLogosList(tree->children);
}

std::vector<std::string> PackageManagerImpl::getValidVariants()
{
    return PackageManagerLib::platformVariantsToTry();
}

void PackageManagerImpl::setEmbeddedModulesDirectory(const std::string& dir)
{
    m_lib->setEmbeddedModulesDirectory(dir);
}

void PackageManagerImpl::addEmbeddedModulesDirectory(const std::string& dir)
{
    m_lib->addEmbeddedModulesDirectory(dir);
}

void PackageManagerImpl::setEmbeddedUiPluginsDirectory(const std::string& dir)
{
    m_lib->setEmbeddedUiPluginsDirectory(dir);
}

void PackageManagerImpl::addEmbeddedUiPluginsDirectory(const std::string& dir)
{
    m_lib->addEmbeddedUiPluginsDirectory(dir);
}

void PackageManagerImpl::setUserModulesDirectory(const std::string& dir)
{
    m_lib->setUserModulesDirectory(dir);
}

void PackageManagerImpl::setUserUiPluginsDirectory(const std::string& dir)
{
    m_lib->setUserUiPluginsDirectory(dir);
}

void PackageManagerImpl::setSignaturePolicy(const std::string& policy)
{
    std::string p = policy;
    std::transform(p.begin(), p.end(), p.begin(), ::tolower);
    if (p == "none") m_lib->setSignaturePolicy(SignaturePolicy::NONE);
    else if (p == "warn") m_lib->setSignaturePolicy(SignaturePolicy::WARN);
    else if (p == "require") m_lib->setSignaturePolicy(SignaturePolicy::REQUIRE);
    else {
        std::cerr << "PackageManagerImpl::setSignaturePolicy: invalid policy '"
                  << policy << "' - expected one of: none, warn, require\n";
    }
}

void PackageManagerImpl::setKeyringDirectory(const std::string& dir)
{
    m_lib->setKeyringDirectory(dir);
}

LogosMap PackageManagerImpl::verifyPackage(const std::string& lgxPath)
{
    auto result = m_lib->verifyPackageSignature(lgxPath);

    LogosMap response;
    response["isSigned"] = result.is_signed;
    response["signatureValid"] = result.signature_valid;
    response["packageValid"] = result.package_valid;
    response["signerDid"] = result.signer_did;
    response["signerName"] = result.signer_name;
    response["signerUrl"] = result.signer_url;
    response["trustedAs"] = result.trusted_as;
    if (!result.error.empty())
        response["error"] = result.error;
    return response;
}

LogosMap PackageManagerImpl::addTrustedKey(const std::string& name, const std::string& did,
                                            const std::string& displayName, const std::string& url)
{
    std::string keyringDir = m_lib->keyringDirectory();
    const char* keyringDirPtr = keyringDir.empty() ? nullptr : keyringDir.c_str();

    lgx_result_t res = lgx_keyring_add(
        keyringDirPtr,
        name.c_str(),
        did.c_str(),
        displayName.empty() ? nullptr : displayName.c_str(),
        url.empty() ? nullptr : url.c_str()
    );

    LogosMap response;
    response["success"] = static_cast<bool>(res.success);
    if (!res.success && res.error)
        response["error"] = std::string(res.error);
    return response;
}

LogosMap PackageManagerImpl::removeTrustedKey(const std::string& name)
{
    std::string keyringDir = m_lib->keyringDirectory();
    const char* keyringDirPtr = keyringDir.empty() ? nullptr : keyringDir.c_str();

    lgx_result_t res = lgx_keyring_remove(
        keyringDirPtr,
        name.c_str()
    );

    LogosMap response;
    response["success"] = static_cast<bool>(res.success);
    if (!res.success && res.error)
        response["error"] = std::string(res.error);
    return response;
}

LogosList PackageManagerImpl::listTrustedKeys()
{
    std::string keyringDir = m_lib->keyringDirectory();
    const char* keyringDirPtr = keyringDir.empty() ? nullptr : keyringDir.c_str();

    lgx_keyring_list_t list = lgx_keyring_list(keyringDirPtr);

    LogosList result = LogosList::array();
    for (size_t i = 0; i < list.count; ++i) {
        LogosMap entry;
        if (list.keys[i].name)         entry["name"]        = std::string(list.keys[i].name);
        if (list.keys[i].did)          entry["did"]         = std::string(list.keys[i].did);
        if (list.keys[i].display_name) entry["displayName"] = std::string(list.keys[i].display_name);
        if (list.keys[i].url)          entry["url"]         = std::string(list.keys[i].url);
        if (list.keys[i].added_at)     entry["addedAt"]     = std::string(list.keys[i].added_at);
        result.push_back(entry);
    }

    lgx_free_keyring_list(list);
    return result;
}

// ---------------------------------------------------------------------------
// Gated uninstall / upgrade flow
// ---------------------------------------------------------------------------

const char* PackageManagerImpl::opName(PendingOp op)
{
    switch (op) {
        case PendingOp::Uninstall: return "uninstall";
        case PendingOp::Upgrade:   return "upgrade";
        case PendingOp::None:      return "none";
    }
    return "none";
}

bool PackageManagerImpl::isEmbedded(const std::string& packageName) const
{
    std::vector<InstalledPackage> scan = m_lib->getInstalledPackages();
    for (const auto& entry : scan) {
        if (entry.name == packageName)
            return entry.installType == InstallType::Embedded;
    }
    return false;
}

std::vector<std::string> PackageManagerImpl::installedDependentsNames(const std::string& packageName) const
{
    std::vector<std::string> names;
    auto tree = m_lib->resolveDependents(packageName);
    if (!tree) return names;
    auto flat = tree->flatten();
    names.reserve(flat.size());
    for (const auto& d : flat) {
        if (!d.name.empty()) names.push_back(d.name);
    }
    return names;
}

// ---------------------------------------------------------------------------
// Pure-C++ ack timer — std::thread + std::condition_variable replacing QTimer.
// See detailed protocol comment in the header.
// ---------------------------------------------------------------------------

void PackageManagerImpl::startAckTimerLocked(std::unique_lock<std::mutex>& lock)
{
    // Precondition: caller holds m_stateMutex via `lock`.

    // Bump the generation and wake any previously-running worker. If one is
    // still waiting on the CV, it'll re-acquire the mutex, see its captured
    // generation is stale, and bail.
    ++m_ackGeneration;
    m_ackCv.notify_all();

    // Join the previous worker (if any) before replacing m_ackThread —
    // assigning to a joinable std::thread is undefined behaviour. Release
    // the lock during join so the worker can proceed past its wait_for;
    // otherwise we'd deadlock (we hold the lock the worker needs).
    if (m_ackThread.joinable()) {
        lock.unlock();
        m_ackThread.join();
        lock.lock();
    }

    const uint64_t gen = m_ackGeneration;
    m_ackThread = std::thread([this, gen]() { ackTimerWorker(gen); });
}

void PackageManagerImpl::stopAckTimerLocked()
{
    // Caller holds m_stateMutex. Bump the generation and notify so any
    // running worker wakes up and exits silently. Do NOT join here: the
    // slot calling us is likely running on the module thread and the
    // worker might be mid-wait needing the mutex we hold. The worker
    // will exit on its own; the next startAckTimerLocked (or the
    // destructor) reaps the std::thread handle.
    ++m_ackGeneration;
    m_ackCv.notify_all();
}

void PackageManagerImpl::ackTimerWorker(uint64_t myGeneration)
{
    std::unique_lock<std::mutex> lock(m_stateMutex);

    // wait_for returns true when the predicate is satisfied, false on
    // timeout. Predicate: "stop waiting" — either the process is shutting
    // down or our generation is stale (a newer request / ack / cancel
    // has superseded us).
    bool cancelled = m_ackCv.wait_for(
        lock,
        std::chrono::milliseconds(m_ackTimeoutMs),
        [this, myGeneration]() {
            return m_ackShutdown || m_ackGeneration != myGeneration;
        }
    );
    if (cancelled) return;

    // Full timeout with no cancellation — but recheck state now that we
    // hold the lock. An ack or a different state change could have
    // landed between the last CV check and here (unlikely, but cheap to
    // verify).
    if (m_ackShutdown) return;
    if (m_ackGeneration != myGeneration) return;
    if (m_pendingAction.op == PendingOp::None || m_pendingAction.acked) return;

    // Claim the pending action so slot-side code sees a clean slate.
    PendingAction pa = m_pendingAction;
    m_pendingAction = {};

    const std::string reason = "no listener acknowledged within "
                             + std::to_string(m_ackTimeoutMs) + "ms";

    // Release the lock before emitting — emitEvent marshals through a
    // Qt signal; a listener synchronously calling back into this impl
    // (e.g. a headless runtime that calls uninstallPackage on cancel
    // notification) would otherwise re-enter the mutex and deadlock.
    lock.unlock();
    emitCancellation(pa, reason);
}

void PackageManagerImpl::emitCancellation(const PendingAction& pa, const std::string& reason)
{
    if (!emitEvent) return;

    LogosMap payload;
    payload["name"] = pa.name;
    payload["reason"] = reason;
    if (pa.op == PendingOp::Upgrade) {
        payload["releaseTag"] = pa.releaseTag;
        emitEvent("upgradeCancelled", payload.dump());
    } else if (pa.op == PendingOp::Uninstall) {
        emitEvent("uninstallCancelled", payload.dump());
    }
}

LogosMap PackageManagerImpl::requestUninstall(const std::string& packageName)
{
    LogosMap response;
    // Empty packageName would be persisted into m_pendingAction.name and then
    // broadcast via beforeUninstall(payload{name:""}), causing listeners to
    // open a dialog titled "Uninstall ''?" with no dependents. Reject early
    // with a distinct error so callers can surface a sane toast and callers
    // that ARE the GUI can avoid showing a stray dialog.
    if (packageName.empty()) {
        response["success"] = false;
        response["error"] = "Package name cannot be empty";
        return response;
    }

    std::unique_lock<std::mutex> lock(m_stateMutex);

    if (m_pendingAction.op != PendingOp::None) {
        response["success"] = false;
        response["error"] = std::string("Another ") + opName(m_pendingAction.op)
                          + " is in progress for '" + m_pendingAction.name + "'";
        return response;
    }

    if (isEmbedded(packageName)) {
        response["success"] = false;
        response["error"] = "Cannot uninstall embedded module '" + packageName + "'";
        return response;
    }

    m_pendingAction = {};
    m_pendingAction.op = PendingOp::Uninstall;
    m_pendingAction.name = packageName;
    m_pendingAction.acked = false;

    // Build the event payload while we still hold the lock (so m_lib reads
    // don't race against a concurrent slot). emitEvent itself is deferred
    // until after the unlock — see the reentrancy note in ackTimerWorker.
    LogosMap payload;
    payload["name"] = packageName;
    LogosList deps = LogosList::array();
    for (const auto& d : installedDependentsNames(packageName))
        deps.push_back(d);
    payload["installedDependents"] = deps;

    // Start the ack timer (this may briefly release + re-acquire `lock`
    // while joining a previous worker).
    startAckTimerLocked(lock);

    lock.unlock();
    if (emitEvent) emitEvent("beforeUninstall", payload.dump());

    response["success"] = true;
    return response;
}

LogosMap PackageManagerImpl::requestUpgrade(const std::string& packageName,
                                             const std::string& releaseTag,
                                             int64_t mode)
{
    LogosMap response;
    // Same rationale as requestUninstall: empty name has to be rejected
    // before we set pending state, otherwise beforeUpgrade(name="") leads
    // listeners into an empty-title dialog.
    if (packageName.empty()) {
        response["success"] = false;
        response["error"] = "Package name cannot be empty";
        return response;
    }

    std::unique_lock<std::mutex> lock(m_stateMutex);

    if (m_pendingAction.op != PendingOp::None) {
        response["success"] = false;
        response["error"] = std::string("Another ") + opName(m_pendingAction.op)
                          + " is in progress for '" + m_pendingAction.name + "'";
        return response;
    }

    if (isEmbedded(packageName)) {
        response["success"] = false;
        response["error"] = "Cannot upgrade embedded module '" + packageName + "'";
        return response;
    }

    m_pendingAction = {};
    m_pendingAction.op = PendingOp::Upgrade;
    m_pendingAction.name = packageName;
    m_pendingAction.releaseTag = releaseTag;
    m_pendingAction.mode = mode;
    m_pendingAction.acked = false;

    LogosMap payload;
    payload["name"] = packageName;
    payload["releaseTag"] = releaseTag;
    payload["mode"] = mode;
    LogosList deps = LogosList::array();
    for (const auto& d : installedDependentsNames(packageName))
        deps.push_back(d);
    payload["installedDependents"] = deps;

    startAckTimerLocked(lock);

    lock.unlock();
    if (emitEvent) emitEvent("beforeUpgrade", payload.dump());

    response["success"] = true;
    return response;
}

LogosMap PackageManagerImpl::ackPendingAction(const std::string& packageName)
{
    std::lock_guard<std::mutex> lock(m_stateMutex);
    LogosMap response;
    if (m_pendingAction.op == PendingOp::None || m_pendingAction.name != packageName) {
        response["success"] = false;
        response["error"] = "No matching pending action to ack for '" + packageName + "'";
        return response;
    }
    // Idempotent — re-acking an already-acked request is a no-op.
    m_pendingAction.acked = true;
    stopAckTimerLocked();
    response["success"] = true;
    return response;
}

LogosMap PackageManagerImpl::confirmUninstall(const std::string& packageName)
{
    {
        std::lock_guard<std::mutex> lock(m_stateMutex);
        if (m_pendingAction.op != PendingOp::Uninstall || m_pendingAction.name != packageName) {
            LogosMap response;
            response["success"] = false;
            response["error"] = "No matching pending uninstall for '" + packageName + "'";
            return response;
        }
        m_pendingAction = {};
        stopAckTimerLocked();
    }
    // Lock released before doUninstall — it emits corePluginUninstalled /
    // uiPluginUninstalled, and listeners may synchronously call back into
    // this impl (the whole point of the event is to trigger cleanup).
    return doUninstall(packageName);
}

LogosMap PackageManagerImpl::cancelUninstall(const std::string& packageName)
{
    PendingAction pa;
    {
        std::lock_guard<std::mutex> lock(m_stateMutex);
        if (m_pendingAction.op != PendingOp::Uninstall || m_pendingAction.name != packageName) {
            LogosMap response;
            response["success"] = false;
            response["error"] = "No matching pending uninstall for '" + packageName + "'";
            return response;
        }
        pa = m_pendingAction;
        m_pendingAction = {};
        stopAckTimerLocked();
    }
    // Uniform cancellation notification — same event the ack-timeout path emits.
    // Initiators (PMU) subscribe once and handle every cancellation consistently.
    emitCancellation(pa, "user cancelled");
    LogosMap response;
    response["success"] = true;
    return response;
}

LogosMap PackageManagerImpl::confirmUpgrade(const std::string& packageName,
                                             const std::string& releaseTag)
{
    int64_t mode = 0;
    {
        std::lock_guard<std::mutex> lock(m_stateMutex);
        if (m_pendingAction.op != PendingOp::Upgrade
            || m_pendingAction.name != packageName
            || m_pendingAction.releaseTag != releaseTag) {
            LogosMap response;
            response["success"] = false;
            response["error"] = "No matching pending upgrade for '" + packageName + "'";
            return response;
        }
        if (!m_pendingAction.acked) {
            LogosMap response;
            response["success"] = false;
            response["error"] = "Pending upgrade for '" + packageName + "' has not been acknowledged";
            return response;
        }
        mode = m_pendingAction.mode;
        m_pendingAction = {};
        stopAckTimerLocked();
    }

    LogosMap uninstallResult = doUninstall(packageName);

    // On successful uninstall, tell PMU to drive the download+install step
    // for the new version. The impl layer has no LogosAPI access (it only
    // communicates outward via the emitEvent callback), so we can't call
    // package_downloader directly. Instead we emit upgradeUninstallDone
    // with the pinned releaseTag — PMU subscribes to this event and reuses
    // its existing download+install chain (downloadPackageAsync →
    // installOnePackage). The user sees the row flip to "Installing" while
    // the download runs, then to "Installed" (or "Failed") when it finishes.
    bool ok = uninstallResult.value("success", false);
    if (ok && emitEvent) {
        LogosMap payload;
        payload["name"] = packageName;
        payload["releaseTag"] = releaseTag;
        payload["mode"] = mode;
        emitEvent("upgradeUninstallDone", payload.dump());
    }

    return uninstallResult;
}

LogosMap PackageManagerImpl::cancelUpgrade(const std::string& packageName,
                                            const std::string& releaseTag)
{
    PendingAction pa;
    {
        std::lock_guard<std::mutex> lock(m_stateMutex);
        if (m_pendingAction.op != PendingOp::Upgrade
            || m_pendingAction.name != packageName
            || m_pendingAction.releaseTag != releaseTag) {
            LogosMap response;
            response["success"] = false;
            response["error"] = "No matching pending upgrade for '" + packageName + "'";
            return response;
        }
        pa = m_pendingAction;
        m_pendingAction = {};
        stopAckTimerLocked();
    }
    emitCancellation(pa, "user cancelled");
    LogosMap response;
    response["success"] = true;
    return response;
}

LogosMap PackageManagerImpl::resetPendingAction()
{
    std::lock_guard<std::mutex> lock(m_stateMutex);
    m_pendingAction = {};
    stopAckTimerLocked();
    LogosMap response;
    response["success"] = true;
    return response;
}
