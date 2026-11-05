// 流量統計的純邏輯（不依賴 Windows 與 Qt，方便單元測試）。
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <limits>
#include <vector>

namespace traffic {

// 一筆網路介面資料（由 GetIfTable2 的 MIB_IF_ROW2 轉換而來）。
struct InterfaceInfo {
    std::uint64_t luid = 0;     // 本次開機內唯一，用來比對前後兩次取樣
    std::string guid;           // 跨重新開機穩定，用來儲存使用者選擇
    std::string description;    // 例如 "Intel(R) Ethernet Connection (2) I219-V"（UTF-8）
    std::string alias;          // 例如 "乙太網路"（UTF-8）
    bool loopback = false;
    bool filter = false;        // NDIS 篩選驅動介面（與實體網卡計數重複）
    bool hardware = false;      // 實體網卡
    bool up = false;
    unsigned index = 0;
    unsigned operStatus = 0;
    unsigned adminStatus = 0;
    std::uint64_t receiveLink = 0, transmitLink = 0;
    std::string mac;
    std::uint64_t inOctets = 0;
    std::uint64_t outOctets = 0;
};

enum class Scope { Physical, All, Single };

struct Selection {
    Scope scope = Scope::Physical;
    std::string guid;           // 只在 Scope::Single 使用
};

// 可選擇實體、VPN 與虛擬介面，排除 loopback 和重複的篩選介面。
inline bool isSelectableAdapter(const InterfaceInfo &i)
{
    return !i.loopback && !i.filter;
}

// 這個介面是否計入目前選擇的統計。
inline bool counts(const InterfaceInfo &i, const Selection &s)
{
    if (i.loopback || i.filter || !i.up)
        return false;
    switch (s.scope) {
    case Scope::Physical: return i.hardware;
    case Scope::All: return true;
    case Scope::Single: return i.guid == s.guid;
    }
    return false;
}

struct Counter {
    std::uint64_t luid = 0;
    std::uint64_t in = 0;
    std::uint64_t out = 0;
};

inline std::vector<Counter> selectCounters(const std::vector<InterfaceInfo> &infos, const Selection &s)
{
    std::vector<Counter> result;
    for (const InterfaceInfo &i : infos)
        if (counts(i, s))
            result.push_back({i.luid, i.inOctets, i.outOctets});
    return result;
}

struct Rates {
    std::uint64_t inBytes = 0, outBytes = 0;
    double seconds = 0;         // 有共同有效計數器的取樣時間
    double down = 0;            // bit/s
    double up = 0;              // bit/s
};

// 逐介面計算差值：只計入前後兩次都存在的介面；計數器變小（重設）時該介面記 0。
// 這樣介面上線、斷線或 VPN 連上時，不會出現假的尖峰或把其他介面的流量扣掉。
inline Rates computeRates(const std::vector<Counter> &previous, const std::vector<Counter> &current, double seconds)
{
    Rates r;
    if (!(seconds > 0) || seconds > 2.0 || !std::isfinite(seconds))
        return r;
    auto &inBytes = r.inBytes; auto &outBytes = r.outBytes;
    for (const Counter &c : current) {
        const auto it = std::find_if(previous.begin(), previous.end(),
                                     [&](const Counter &p) { return p.luid == c.luid; });
        if (it == previous.end())
            continue;
        if (c.in < it->in || c.out < it->out) continue;
        r.seconds = seconds;
        inBytes += (c.in - it->in);
        outBytes += (c.out - it->out);
    }
    r.down = inBytes * 8.0 / seconds;
    r.up = outBytes * 8.0 / seconds;
    return r;
}

struct Session {
    std::uint64_t down = 0, up = 0;
    double seconds = 0, peakDown = 0, peakUp = 0;
    double averageDown() const { return seconds > 0 ? down * 8.0 / seconds : 0; }
    double averageUp() const { return seconds > 0 ? up * 8.0 / seconds : 0; }
    void add(const Rates &r) {
        seconds += r.seconds;
        peakDown = std::max(peakDown, r.down);
        peakUp = std::max(peakUp, r.up);
        const auto max = std::numeric_limits<std::uint64_t>::max();
        down += std::min(r.inBytes, max - down);
        up += std::min(r.outBytes, max - up);
    }
};

// 不小於 raw 的「好讀」刻度間距：1、2、5 × 10^n。
inline double niceStep(double raw)
{
    if (!(raw > 0) || !std::isfinite(raw))
        return 1.0;
    const double magnitude = std::pow(10.0, std::floor(std::log10(raw)));
    const double fraction = raw / magnitude;
    double nice = 10.0;
    if (fraction <= 1.0 + 1e-9) nice = 1.0;
    else if (fraction <= 2.0 + 1e-9) nice = 2.0;
    else if (fraction <= 5.0 + 1e-9) nice = 5.0;
    return nice * magnitude;
}

struct Axis {
    double top = 1.0;           // 縱軸最大值（與輸入同單位）
    int intervals = 1;          // 格數（2～4）
};

// 依峰值決定縱軸：最大值與每格都是整齊的數字，峰值不會超出頂端。
inline Axis niceAxis(double peak)
{
    if (!(peak > 0) || !std::isfinite(peak))
        peak = 1.0;
    const double step = niceStep(peak / 4.0);
    int n = int(std::ceil(peak / step - 1e-9));
    n = std::clamp(n, 1, 4);
    return {n * step, n};
}

} // namespace traffic
