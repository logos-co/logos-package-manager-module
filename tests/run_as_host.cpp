// The impl answers configuration only to the runtime or core_service; these
// tests call it directly, standing in for the runtime on the main thread.
#include <logos_caller.h>

namespace {
const bool kRunAsHost = [] {
    logos::detail::setCallCaller(R"({"kind":"host"})");
    return true;
}();
} // namespace
