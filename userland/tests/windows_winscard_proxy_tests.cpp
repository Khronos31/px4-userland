// SPDX-License-Identifier: GPL-2.0-only
// Production ABI/ownership logic with in-process fake providers. Real CARD_*
// framing, discovery and security are tested by windows_winscard_client_tests.
#define WINSCARDDATA
#include "windows/winscard_native.h"
#include "windows/winscard_card_client.h"

#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <thread>

namespace {
using namespace px4::userland;
using namespace px4::userland::windows_card;
std::atomic<int> failures{0};
#define CHECK(x) do { if (!(x)) { std::fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #x); ++failures; } } while (false)
constexpr SCARDCONTEXT native_context = 0x88776655U;
constexpr SCARDHANDLE native_card = 0x99887766U;
int establishes = 0, releases = 0, connects = 0, disconnects = 0, transmits = 0;
int local_transmits = 0, local_disconnects = 0, local_closes = 0;
LONG disconnect_result = SCARD_S_SUCCESS;
bool collide = false, native_missing = false, local_missing = false;
bool malformed_list = false;
Error local_error = Error::OK;
Error local_status_error = Error::OK;
bool local_present = true, local_initialized = true;
std::size_t local_atr_size = 3U;
std::mutex barrier_mutex;
std::condition_variable barrier;
bool block_transmit = false, entered_transmit = false, finish_transmit = false;
std::atomic<bool> transmit_active{false};

LONG WINAPI establish(DWORD scope, LPCVOID a, LPCVOID b, LPSCARDCONTEXT output)
{
    CHECK(scope == SCARD_SCOPE_USER || scope == SCARD_SCOPE_SYSTEM);
    CHECK(!a && !b);
    ++establishes;
    if (native_missing) { *output = 0xdeadU; return SCARD_E_NO_SERVICE; }
    *output = native_context;
    return SCARD_S_SUCCESS;
}
LONG WINAPI release(SCARDCONTEXT context)
{
    CHECK(context == native_context);
    CHECK(!transmit_active.load());
    ++releases;
    return SCARD_S_SUCCESS;
}
template <typename C> LONG reader_list(SCARDCONTEXT context, const C* groups, C* output, LPDWORD length)
{
    CHECK(context == native_context && !groups);
    const char* text = collide ? "PX4 userland (mock)" : "External reader";
    const DWORD required = static_cast<DWORD>(std::strlen(text) + 2U);
    const DWORD capacity = *length;
    *length = required;
    if (!output) return SCARD_S_SUCCESS;
    if (capacity < required) return SCARD_E_INSUFFICIENT_BUFFER;
    for (DWORD i = 0; i < required - 1U; ++i) output[i] = static_cast<C>(text[i]);
    output[required - 1U] = 0;
    if (malformed_list) output[required - 1U] = static_cast<C>('!');
    return SCARD_S_SUCCESS;
}
LONG WINAPI list_a(SCARDCONTEXT c, LPCSTR g, LPSTR o, LPDWORD n) { return reader_list(c,g,o,n); }
LONG WINAPI list_w(SCARDCONTEXT c, LPCWSTR g, LPWSTR o, LPDWORD n) { return reader_list(c,g,o,n); }
template <typename C> LONG connect_native(SCARDCONTEXT context, const C* name, DWORD share,
                                         DWORD protocol, LPSCARDHANDLE output, LPDWORD active)
{
    CHECK(context == native_context && name && share == SCARD_SHARE_SHARED && protocol == SCARD_PROTOCOL_T1);
    ++connects;
    *output = native_card;
    *active = SCARD_PROTOCOL_T1;
    return SCARD_S_SUCCESS;
}
LONG WINAPI connect_a(SCARDCONTEXT c,LPCSTR n,DWORD s,DWORD p,LPSCARDHANDLE h,LPDWORD a) { return connect_native(c,n,s,p,h,a); }
LONG WINAPI connect_w(SCARDCONTEXT c,LPCWSTR n,DWORD s,DWORD p,LPSCARDHANDLE h,LPDWORD a) { return connect_native(c,n,s,p,h,a); }
LONG WINAPI disconnect(SCARDHANDLE card, DWORD disposition)
{
    CHECK(card == native_card && disposition <= SCARD_EJECT_CARD);
    ++disconnects;
    return disconnect_result;
}
LONG WINAPI transmit(SCARDHANDLE card,LPCSCARD_IO_REQUEST pci,LPCBYTE data,DWORD size,
                     LPSCARD_IO_REQUEST receive_pci,LPBYTE output,LPDWORD length)
{
    CHECK(card == native_card && pci == SCARD_PCI_T1 && data && size == 1U);
    CHECK(!receive_pci && output && length && *length >= 2U);
    ++transmits;
    transmit_active.store(true);
    std::unique_lock<std::mutex> lock(barrier_mutex);
    if (block_transmit) {
        entered_transmit = true;
        barrier.notify_all();
        barrier.wait(lock, [] { return finish_transmit; });
    }
    output[0] = 0x90U; output[1] = 0U; *length = 2U;
    transmit_active.store(false);
    return SCARD_S_SUCCESS;
}
LONG WINAPI status_w(SCARDHANDLE card,LPWSTR names,LPDWORD name_length,LPDWORD state,
                     LPDWORD protocol,LPBYTE atr,LPDWORD atr_length)
{
    CHECK(card == native_card);
    const LONG names_result = reader_list(native_context,static_cast<LPCWSTR>(nullptr),names,name_length);
    *state = SCARD_SPECIFIC; *protocol = SCARD_PROTOCOL_T1;
    CHECK(atr && *atr_length >= 3U);
    atr[0] = 0x3bU; atr[1] = 0x01U; atr[2] = 0x02U; *atr_length = 3U;
    return names_result;
}
LONG WINAPI change_w(SCARDCONTEXT context,DWORD timeout,LPSCARD_READERSTATEW states,DWORD count)
{
    CHECK(context == native_context && timeout == 0U && count == 1U && states);
    CHECK(states[0].dwCurrentState == SCARD_STATE_UNAWARE);
    states[0].dwEventState = SCARD_STATE_PRESENT | SCARD_STATE_CHANGED;
    states[0].cbAtr = 3U; states[0].rgbAtr[0] = 0x3bU;
    return SCARD_S_SUCCESS;
}
SCARDCONTEXT make_context()
{
    SCARDCONTEXT context = 0U;
    CHECK(SCardEstablishContext(SCARD_SCOPE_USER,nullptr,nullptr,&context) == SCARD_S_SUCCESS);
    CHECK(context && context != native_context);
    return context;
}
SCARDHANDLE make_card(SCARDCONTEXT context, const char* name)
{
    SCARDHANDLE card = 0U; DWORD protocol = 0U;
    CHECK(SCardConnectA(context,name,SCARD_SHARE_SHARED,SCARD_PROTOCOL_T1,&card,&protocol) == SCARD_S_SUCCESS);
    CHECK(card && card != native_card && protocol == SCARD_PROTOCOL_T1);
    return card;
}
void test_lists()
{
    const auto context = make_context();
    DWORD size = SCARD_AUTOALLOCATE;
    CHECK(SCardListReadersA(context,nullptr,nullptr,&size) == SCARD_S_SUCCESS);
    const char expected[] = "PX4 userland (mock)\0External reader\0";
    CHECK(size == sizeof(expected));
    std::array<char,100U> names{}; names.fill('!');
    DWORD short_size = size - 1U;
    CHECK(SCardListReadersA(context,nullptr,names.data(),&short_size) == SCARD_E_INSUFFICIENT_BUFFER);
    CHECK(short_size == size && names[0] == '!');
    CHECK(SCardListReadersA(context,nullptr,names.data(),&size) == SCARD_S_SUCCESS);
    CHECK(std::memcmp(names.data(),expected,sizeof(expected)) == 0);
    DWORD wide_size = 0U;
    CHECK(SCardListReadersW(context,nullptr,nullptr,&wide_size) == SCARD_S_SUCCESS && wide_size == size);
    std::array<wchar_t,100U> wide{};
    CHECK(SCardListReadersW(context,nullptr,wide.data(),&wide_size) == SCARD_S_SUCCESS);
    for (DWORD i=0; i<size; ++i) CHECK(wide[i] == static_cast<wchar_t>(expected[i]));
    size = SCARD_AUTOALLOCATE;
    LPSTR allocated = nullptr;
    CHECK(SCardListReadersA(context,nullptr,reinterpret_cast<LPSTR>(&allocated),&size) == SCARD_S_SUCCESS);
    CHECK(allocated && std::memcmp(allocated,expected,sizeof(expected)) == 0);
    CHECK(SCardFreeMemory(context,allocated) == SCARD_S_SUCCESS);
    CHECK(SCardFreeMemory(context,allocated) == SCARD_E_INVALID_PARAMETER);
    CHECK(SCardListReadersA(context,"group",nullptr,&size) == SCARD_E_UNSUPPORTED_FEATURE);
    CHECK(SCardListReadersA(context,nullptr,nullptr,nullptr) == SCARD_E_INVALID_PARAMETER);
    malformed_list = true; size = 0U;
    CHECK(SCardListReadersA(context,nullptr,nullptr,&size) == SCARD_F_COMM_ERROR);
    malformed_list = false;
    CHECK(SCardReleaseContext(context) == SCARD_S_SUCCESS);
    CHECK(SCardReleaseContext(context) == SCARD_E_INVALID_HANDLE);
    CHECK(SCardListReadersA(context,nullptr,nullptr,&size) == SCARD_E_INVALID_HANDLE);
}
void test_local()
{
    const auto context = make_context();
    SCARDHANDLE card = 77U; DWORD active = 88U;
    CHECK(SCardConnectA(context,"PX4 userland (mock)",SCARD_SHARE_DIRECT,SCARD_PROTOCOL_T1,&card,&active) == SCARD_E_UNSUPPORTED_FEATURE);
    CHECK(card == 0U && active == 0U);
    CHECK(SCardConnectA(context,"PX4 userland (mock)",SCARD_SHARE_SHARED,SCARD_PROTOCOL_T0,&card,&active) == SCARD_E_PROTO_MISMATCH);
    CHECK(SCardConnectA(context,"PX4 userland (missing)",SCARD_SHARE_SHARED,SCARD_PROTOCOL_T1,&card,&active) == SCARD_E_UNKNOWN_READER);
    CHECK(SCardConnectW(context,L"PX4 userland (mock)",SCARD_SHARE_SHARED,SCARD_PROTOCOL_T1,&card,&active) == SCARD_S_SUCCESS);
    const BYTE apdu = 0U; std::array<BYTE,8U> response{};
    DWORD size = SCARD_AUTOALLOCATE;
    const int before = local_transmits;
    CHECK(SCardTransmit(card,SCARD_PCI_T1,&apdu,1U,nullptr,response.data(),&size) == SCARD_E_INVALID_PARAMETER);
    CHECK(local_transmits == before);
    size = 1U;
    CHECK(SCardTransmit(card,SCARD_PCI_T1,&apdu,1U,nullptr,response.data(),&size) == SCARD_E_INSUFFICIENT_BUFFER);
    CHECK(local_transmits == before + 1 && size == 2U && response[0] == 0U);
    size = 8U; SCARD_IO_REQUEST receive_pci{0U,8U};
    CHECK(SCardTransmit(card,SCARD_PCI_T1,&apdu,1U,&receive_pci,response.data(),&size) == SCARD_S_SUCCESS);
    CHECK(local_transmits == before + 2 && size == 2U && response[0] == 0x90U);
    CHECK(receive_pci.dwProtocol == 2U && receive_pci.cbPciLength == 8U);
    CHECK(SCardDisconnect(card,SCARD_UNPOWER_CARD) == SCARD_E_UNSUPPORTED_FEATURE);
    CHECK(SCardDisconnect(card,99U) == SCARD_E_INVALID_VALUE);
    CHECK(SCardDisconnect(card,SCARD_RESET_CARD) == SCARD_S_SUCCESS);
    CHECK(SCardDisconnect(card,SCARD_LEAVE_CARD) == SCARD_E_INVALID_HANDLE);
    CHECK(SCardTransmit(card,SCARD_PCI_T1,&apdu,1U,nullptr,response.data(),&size) == SCARD_E_INVALID_HANDLE);
    const auto abandoned = make_card(context,"PX4 userland (mock)");
    const int disconnect_before = local_disconnects, closes_before = local_closes;
    CHECK(SCardReleaseContext(context) == SCARD_S_SUCCESS);
    CHECK(local_disconnects == disconnect_before && local_closes == closes_before + 1);
    CHECK(SCardDisconnect(abandoned,SCARD_LEAVE_CARD) == SCARD_E_INVALID_HANDLE);
}
void test_native_and_collision()
{
    const auto context = make_context();
    auto card = make_card(context,"External reader");
    const int before = disconnects;
    CHECK(SCardDisconnect(card,99U) == SCARD_E_INVALID_VALUE && disconnects == before);
    disconnect_result = SCARD_E_NOT_READY;
    CHECK(SCardDisconnect(card,SCARD_RESET_CARD) == SCARD_E_NOT_READY);
    disconnect_result = SCARD_S_SUCCESS;
    BYTE input = 0U; BYTE output[2]{}; DWORD size = 2U;
    CHECK(SCardTransmit(card,SCARD_PCI_T1,&input,1U,nullptr,output,&size) == SCARD_S_SUCCESS);
    CHECK(SCardDisconnect(card,SCARD_LEAVE_CARD) == SCARD_S_SUCCESS);
    CHECK(SCardDisconnect(native_card,SCARD_LEAVE_CARD) == SCARD_E_INVALID_HANDLE);
    collide = true;
    const int native_before = connects;
    card = make_card(context,"PX4 userland (mock)");
    CHECK(connects == native_before + 1);
    DWORD count = 0U;
    CHECK(SCardListReadersA(context,nullptr,nullptr,&count) == SCARD_S_SUCCESS);
    CHECK(count == std::strlen("PX4 userland (mock)") + 2U);
    CHECK(SCardDisconnect(card,SCARD_LEAVE_CARD) == SCARD_S_SUCCESS);
    collide = false;
    CHECK(SCardReleaseContext(context) == SCARD_S_SUCCESS);
}
void test_release_race()
{
    const auto context = make_context();
    const auto card = make_card(context,"External reader");
    block_transmit = true; entered_transmit = false; finish_transmit = false;
    std::atomic<LONG> transmitted{SCARD_F_INTERNAL_ERROR}, released{SCARD_F_INTERNAL_ERROR};
    std::atomic<bool> release_started{false};
    std::thread sender([&] {
        BYTE input = 0U, output[2]{}; DWORD size = 2U;
        transmitted.store(SCardTransmit(card,SCARD_PCI_T1,&input,1U,nullptr,output,&size));
    });
    {
        std::unique_lock<std::mutex> lock(barrier_mutex);
        CHECK(barrier.wait_for(lock,std::chrono::seconds(5),[] { return entered_transmit; }));
    }
    std::thread closer([&] { release_started.store(true); released.store(SCardReleaseContext(context)); });
    while (!release_started.load()) std::this_thread::yield();
    {
        std::lock_guard<std::mutex> lock(barrier_mutex);
        CHECK(released.load() == SCARD_F_INTERNAL_ERROR);
        finish_transmit = true;
    }
    barrier.notify_all(); sender.join(); closer.join(); block_transmit = false;
    CHECK(transmitted.load() == SCARD_S_SUCCESS && released.load() == SCARD_S_SUCCESS);
    CHECK(SCardDisconnect(card,SCARD_LEAVE_CARD) == SCARD_E_INVALID_HANDLE);
}
void test_provider_absence()
{
    native_missing = true;
    const auto context = make_context();
    DWORD size = 0U;
    CHECK(SCardListReadersA(context,nullptr,nullptr,&size) == SCARD_S_SUCCESS);
    local_missing = true;
    CHECK(SCardListReadersA(context,nullptr,nullptr,&size) == SCARD_E_NO_READERS_AVAILABLE && size == 0U);
    native_missing = false;
    CHECK(SCardListReadersA(context,nullptr,nullptr,&size) == SCARD_S_SUCCESS);
    local_missing = false;
    CHECK(SCardReleaseContext(context) == SCARD_S_SUCCESS);
}
void test_local_failure_keeps_external_readers()
{
    local_error = Error::INVALID_ARGUMENT;
    const auto context = make_context();
    DWORD size = 0U;
    CHECK(SCardListReadersA(context,nullptr,nullptr,&size) == SCARD_S_SUCCESS);
    CHECK(size == std::strlen("External reader") + 2U);
    const auto card = make_card(context,"External reader");
    CHECK(SCardDisconnect(card,SCARD_LEAVE_CARD) == SCARD_S_SUCCESS);
    SCARDHANDLE unavailable = 0U; DWORD active = 0U;
    const int before = connects;
    CHECK(SCardConnectA(context,"PX4 userland (mock)",SCARD_SHARE_SHARED,SCARD_PROTOCOL_T1,&unavailable,&active) == SCARD_E_INVALID_PARAMETER);
    CHECK(connects == before);
    CHECK(SCardReleaseContext(context) == SCARD_S_SUCCESS);
    CHECK(::SetEnvironmentVariableW(L"PX4_WINSCARD_RUNTIME_DIR",L"\\\\server\\share") != 0);
    const auto bad_configuration = make_context();
    CHECK(SCardListReadersA(bad_configuration,nullptr,nullptr,&size) == SCARD_S_SUCCESS);
    CHECK(SCardReleaseContext(bad_configuration) == SCARD_S_SUCCESS);
    CHECK(::SetEnvironmentVariableW(L"PX4_WINSCARD_RUNTIME_DIR",L"C:\\px4-test") != 0);
    local_error = Error::OK;
}
void test_resource_limits()
{
    std::array<SCARDCONTEXT,64U> contexts{};
    for (auto& context : contexts) context = make_context();
    SCARDCONTEXT overflow = 0U;
    const int released_before = releases;
    CHECK(SCardEstablishContext(SCARD_SCOPE_USER,nullptr,nullptr,&overflow) == SCARD_E_NO_MEMORY);
    CHECK(overflow == 0U && releases == released_before + 1);
    for (const auto context : contexts) CHECK(SCardReleaseContext(context) == SCARD_S_SUCCESS);
    const auto context = make_context();
    CHECK(context > contexts.back());
    std::array<SCARDHANDLE,256U> cards{};
    for (auto& card : cards) card = make_card(context,"PX4 userland (mock)");
    SCARDHANDLE extra = 0U; DWORD active = 0U;
    const int before = local_disconnects;
    CHECK(SCardConnectA(context,"PX4 userland (mock)",SCARD_SHARE_SHARED,SCARD_PROTOCOL_T1,&extra,&active) == SCARD_E_NO_MEMORY);
    CHECK(extra == 0U && local_disconnects == before + 1);
    CHECK(SCardReleaseContext(context) == SCARD_S_SUCCESS);
    CHECK(SCardDisconnect(cards.back(),SCARD_LEAVE_CARD) == SCARD_E_INVALID_HANDLE);
}
void test_tvtest_sequence_and_memory()
{
    const auto context = make_context();
    CHECK(SCardIsValidContext(context) == SCARD_S_SUCCESS);
    DWORD characters = SCARD_AUTOALLOCATE;
    LPWSTR names = nullptr;
    CHECK(SCardListReadersW(context,nullptr,reinterpret_cast<LPWSTR>(&names),&characters) == SCARD_S_SUCCESS);
    CHECK(names && characters >= 2U && names[characters-1U] == 0 && names[characters-2U] == 0);
    SCARD_READERSTATEW reader{}; reader.szReader = names; reader.pvUserData = &characters;
    local_initialized = false;
    CHECK(SCardGetStatusChangeW(context,0U,&reader,1U) == SCARD_S_SUCCESS);
    CHECK((reader.dwEventState & SCARD_STATE_PRESENT) != 0U && reader.cbAtr == 0U);
    CHECK(reader.dwCurrentState == 0U && reader.pvUserData == &characters && reader.szReader == names);
    SCARDHANDLE card = 0U; DWORD protocol = 0U;
    CHECK(SCardConnectW(context,names,SCARD_SHARE_SHARED,SCARD_PROTOCOL_T1,&card,&protocol) == SCARD_S_SUCCESS);
    CHECK(SCardFreeMemory(context,names) == SCARD_S_SUCCESS);
    names = nullptr; characters = SCARD_AUTOALLOCATE;
    std::array<BYTE,32U> atr{}; DWORD atr_length = static_cast<DWORD>(atr.size()), state = 0U;
    CHECK(SCardStatusW(card,reinterpret_cast<LPWSTR>(&names),&characters,&state,&protocol,atr.data(),&atr_length) == SCARD_S_SUCCESS);
    CHECK(names && state == SCARD_SPECIFIC && protocol == SCARD_PROTOCOL_T1 && atr_length == 3U && atr[0] == 0x3bU);
    CHECK(names[characters-1U] == 0 && names[characters-2U] == 0);
    const auto foreign = make_context();
    CHECK(SCardFreeMemory(foreign,names) == SCARD_E_INVALID_PARAMETER);
    CHECK(SCardFreeMemory(context,&state) == SCARD_E_INVALID_PARAMETER);
    CHECK(SCardFreeMemory(context,names) == SCARD_S_SUCCESS);
    CHECK(SCardReleaseContext(foreign) == SCARD_S_SUCCESS);
    CHECK(SCardStatusW(card,nullptr,nullptr,nullptr,nullptr,nullptr,nullptr) == SCARD_S_SUCCESS);
    characters = SCARD_AUTOALLOCATE; atr_length = SCARD_AUTOALLOCATE;
    CHECK(SCardStatusW(card,nullptr,&characters,nullptr,nullptr,nullptr,&atr_length) == SCARD_S_SUCCESS);
    CHECK(characters == std::strlen("PX4 userland (mock)") + 2U && atr_length == 3U);
    // TvCas does not free a name on Status failure. Never publish/retain an
    // allocation when the fixed 32-byte ATR buffer cannot hold the response.
    local_atr_size = 33U; names = nullptr; characters = SCARD_AUTOALLOCATE; atr_length = 32U;
    atr.fill(0x55U);
    CHECK(SCardStatusW(card,reinterpret_cast<LPWSTR>(&names),&characters,nullptr,nullptr,atr.data(),&atr_length) == SCARD_E_INSUFFICIENT_BUFFER);
    CHECK(names == nullptr && atr_length == 33U && atr[0] == 0x55U);
    local_atr_size = 3U;
    std::array<LPWSTR,64U> allocations{};
    for (auto& allocation : allocations) {
        characters = SCARD_AUTOALLOCATE;
        CHECK(SCardListReadersW(context,nullptr,reinterpret_cast<LPWSTR>(&allocation),&characters) == SCARD_S_SUCCESS);
    }
    CHECK(SCardFreeMemory(context,allocations.back()) == SCARD_S_SUCCESS); allocations.back() = nullptr;
    names = nullptr; LPBYTE auto_atr = nullptr;
    characters = SCARD_AUTOALLOCATE; atr_length = SCARD_AUTOALLOCATE;
    CHECK(SCardStatusW(card,reinterpret_cast<LPWSTR>(&names),&characters,nullptr,nullptr,reinterpret_cast<LPBYTE>(&auto_atr),&atr_length) == SCARD_E_NO_MEMORY);
    CHECK(names == nullptr && auto_atr == nullptr);
    // The failed two-allocation operation rolled its first allocation back.
    characters = SCARD_AUTOALLOCATE;
    CHECK(SCardListReadersW(context,nullptr,reinterpret_cast<LPWSTR>(&allocations.back()),&characters) == SCARD_S_SUCCESS);
    for (auto allocation : allocations) CHECK(SCardFreeMemory(context,allocation) == SCARD_S_SUCCESS);
    characters = SCARD_AUTOALLOCATE; atr_length = SCARD_AUTOALLOCATE;
    CHECK(SCardStatusW(card,reinterpret_cast<LPWSTR>(&names),&characters,nullptr,nullptr,reinterpret_cast<LPBYTE>(&auto_atr),&atr_length) == SCARD_S_SUCCESS);
    CHECK(auto_atr && auto_atr[0] == 0x3bU);
    CHECK(SCardFreeMemory(context,names) == SCARD_S_SUCCESS);
    CHECK(SCardFreeMemory(context,auto_atr) == SCARD_S_SUCCESS);
    reader.szReader = L"PX4 userland (mock)";
    local_present = false;
    CHECK(SCardGetStatusChangeW(context,0U,&reader,1U) == SCARD_S_SUCCESS);
    CHECK(reader.dwEventState == (SCARD_STATE_EMPTY | SCARD_STATE_CHANGED));
    local_present = true;
    CHECK(SCardGetStatusChangeW(context,1U,&reader,1U) == SCARD_E_UNSUPPORTED_FEATURE);
    CHECK(SCardGetStatusChangeW(context,INFINITE,&reader,1U) == SCARD_E_UNSUPPORTED_FEATURE);
    CHECK(SCardGetStatusChangeW(context,0U,&reader,2U) == SCARD_E_UNSUPPORTED_FEATURE);
    reader.dwCurrentState = SCARD_STATE_PRESENT;
    CHECK(SCardGetStatusChangeW(context,0U,&reader,1U) == SCARD_E_UNSUPPORTED_FEATURE);
    reader.dwCurrentState = 0U; reader.szReader = L"External reader";
    CHECK(SCardGetStatusChangeW(context,0U,&reader,1U) == SCARD_S_SUCCESS);
    reader.szReader = L"PX4 userland (unknown)";
    CHECK(SCardGetStatusChangeW(context,0U,&reader,1U) == SCARD_E_UNKNOWN_READER);
    CHECK(reader.dwEventState == (SCARD_STATE_UNKNOWN | SCARD_STATE_IGNORE | SCARD_STATE_CHANGED));
    local_status_error = Error::CARD_REMOVED;
    CHECK(SCardStatusW(card,nullptr,nullptr,nullptr,nullptr,nullptr,nullptr) == SCARD_W_REMOVED_CARD);
    local_status_error = Error::OK;
    CHECK(SCardDisconnect(card,SCARD_LEAVE_CARD) == SCARD_S_SUCCESS);
    const auto external = make_card(context,"External reader");
    characters = SCARD_AUTOALLOCATE; atr_length = 32U; names = nullptr;
    CHECK(SCardStatusW(external,reinterpret_cast<LPWSTR>(&names),&characters,&state,&protocol,atr.data(),&atr_length) == SCARD_S_SUCCESS);
    CHECK(names && std::wcscmp(names,L"External reader") == 0 && atr_length == 3U);
    // Outstanding memory and external card are released with their context.
    CHECK(SCardReleaseContext(context) == SCARD_S_SUCCESS);
    CHECK(SCardFreeMemory(context,names) == SCARD_E_INVALID_HANDLE);
    CHECK(SCardIsValidContext(context) == ERROR_INVALID_HANDLE);
}
} // namespace

namespace px4::userland::windows_card {
Result<std::vector<Reader>> list_readers(const std::string& root) noexcept
{
    if (root.empty()) return Result<std::vector<Reader>>::failure(Error::INVALID_ARGUMENT);
    if (local_error != Error::OK) return Result<std::vector<Reader>>::failure(local_error);
    std::vector<Reader> readers;
    if (!local_missing) readers.push_back({"mock","PX4 userland (mock)"});
    return Result<std::vector<Reader>>::success(std::move(readers));
}
Result<std::unique_ptr<CardClient>> CardClient::connect(const std::string&,const std::string& instance,
                                                       ipc::ShareMode mode) noexcept
{
    CHECK(instance == "mock" && (mode == ipc::ShareMode::shared || mode == ipc::ShareMode::exclusive));
    local_initialized = true;
    dtv::userland::wire::CardClientSession session{};
    session.card_handle = 0xfedcba9876543210ULL;
    session.bound_generation = 1U;
    return Result<std::unique_ptr<CardClient>>::success(
        std::unique_ptr<CardClient>(new CardClient(nullptr, session, {})));
}
CardClient::~CardClient() noexcept { ++local_closes; }
Result<std::vector<std::uint8_t>> CardClient::transmit(ByteView) noexcept
{
    CHECK(session_.card_handle == 0xfedcba9876543210ULL); ++local_transmits;
    return Result<std::vector<std::uint8_t>>::success({0x90U,0U});
}
Result<void> CardClient::disconnect(ipc::Disposition disposition) noexcept
{
    CHECK(disposition == ipc::Disposition::leave || disposition == ipc::Disposition::reset);
    ++local_disconnects;
    return Result<void>::success();
}
Result<ReaderStatus> reader_status(const std::string&,const std::string& instance) noexcept
{
    CHECK(instance == "mock");
    ReaderStatus result{local_present,local_initialized,0xfedcba9876543210ULL,{}};
    if (local_present && local_initialized) result.atr.assign(local_atr_size,0x3bU);
    return Result<ReaderStatus>::success(std::move(result));
}
Result<ReaderStatus> CardClient::status() noexcept
{
    if (local_status_error != Error::OK) return Result<ReaderStatus>::failure(local_status_error);
    return reader_status({},"mock");
}
} // namespace px4::userland::windows_card

int main()
{
    CHECK(::SetEnvironmentVariableW(L"PX4_WINSCARD_RUNTIME_DIR",L"C:\\px4-test") != 0);
    set_native_api_for_tests({establish,release,list_a,list_w,connect_a,connect_w,disconnect,transmit,status_w,change_w});
    CHECK(g_rgSCardT1Pci.dwProtocol == 2U && g_rgSCardT1Pci.cbPciLength == 8U);
    test_lists(); test_local(); test_native_and_collision(); test_release_race(); test_provider_absence();
    test_local_failure_keeps_external_readers(); test_resource_limits();
    test_tvtest_sequence_and_memory();
    CHECK(SCardReleaseContext(native_context) == SCARD_E_INVALID_HANDLE);
    CHECK(::SetEnvironmentVariableW(L"PX4_WINSCARD_RUNTIME_DIR",L"\\\\server\\share") != 0);
    native_missing = true;
    SCARDCONTEXT context = 0U;
    CHECK(SCardEstablishContext(SCARD_SCOPE_USER,nullptr,nullptr,&context) == SCARD_E_INVALID_PARAMETER);
    std::printf("WinSCard proxy tests: %d failures\n",failures.load());
    return failures.load() == 0 ? 0 : 1;
}
