// SPDX-License-Identifier: GPL-2.0-only
#include "libusb_transport_internal.h"

#include <new>

namespace px4::userland {

Result<GroupingResult> RuntimeTestAccess::enumerate_native(
    std::unique_ptr<LibusbApi> api) noexcept
{
    if (!api) {
        return Result<GroupingResult>::failure(Error::INVALID_ARGUMENT);
    }
    auto session = LibusbSession::create(*api, false);
    if (!session) {
        return Result<GroupingResult>::failure(session.error());
    }
    DeviceDiscovery discovery;
    NativeEnumerator enumerator(*api, session.value()->context());
    const auto discovered = enumerator.discover(discovery);
    if (!discovered) {
        return Result<GroupingResult>::failure(discovered.error());
    }
    std::vector<DeviceObservation> observations;
    observations.reserve(discovery.candidates().size());
    for (const DeviceCandidate& candidate : discovery.candidates()) {
        observations.push_back(candidate.observation);
    }
    return group_q3u4_devices(observations);
}

Result<std::unique_ptr<Q3U4Runtime>> RuntimeTestAccess::open_native(
    std::unique_ptr<LibusbApi> api, std::string_view base_serial) noexcept
{
    auto impl = Q3U4Runtime::Impl::create(std::move(api), nullptr, false, base_serial, nullptr);
    if (!impl) {
        return Result<std::unique_ptr<Q3U4Runtime>>::failure(impl.error());
    }
    std::unique_ptr<Q3U4Runtime> runtime(
        new (std::nothrow) Q3U4Runtime(std::move(impl.value())));
    if (!runtime) {
        return Result<std::unique_ptr<Q3U4Runtime>>::failure(Error::INTERNAL);
    }
    return Result<std::unique_ptr<Q3U4Runtime>>::success(std::move(runtime));
}

Result<std::unique_ptr<Q3U4Runtime>> RuntimeTestAccess::open_fds(
    std::unique_ptr<LibusbApi> api, std::unique_ptr<FdSyscalls> syscalls,
    const std::vector<int>& fds, std::string_view base_serial) noexcept
{
    auto impl = Q3U4Runtime::Impl::create(std::move(api), std::move(syscalls), true,
                                          base_serial, &fds);
    if (!impl) {
        return Result<std::unique_ptr<Q3U4Runtime>>::failure(impl.error());
    }
    std::unique_ptr<Q3U4Runtime> runtime(
        new (std::nothrow) Q3U4Runtime(std::move(impl.value())));
    if (!runtime) {
        return Result<std::unique_ptr<Q3U4Runtime>>::failure(Error::INTERNAL);
    }
    return Result<std::unique_ptr<Q3U4Runtime>>::success(std::move(runtime));
}

}  // namespace px4::userland
