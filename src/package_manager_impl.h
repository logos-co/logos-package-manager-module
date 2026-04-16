#pragma once

#include <string>
#include <vector>
#include <functional>
#include <mutex>
#include <condition_variable>
#include <thread>
#include <cstdint>
#include <logos_json.h>

class PackageManagerLib;

class PackageManagerImpl {
public:
    PackageManagerImpl();
    ~PackageManagerImpl();

    PackageManagerImpl(const PackageManagerImpl&) = delete;
    PackageManagerImpl& operator=(const PackageManagerImpl&) = delete;

    // Event callback — wired automatically by the generated glue layer.
    // Call this to emit named events to other modules / the host application.
    std::function<void(const std::string& eventName, const std::string& data)> emitEvent;

    // Install from local LGX file — returns LogosMap {name, path, error, isCoreModule, ...}
    LogosMap installPlugin(const std::string& pluginPath, bool skipIfNotNewerVersion);

    // Inspect an LGX file without installing. Returns package metadata plus
    // already-installed status and dependents so callers can show a confirmation
    // dialog before committing. Shape:
    //   { name, version, type, description, category,
    //     signatureStatus ("signed"|"unsigned"|"invalid"|"error"),
    //     signerDid?, signerName?,
    //     isAlreadyInstalled, installedVersion?,
    //     installedDependents? }
    LogosMap inspectPackage(const std::string& lgxPath);

    // Directory configuration — embedded (multiple, read-only)
    void setEmbeddedModulesDirectory(const std::string& dir);
    void addEmbeddedModulesDirectory(const std::string& dir);
    void setEmbeddedUiPluginsDirectory(const std::string& dir);
    void addEmbeddedUiPluginsDirectory(const std::string& dir);

    // Directory configuration — user (single, writable)
    void setUserModulesDirectory(const std::string& dir);
    void setUserUiPluginsDirectory(const std::string& dir);

    // Scanning — each returns LogosList (JSON array with all manifest fields
    // + installDir + mainFilePath + installType ("embedded"|"user"))
    LogosList getInstalledPackages();
    LogosList getInstalledModules();
    LogosList getInstalledUiPlugins();

    // Uninstall a user-installed package. Refuses embedded packages.
    // Returns { success: bool, error?: string, removedFiles?: [string] }.
    // On success also emits "corePluginUninstalled" or "uiPluginUninstalled".
    //
    // This is the ungated path — it performs the uninstall immediately.
    // Headless callers (lgpm, scripts, logoscore-driven automation) use this
    // directly. GUI callers should prefer requestUninstall below, which gates
    // the destructive work behind a listener-driven confirmation dialog.
    LogosMap uninstallPackage(const std::string& packageName);

    // Forward dependency walk for an installed package. Returns a tree of
    // { name, status, version, installType, children: [...] } rooted at the
    // queried package. Stops at NotInstalled/Cycle nodes. `recursive=false`
    // walks only one level deep (children have empty `children` arrays);
    // `recursive=true` walks the full tree.
    LogosMap resolveDependencies(const std::string& packageName, bool recursive);

    // Reverse dependency walk — same tree shape as resolveDependencies but
    // for the inverse edge. Returns a tree of { name, version, type,
    // installType, installDir, children: [...] } rooted at the queried
    // package. `recursive=false` walks only depth-1 (direct reverse
    // neighbours, with empty `children`); `recursive=true` walks the full
    // reverse subtree.
    LogosMap resolveDependents(const std::string& packageName, bool recursive);

    // Flat projections of the two walks above. Each returns a LogosList of
    // per-node maps (same fields as the tree version minus `children`).
    // `recursive=false` emits only direct neighbours; `recursive=true`
    // emits every descendant, BFS-ordered and deduplicated by name.
    LogosList resolveFlatDependencies(const std::string& packageName, bool recursive);
    LogosList resolveFlatDependents(const std::string& packageName, bool recursive);

    // Platform variants this build accepts (e.g. ["darwin-arm64-dev"] or ["darwin-arm64"])
    std::vector<std::string> getValidVariants();

    // Signature policy configuration
    void setSignaturePolicy(const std::string& policy);
    void setKeyringDirectory(const std::string& dir);

    // Standalone signature verification — returns {isSigned, signatureValid, packageValid, signerDid, ...}
    LogosMap verifyPackage(const std::string& lgxPath);

    // Keyring management — add/remove/list trusted signing keys
    LogosMap addTrustedKey(const std::string& name, const std::string& did,
                           const std::string& displayName, const std::string& url);
    LogosMap removeTrustedKey(const std::string& name);
    LogosList listTrustedKeys();

    // ----------------------------------------------------------------
    // Gated uninstall / upgrade flow with two-phase listener ack.
    // ----------------------------------------------------------------
    //
    // Protocol (GUI-only — headless callers must use the ungated slots above):
    //
    //   1. Caller invokes requestUninstall(name) / requestUpgrade(name, tag, mode).
    //      Returns LogosResult-shaped LogosMap { success, error? } synchronously.
    //      On success, sets pending state, emits "beforeUninstall" / "beforeUpgrade",
    //      and starts a 3s ack-reception timer.
    //
    //   2. Any listener handling the event MUST immediately call
    //      ackPendingAction(name). This cancels the ack timer and tells the module
    //      "I'm driving the dialog — wait indefinitely for my decision."
    //
    //   3a. If the ack timer fires with no ack, the module clears pending state
    //       and emits "uninstallCancelled" / "upgradeCancelled" with a timeout
    //       reason. Destructive work never runs without an owning listener.
    //
    //   3b. Once acked, confirmUninstall / confirmUpgrade performs the work;
    //       cancelUninstall / cancelUpgrade aborts and emits the cancellation
    //       event with reason "user cancelled".
    //
    // Only one gated flow can be pending globally (across packages and ops).
    // A second requestXxx while one is pending returns { success: false, error: ... }.
    LogosMap requestUninstall(const std::string& packageName);
    LogosMap requestUpgrade(const std::string& packageName, const std::string& releaseTag, int64_t mode);

    LogosMap ackPendingAction(const std::string& packageName);

    LogosMap confirmUninstall(const std::string& packageName);
    LogosMap cancelUninstall(const std::string& packageName);
    LogosMap confirmUpgrade(const std::string& packageName, const std::string& releaseTag);
    LogosMap cancelUpgrade(const std::string& packageName, const std::string& releaseTag);

    // Belt-and-braces: clears any pending state. Called by Basecamp at startup
    // so a crash mid-dialog in a previous session doesn't block new requests.
    LogosMap resetPendingAction();

    // Test-only hook — override the ack-reception timeout so timeout-path
    // tests complete in milliseconds instead of the production 3-second
    // default. Must be called before any request*() on this instance (i.e.
    // while no ack-timer worker is running). Not thread-safe against a
    // concurrent worker.
    void setAckTimeoutMsForTest(int ms) { m_ackTimeoutMs = ms; }

private:
    enum class PendingOp { None, Uninstall, Upgrade };

    struct PendingAction {
        PendingOp   op = PendingOp::None;
        std::string name;
        std::string releaseTag;   // upgrade only
        int64_t     mode = 0;     // upgrade only (UpgradeMode enum as int)
        bool        acked = false;
    };

    // Production ack-reception timeout. Overridable per-instance via
    // setAckTimeoutMsForTest so timeout-path tests don't need to wait
    // 3 real seconds each.
    int m_ackTimeoutMs = 3000;
    static const char* opName(PendingOp op);

    // ----------------------------------------------------------------
    // Pure-C++ ack timer.
    // ----------------------------------------------------------------
    //
    // When a gated operation (requestUninstall / requestUpgrade) is initiated,
    // a worker thread is spawned that waits on m_ackCv for up to m_ackTimeoutMs.
    // Any concurrent state change — ackPendingAction, confirmXxx, cancelXxx,
    // resetPendingAction, a new requestXxx, or the destructor — bumps
    // m_ackGeneration and notifies the CV. The worker wakes up, observes that
    // its captured generation no longer matches the current one (or m_ackShutdown
    // is set), and exits silently. Only a worker whose captured generation still
    // matches after a full timeout proceeds to emit the cancellation event.
    //
    // Threading rules:
    //   - startAckTimerLocked / stopAckTimerLocked must be called with
    //     m_stateMutex held.
    //   - startAckTimerLocked temporarily releases the lock while joining the
    //     previous worker thread (if any), to avoid a deadlock where the
    //     worker can't exit its wait_for because we're holding the lock.
    //   - stopAckTimerLocked never joins — it only signals. The worker exits
    //     on its own; the next startAckTimerLocked (or the destructor) joins
    //     the now-finished thread.
    //   - The destructor sets m_ackShutdown, notifies the CV, and joins the
    //     worker before destroying any other state.
    //
    // Everything else on this class (m_lib access, file scanning, emitEvent
    // from user code) runs serially on the module thread via the glue layer's
    // queued connection, so no additional locking is needed beyond
    // m_stateMutex guarding the pending-action state.
    void startAckTimerLocked(std::unique_lock<std::mutex>& lock);
    void stopAckTimerLocked();
    void ackTimerWorker(uint64_t myGeneration);

    // Helpers used by the gated-flow slots.
    bool isEmbedded(const std::string& packageName) const;
    std::vector<std::string> installedDependentsNames(const std::string& packageName) const;
    LogosMap doUninstall(const std::string& packageName);
    void emitCancellation(const PendingAction& pa, const std::string& reason);

    PackageManagerLib* m_lib;

    // Guards m_pendingAction and the ack-timer generation/shutdown flags.
    mutable std::mutex      m_stateMutex;
    std::condition_variable m_ackCv;
    std::thread             m_ackThread;
    uint64_t                m_ackGeneration = 0;
    bool                    m_ackShutdown   = false;

    PendingAction m_pendingAction;
};
