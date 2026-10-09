// bon-ccprobe: concurrent 8-receiver TS continuity probe for px4_drv BonDriver and px4-ts (px4-userland).
// CC rules mirror px4-userland observe_continuity (q3u4_stream.cpp):
//  - PID 0x1FFF ignored; AFC 0/2 packets ignored
//  - discontinuity_indicator rebases (clean)
//  - first packet per PID is a boundary; until two consecutive CCs are seen the PID stays unestablished
//    (non-consecutive = boundary); once established every mismatch (incl. duplicate) is a fault
//  - startup stabilization like px4d: faults before (>=16384 packets && >=4096 consecutive clean && any
//    PID established) are counted separately as startup faults (cap 131072 packets)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>
#include <thread>
#include <atomic>
#include <mutex>
#include "IBonDriver2.h"

static LARGE_INTEGER g_freq, g_t0;
static double now_ms() { LARGE_INTEGER c; QueryPerformanceCounter(&c); return (double)(c.QuadPart - g_t0.QuadPart) * 1000.0 / (double)g_freq.QuadPart; }

struct CcEvent { double ms; int pid; int expected; int got; uint64_t pkt; bool dup; bool startup; };

struct Analyzer {
    struct St { bool seen = false, established = false; uint8_t cc = 0; };
    std::vector<St> st = std::vector<St>(8192);
    uint8_t carry[188]; size_t carry_len = 0;
    uint64_t total = 0, nulls = 0, delivered = 0, boundary = 0, cc_err = 0, dup = 0, lost = 0, tei = 0, sync_loss = 0, bytes = 0;
    uint64_t st_pk = 0, st_clean = 0, st_cc = 0, st_tei = 0; bool stabilizing = true, est_any = false, st_forced = false;
    double stab_ms = -1, first_ms = -1, last_ms = -1;
    int pat_tsid = -1;
    std::vector<CcEvent> ev;
    void packet(const uint8_t* p, double ms) {
        total++;
        if (first_ms < 0) first_ms = ms;
        last_ms = ms;
        int pid = ((p[1] & 0x1f) << 8) | p[2];
        bool teiF = (p[1] & 0x80) != 0;
        if (pid == 0 && pat_tsid < 0 && (p[1] & 0x40) && !teiF) {
            int afc = (p[3] >> 4) & 3; int off = 4;
            if (afc == 3) off += 1 + p[4];
            if ((afc & 1) && off < 188) { off += 1 + p[off]; if (off + 5 < 188 && p[off] == 0) pat_tsid = (p[off + 3] << 8) | p[off + 4]; }
        }
        if (stabilizing) {
            st_pk++;
            if (teiF) st_tei++;
            int o = observe(p, pid, ms, true);
            if (o == 2) { st_cc++; st_clean = 0; } else st_clean++;
            if (est_any && st_pk >= 16384 && st_clean >= 4096) { stabilizing = false; stab_ms = ms; }
            else if (st_pk >= 131072) { stabilizing = false; st_forced = true; stab_ms = ms; }
            return;
        }
        if (teiF) tei++;
        if (pid == 0x1fff) nulls++;
        int o = observe(p, pid, ms, false);
        if (o == 1) { boundary++; return; }
        if (o == 2) cc_err++;
        delivered++;
    }
    // 0 clean, 1 boundary, 2 fault
    int observe(const uint8_t* p, int pid, double ms, bool startup) {
        if (pid == 0x1fff) return 0;
        int afc = (p[3] >> 4) & 3; uint8_t cc = p[3] & 0x0f;
        if (afc == 0 || afc == 2) return 0;
        bool disc = false;
        if (afc == 3) { size_t len = p[4]; if (len != 0 && len + 5 <= 188) disc = (p[5] & 0x80) != 0; }
        St& s = st[pid];
        if (disc) { s.seen = true; s.established = true; s.cc = cc; est_any = true; return 0; }
        if (!s.seen) { s.seen = true; s.cc = cc; return 1; }
        uint8_t exp = (s.cc + 1) & 0x0f;
        if (!s.established) { s.cc = cc; if (cc != exp) return 1; s.established = true; est_any = true; return 0; }
        bool fault = cc != exp;
        bool isdup = fault && cc == s.cc;
        s.cc = cc;
        if (fault) {
            if (!startup) { if (isdup) dup++; else lost += (uint8_t)((cc - exp) & 0x0f); }
            if (ev.size() < 4000) ev.push_back(CcEvent{ ms, pid, exp, cc, total, isdup, startup });
            return 2;
        }
        return 0;
    }
    void feed(const uint8_t* b, size_t n, double ms) {
        bytes += n;
        size_t i = 0;
        if (carry_len) {
            size_t need = 188 - carry_len, take = need < n ? need : n;
            memcpy(carry + carry_len, b, take); carry_len += take; i = take;
            if (carry_len < 188) return;
            if (carry[0] == 0x47) packet(carry, ms); else sync_loss++;
            carry_len = 0;
        }
        while (i < n) {
            if (b[i] != 0x47) {
                sync_loss++;
                size_t j = i + 1;
                while (j < n && !(b[j] == 0x47 && (j + 188 >= n || b[j + 188] == 0x47))) j++;
                i = j; continue;
            }
            if (n - i < 188) { memcpy(carry, b + i, n - i); carry_len = n - i; break; }
            packet(b + i, ms); i += 188;
        }
    }
};

struct Rx {
    std::string label, kind; // kind S/T
    Analyzer a;
    std::string note;
    bool open_ok = false, tune_ok = false;
    double open_ms = -1, tune_ms = -1, cnr = -1;
    int exit_code = -999;
    std::string stream_end;
    uint64_t reads = 0, max_remain = 0;
};

static void jstr(FILE* f, const std::string& s) {
    fputc('"', f);
    for (unsigned char c : s) { if (c == '"' || c == '\\') { fputc('\\', f); fputc(c, f); } else if (c < 0x20) fprintf(f, "\\u%04x", c); else fputc(c, f); }
    fputc('"', f);
}

static std::vector<Rx> g_rx(8);
static std::string g_mode, g_out;
static double g_seconds = 30;
static std::mutex g_wmtx;

static void write_json(const char* status) {
    std::lock_guard<std::mutex> lk(g_wmtx);
    FILE* f = fopen(g_out.c_str(), "wb");
    if (!f) return;
    SYSTEMTIME t; GetSystemTime(&t);
    fprintf(f, "{\"mode\":"); jstr(f, g_mode);
    fprintf(f, ",\"status\":"); jstr(f, status);
    fprintf(f, ",\"written_utc\":\"%04d-%02d-%02dT%02d:%02d:%02d.%03dZ\",\"seconds\":%.1f,\"rx\":[", t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute, t.wSecond, t.wMilliseconds, g_seconds);
    for (size_t i = 0; i < g_rx.size(); i++) {
        Rx& r = g_rx[i]; Analyzer& a = r.a;
        if (i) fputc(',', f);
        fprintf(f, "\n{\"idx\":%zu,\"label\":", i); jstr(f, r.label);
        fprintf(f, ",\"kind\":"); jstr(f, r.kind);
        fprintf(f, ",\"open_ok\":%s,\"tune_ok\":%s,\"open_ms\":%.1f,\"tune_ms\":%.1f,\"cnr\":%.3f,\"exit_code\":%d",
            r.open_ok ? "true" : "false", r.tune_ok ? "true" : "false", r.open_ms, r.tune_ms, r.cnr, r.exit_code);
        fprintf(f, ",\"bytes\":%llu,\"packets_total\":%llu,\"packets_delivered\":%llu,\"nulls\":%llu,\"boundary\":%llu,\"cc_errors\":%llu,\"cc_dup\":%llu,\"est_lost\":%llu,\"tei\":%llu,\"sync_loss\":%llu",
            (unsigned long long)a.bytes, (unsigned long long)a.total, (unsigned long long)a.delivered, (unsigned long long)a.nulls, (unsigned long long)a.boundary,
            (unsigned long long)a.cc_err, (unsigned long long)a.dup, (unsigned long long)a.lost, (unsigned long long)a.tei, (unsigned long long)a.sync_loss);
        fprintf(f, ",\"startup_packets\":%llu,\"startup_cc\":%llu,\"startup_tei\":%llu,\"stabilized_ms\":%.1f,\"stab_forced\":%s,\"first_ms\":%.1f,\"last_ms\":%.1f,\"pat_tsid\":%d,\"reads\":%llu,\"max_remain\":%llu",
            (unsigned long long)a.st_pk, (unsigned long long)a.st_cc, (unsigned long long)a.st_tei, a.stab_ms, a.st_forced ? "true" : "false", a.first_ms, a.last_ms, a.pat_tsid,
            (unsigned long long)r.reads, (unsigned long long)r.max_remain);
        fprintf(f, ",\"note\":"); jstr(f, r.note);
        fprintf(f, ",\"stream_end\":"); jstr(f, r.stream_end);
        fprintf(f, ",\"events\":[");
        for (size_t k = 0; k < a.ev.size(); k++) {
            const CcEvent& e = a.ev[k];
            fprintf(f, "%s[%.2f,%d,%d,%d,%llu,%d,%d]", k ? "," : "", e.ms, e.pid, e.expected, e.got, (unsigned long long)e.pkt, e.dup ? 1 : 0, e.startup ? 1 : 0);
        }
        fprintf(f, "]}");
    }
    fprintf(f, "\n]}\n");
    fclose(f);
}

static void watchdog(double limit_s) {
    std::thread([limit_s] {
        Sleep((DWORD)(limit_s * 1000));
        write_json("watchdog-timeout");
        fprintf(stderr, "watchdog timeout\n");
        ExitProcess(3);
    }).detach();
}

typedef IBonDriver* (*CreateBonDriverFn)();

// Each instance needs its own DLL module: copies are made by the harness (BonDriver_PX4-S0.dll .. -S3, -T0 .. -T3).
static int run_bon(const std::wstring& dir) {
    watchdog(g_seconds + 120);
    const char* kinds = "SSSSTTTT";
    std::vector<IBonDriver2*> b(8, nullptr);
    std::vector<HMODULE> mods(8, nullptr);
    for (int i = 0; i < 8; i++) {
        int k = i % 4;
        g_rx[i].kind = std::string(1, kinds[i]);
        g_rx[i].label = std::string(kinds[i] == 'S' ? "S" : "T") + std::to_string(k);
        std::wstring path = dir + L"\\BonDriver_PX4-" + (kinds[i] == 'S' ? L"S" : L"T") + std::to_wstring(k) + L".dll";
        mods[i] = LoadLibraryW(path.c_str());
        if (!mods[i]) { g_rx[i].note = "LoadLibrary failed " + std::to_string(GetLastError()); continue; }
        auto fn = (CreateBonDriverFn)GetProcAddress(mods[i], "CreateBonDriver");
        if (!fn) { g_rx[i].note = "no CreateBonDriver"; continue; }
        IBonDriver* p = fn();
        if (!p) { g_rx[i].note = "CreateBonDriver null"; continue; }
        b[i] = static_cast<IBonDriver2*>(p);
    }
    // open sequentially: S0..S3, T0..T3
    for (int i = 0; i < 8; i++) {
        if (!b[i]) continue;
        double t = now_ms();
        g_rx[i].open_ok = b[i]->OpenTuner() != FALSE;
        g_rx[i].open_ms = now_ms() - t;
        if (!g_rx[i].open_ok) g_rx[i].note += "OpenTuner failed;";
        else {
            LPCWSTR nm = b[i]->EnumChannelName(0, g_rx[i].kind == "S" ? 13 : 9);
            LPCWSTR tn = b[i]->GetTunerName();
            char u8[512] = {0}, u8t[512] = {0};
            if (nm) WideCharToMultiByte(CP_UTF8, 0, nm, -1, u8, sizeof u8 - 1, nullptr, nullptr);
            if (tn) WideCharToMultiByte(CP_UTF8, 0, tn, -1, u8t, sizeof u8t - 1, nullptr, nullptr);
            g_rx[i].note += std::string("tuner=") + u8t + ";ch=" + u8 + ";";
        }
        fprintf(stderr, "open %s ok=%d %.0fms\n", g_rx[i].label.c_str(), (int)g_rx[i].open_ok, g_rx[i].open_ms);
    }
    std::atomic<bool> stop{ false };
    std::vector<std::thread> th;
    double tstart = now_ms();
    for (int i = 0; i < 8; i++) {
        if (!b[i] || !g_rx[i].open_ok) continue;
        double t = now_ms();
        // S: space0 ch13 = BS15/TS0 (TSID 16625=0x40F1, ptx 7 -> 1318000 kHz); T: space0 ch9 = 22ch (ptx 72 -> 527143 kHz)
        BOOL ok = (g_rx[i].kind == "S") ? b[i]->SetChannel((DWORD)0, (DWORD)13) : b[i]->SetChannel((DWORD)0, (DWORD)9);
        g_rx[i].tune_ok = ok != FALSE;
        g_rx[i].tune_ms = now_ms() - t;
        fprintf(stderr, "tune %s ok=%d %.0fms\n", g_rx[i].label.c_str(), (int)g_rx[i].tune_ok, g_rx[i].tune_ms);
        if (!ok) { g_rx[i].note += "SetChannel failed;"; continue; }
        th.emplace_back([i, &b, &stop] {
            Rx& r = g_rx[i];
            double end = now_ms() + g_seconds * 1000.0;
            while (now_ms() < end) {
                b[i]->WaitTsStream(100);
                for (;;) {
                    BYTE* p = nullptr; DWORD sz = 0, remain = 0;
                    if (!b[i]->GetTsStream(&p, &sz, &remain) || !p || sz == 0) break;
                    r.reads++; if (remain > r.max_remain) r.max_remain = remain;
                    r.a.feed(p, sz, now_ms());
                    if (remain == 0) break;
                }
            }
        });
    }
    for (auto& t : th) t.join();
    for (int i = 0; i < 8; i++) if (b[i] && g_rx[i].open_ok) g_rx[i].cnr = b[i]->GetSignalLevel();
    write_json("closing");
    for (int i = 0; i < 8; i++) if (b[i]) { if (g_rx[i].open_ok) b[i]->CloseTuner(); b[i]->Release(); }
    write_json("ok");
    (void)tstart;
    return 0;
}

static std::wstring quote(const std::wstring& s) {
    if (s.find_first_of(L" \t\"") == std::wstring::npos) return s;
    return L"\"" + s + L"\"";
}

static int run_px4ts(const std::wstring& exe, const std::wstring& dev, const std::wstring& rt, const std::wstring& lad, const std::wstring& outdir) {
    watchdog(g_seconds + 150);
    if (!lad.empty()) SetEnvironmentVariableW(L"LOCALAPPDATA", lad.c_str());
    std::vector<HANDLE> procs(8, nullptr), rd(8, nullptr);
    std::vector<std::wstring> errpaths(8);
    for (int r = 0; r < 8; r++) {
        bool s = (r == 0 || r == 1 || r == 4 || r == 5);
        g_rx[r].kind = s ? "S" : "T"; g_rx[r].label = "r" + std::to_string(r);
        std::wstring cmd = quote(exe) + L" --device " + dev + L" --runtime-dir " + quote(rt) + L" --receiver " + std::to_wstring(r);
        cmd += s ? L" --system isdb-s --frequency-khz 1318000 --slot 0" : L" --system isdb-t --frequency-khz 527143";
        cmd += L" --output - --duration-seconds " + std::to_wstring((int)g_seconds);
        SECURITY_ATTRIBUTES sa{ sizeof(sa), nullptr, TRUE };
        HANDLE r0, w0;
        if (!CreatePipe(&r0, &w0, &sa, 4 * 1024 * 1024)) { g_rx[r].note = "CreatePipe failed"; continue; }
        SetHandleInformation(r0, HANDLE_FLAG_INHERIT, 0);
        errpaths[r] = outdir + L"\\r" + std::to_wstring(r) + L".err";
        HANDLE ef = CreateFileW(errpaths[r].c_str(), GENERIC_WRITE, FILE_SHARE_READ, &sa, CREATE_ALWAYS, 0, nullptr);
        HANDLE nulin = CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, &sa, OPEN_EXISTING, 0, nullptr);
        STARTUPINFOW si{}; si.cb = sizeof(si); si.dwFlags = STARTF_USESTDHANDLES; si.hStdOutput = w0; si.hStdError = ef; si.hStdInput = nulin;
        PROCESS_INFORMATION pi{};
        std::vector<wchar_t> cl(cmd.begin(), cmd.end()); cl.push_back(0);
        BOOL ok = CreateProcessW(nullptr, cl.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi);
        CloseHandle(w0); CloseHandle(ef); CloseHandle(nulin);
        if (!ok) { g_rx[r].note = "CreateProcess failed " + std::to_string(GetLastError()); CloseHandle(r0); continue; }
        CloseHandle(pi.hThread);
        procs[r] = pi.hProcess; rd[r] = r0; g_rx[r].open_ok = true;
    }
    std::vector<std::thread> th;
    for (int r = 0; r < 8; r++) {
        if (!rd[r]) continue;
        th.emplace_back([r, &rd] {
            std::vector<uint8_t> buf(188 * 2048);
            for (;;) {
                DWORD n = 0;
                if (!ReadFile(rd[r], buf.data(), (DWORD)buf.size(), &n, nullptr) || n == 0) break;
                g_rx[r].reads++;
                g_rx[r].a.feed(buf.data(), n, now_ms());
            }
            CloseHandle(rd[r]);
        });
    }
    for (auto& t : th) t.join();
    for (int r = 0; r < 8; r++) {
        if (!procs[r]) continue;
        WaitForSingleObject(procs[r], 30000);
        DWORD ec = 0; GetExitCodeProcess(procs[r], &ec); g_rx[r].exit_code = (int)ec; CloseHandle(procs[r]);
        FILE* f = _wfopen(errpaths[r].c_str(), L"rb");
        if (f) {
            char line[4096];
            while (fgets(line, sizeof line, f)) {
                std::string l(line);
                while (!l.empty() && (l.back() == '\n' || l.back() == '\r')) l.pop_back();
                if (l.find("STREAM_END packets") != std::string::npos || (g_rx[r].stream_end.empty() && l.rfind("stream packets", 0) == 0)) g_rx[r].stream_end = l;
            }
            fclose(f);
        }
    }
    write_json("ok");
    return 0;
}

int wmain(int argc, wchar_t** argv) {
    QueryPerformanceFrequency(&g_freq); QueryPerformanceCounter(&g_t0);
    auto narrow = [](const std::wstring& w) { std::string s; for (wchar_t c : w) s.push_back((char)c); return s; };
    if (argc < 2) { fprintf(stderr, "usage: bon-ccprobe bon --dir D --out F [--seconds N]\n       bon-ccprobe px4ts --exe X --device ID --runtime-dir RT --localappdata L --errdir E --out F [--seconds N]\n"); return 2; }
    std::wstring mode = argv[1], dir, exe, dev, rt, lad, errdir, out;
    for (int i = 2; i + 1 < argc; i += 2) {
        std::wstring k = argv[i], v = argv[i + 1];
        if (k == L"--dir") dir = v; else if (k == L"--exe") exe = v; else if (k == L"--device") dev = v;
        else if (k == L"--runtime-dir") rt = v; else if (k == L"--localappdata") lad = v; else if (k == L"--errdir") errdir = v;
        else if (k == L"--out") out = v; else if (k == L"--seconds") g_seconds = _wtof(v.c_str());
    }
    g_mode = narrow(mode); g_out = narrow(out);
    if (g_out.empty()) return 2;
    if (mode == L"bon") return run_bon(dir);
    if (mode == L"px4ts") return run_px4ts(exe, dev, rt, lad, errdir);
    return 2;
}
