// traffic.h 的單元測試，不需要 Qt 或網卡。
#include "../traffic.h"

#include <cmath>
#include <iostream>

using namespace traffic;

static int failures = 0;
static void check(bool condition, const char *message)
{
    if (!condition) { std::cerr << "FAIL: " << message << '\n'; ++failures; }
}
static bool near(double a, double b) { return std::fabs(a - b) <= 1e-6 * std::max(1.0, std::fabs(b)); }

static InterfaceInfo nic(std::uint64_t luid, const char *guid, bool hardware, bool filter, bool up,
                         std::uint64_t in, std::uint64_t out)
{
    InterfaceInfo i;
    i.luid = luid; i.guid = guid; i.hardware = hardware; i.filter = filter; i.up = up;
    i.inOctets = in; i.outOctets = out;
    return i;
}

int main()
{
    // 典型情況：I219-V 本體 + 兩個同計數的篩選介面 + VPN + loopback
    InterfaceInfo loop = nic(1, "{lo}", false, false, true, 999, 999);
    loop.loopback = true;
    const std::vector<InterfaceInfo> before = {
        nic(10, "{eth}", true, false, true, 1000, 500),
        nic(11, "{eth-wfp}", false, true, true, 1000, 500),
        nic(12, "{eth-qos}", false, true, true, 1000, 500),
        nic(20, "{vpn}", false, false, true, 100, 50),
        loop,
    };
    const std::vector<InterfaceInfo> after = {
        nic(10, "{eth}", true, false, true, 1000 + 125000, 500 + 12500),
        nic(11, "{eth-wfp}", false, true, true, 1000 + 125000, 500 + 12500),
        nic(12, "{eth-qos}", false, true, true, 1000 + 125000, 500 + 12500),
        nic(20, "{vpn}", false, false, true, 100 + 25000, 50 + 2500),
        loop,
    };

    const Selection physical{Scope::Physical, {}};
    const Selection all{Scope::All, {}};
    const Selection single{Scope::Single, "{eth}"};

    Rates r = computeRates(selectCounters(before, physical), selectCounters(after, physical), 1.0);
    check(near(r.down, 1e6) && near(r.up, 1e5), "physical: filter interfaces not double counted");

    r = computeRates(selectCounters(before, single), selectCounters(after, single), 0.5);
    check(near(r.down, 2e6) && near(r.up, 2e5), "single adapter, 0.5 s interval");

    r = computeRates(selectCounters(before, all), selectCounters(after, all), 1.0);
    check(near(r.down, 1.2e6) && near(r.up, 1.2e5), "all: includes VPN, excludes filters and loopback");

    // 介面剛上線，帶著很大的累積計數 → 不可出現尖峰
    std::vector<InterfaceInfo> withNew = after;
    withNew.push_back(nic(30, "{usb}", true, false, true, 5000000000ULL, 5000000000ULL));
    r = computeRates(selectCounters(after, physical), selectCounters(withNew, physical), 1.0);
    check(near(r.down, 0) && near(r.up, 0), "new interface does not create a spike");

    // 介面消失 → 其他介面的流量照算，不會被扣掉
    const std::vector<InterfaceInfo> twoBefore = {
        nic(10, "{eth}", true, false, true, 0, 0),
        nic(40, "{wifi}", true, false, true, 9000000, 9000000),
    };
    const std::vector<InterfaceInfo> wifiGone = {nic(10, "{eth}", true, false, true, 125000, 0)};
    r = computeRates(selectCounters(twoBefore, physical), selectCounters(wifiGone, physical), 1.0);
    check(near(r.down, 1e6) && near(r.up, 0), "removed interface does not cancel others");

    // 計數器重設（變小）→ 該介面記 0
    const std::vector<InterfaceInfo> reset = {nic(10, "{eth}", true, false, true, 10, 10)};
    r = computeRates(selectCounters(before, physical), selectCounters(reset, physical), 1.0);
    check(near(r.down, 0) && near(r.up, 0), "counter reset gives zero");

    // 介面斷線（OperStatus 不是 Up）不計入
    std::vector<InterfaceInfo> down = after;
    down[0].up = false;
    r = computeRates(selectCounters(before, physical), selectCounters(down, physical), 1.0);
    check(near(r.down, 0), "down interface ignored");

    check(near(computeRates({}, {}, 0).down, 0), "zero interval is safe");

    // 網卡選單候選
    check(isSelectableAdapter(before[0]) && !isSelectableAdapter(before[1]) && !isSelectableAdapter(before[3]),
          "only physical adapters are selectable");

    Session session;
    auto delta = computeRates({{1, 100, 200}}, {{1, 150, 275}}, 0.5);
    session.add(delta);
    check(session.down == 50 && session.up == 75, "session exact byte deltas");
    session.add(computeRates({{1, 150, 275}}, {{1, 9999, 9999}}, 60));
    check(session.down == 50 && session.up == 75, "sleep gap excluded");
    session.add(computeRates({{1, 150, 275}}, {{1, 1, 2}}, 0.5));
    check(session.down == 50 && session.up == 75, "counter reset excluded");
    session.add(computeRates({{1, 1, 2}}, {{2, 9999, 9999}}, 0.5));
    check(session.down == 50 && session.up == 75, "new adapter baseline excluded");
    session = {};
    check(session.down == 0 && session.up == 0, "session reset");
    // 縱軸刻度
    check(near(niceStep(0.7), 1) && near(niceStep(1.3), 2) && near(niceStep(3), 5) && near(niceStep(7), 10),
          "nice step 1/2/5");
    Axis a = niceAxis(5.22);
    check(near(a.top, 6) && a.intervals == 3, "axis 5.22 -> 0..6 in 3 steps");
    a = niceAxis(100);
    check(near(a.top, 100) && a.intervals == 2, "axis 100 -> 0..100 in 2 steps of 50");
    a = niceAxis(0.0123);
    check(a.top >= 0.0123 && a.intervals >= 2 && a.intervals <= 4, "axis tiny values");
    for (double p : {0.3, 1.0, 1.7, 9.99, 37.0, 999.0, 1234.5}) {
        a = niceAxis(p);
        check(a.top >= p - 1e-9 && a.intervals >= 2 && a.intervals <= 4, "axis covers peak with 2-4 steps");
    }
    a = niceAxis(0);
    check(a.top > 0, "axis zero peak");
    a = niceAxis(std::nan(""));
    check(a.top > 0, "axis NaN peak");

    std::cout << (failures ? "FAILED" : "PASS") << ": traffic rates, interface filtering, axis scaling\n";
    return failures ? 1 : 0;
}
