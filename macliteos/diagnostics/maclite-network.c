/* maclite-network — link, addressing, DNS and reachability (spec §10).
 *
 * Reads sysfs/procfs and does real work for the checks: a DNS lookup through
 * the resolver, a TCP connect, and the interface state. There is no daemon and
 * no polling loop; --up/--down are one-shot ioctls for scripted checks.
 *
 *   maclite-network                 report + checks
 *   maclite-network --host NAME     use another name for the resolution check
 *   maclite-network --up IFACE      bring an interface up (needs CAP_NET_ADMIN)
 *   maclite-network --down IFACE    take it down
 */
#include "diag_common.h"
#include <net/if.h>
#include <netdb.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <poll.h>
#include <errno.h>

/* One-shot link state change: SIOCSIFFLAGS. Returns false with errno set when
 * the process lacks CAP_NET_ADMIN. */
static bool set_link(const char *iface, bool up)
{
    int s = socket(AF_INET, SOCK_DGRAM, 0);
    if (s < 0) return false;
    struct ifreq ifr;
    memset(&ifr, 0, sizeof ifr);
    snprintf(ifr.ifr_name, sizeof ifr.ifr_name, "%s", iface);
    bool ok = false;
    if (ioctl(s, SIOCGIFFLAGS, &ifr) == 0) {
        if (up) ifr.ifr_flags |= IFF_UP;
        else    ifr.ifr_flags &= (short)~IFF_UP;
        ok = ioctl(s, SIOCSIFFLAGS, &ifr) == 0;
    }
    close(s);
    return ok;
}

/* TCP connect with a timeout — the honest "is anything out there" check. A UDP
 * DNS packet would be answered by a cached local stub and prove less. */
static bool tcp_reachable(const char *ip, int port, int timeout_ms)
{
    struct sockaddr_in sa;
    memset(&sa, 0, sizeof sa);
    sa.sin_family = AF_INET;
    sa.sin_port = htons((uint16_t)port);
    if (inet_pton(AF_INET, ip, &sa.sin_addr) != 1) return false;
    int fd = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0);
    if (fd < 0) return false;
    int rc = connect(fd, (struct sockaddr *)&sa, sizeof sa);
    if (rc == 0) { close(fd); return true; }
    if (errno != EINPROGRESS) { close(fd); return false; }
    struct pollfd p = { .fd = fd, .events = POLLOUT };
    bool ok = poll(&p, 1, timeout_ms) == 1;
    if (ok) {
        int err = 0;
        socklen_t l = sizeof err;
        if (getsockopt(fd, SOL_SOCKET, SO_ERROR, &err, &l) != 0 || err != 0) ok = false;
    }
    close(fd);
    return ok;
}

static bool name_resolves(const char *host)
{
    struct addrinfo hints = { .ai_family = AF_UNSPEC, .ai_socktype = SOCK_STREAM }, *ai = NULL;
    int rc = getaddrinfo(host, NULL, &hints, &ai);
    if (ai) freeaddrinfo(ai);
    return rc == 0;
}

/* Returns true when a DHCP client has published a lease directory — that is
 * the only honest way to tell "DHCP worked" from "a static address exists". */
static bool dhcp_lease_found(const mica_net_iface *i, char *buf, size_t buflen)
{
    const char *cands[] = { "/run/systemd/netif/leases", "/var/lib/dhcp", "/var/lib/dhcpcd" };
    for (size_t k = 0; k < ML_ARRAY_SIZE(cands); k++)
        if (ml_is_dir(cands[k])) {
            snprintf(buf, buflen, "lease directory %s present", cands[k]);
            return true;
        }
    snprintf(buf, buflen, "%s has an address but no lease directory (DHCP vs static unknown)", i->name);
    return false;
}

int main(int argc, char **argv)
{
    if (diag_flag(argc, argv, "--help")) {
        fprintf(stderr, "usage: %s [--up IFACE] [--down IFACE] [--host NAME]\n"
                        "exit: 0 pass, 1 fail, 2 not tested, 3 unsupported, 4 usage\n", argv[0]);
        return HW_EXIT_USAGE;
    }
    const char *up = diag_opt(argc, argv, "--up");
    const char *down = diag_opt(argc, argv, "--down");
    const char *host = diag_opt(argc, argv, "--host");
    if (!host) host = "one.one.one.one";

    if (up) {
        printf("%s: %s\n", up, set_link(up, true) ? "up" : "FAILED (need CAP_NET_ADMIN)");
        return set_link(up, true) ? HW_EXIT_PASS : HW_EXIT_FAIL;
    }
    if (down) {
        printf("%s: %s\n", down, set_link(down, false) ? "down" : "FAILED (need CAP_NET_ADMIN)");
        return set_link(down, false) ? HW_EXIT_PASS : HW_EXIT_FAIL;
    }

    mica_net_state n;
    mica_net_probe(&n);

    diag_title("MacLiteOS Network");
    hw_report rep;
    hw_report_init(&rep);
    /* Under fixture roots the interface list, addresses and resolvers are the
     * fixture's, while any socket probe would talk to the *sandbox*. Checks
     * that need a live network are therefore NOT TESTED here — a PASS from
     * fixture mode would be a claim about a machine that was never touched. */
    const bool fixture = hw_using_fixture();

    diag_section("Interface");
    if (!n.n) {
        printf("  (none)\n");
        hw_report_add(&rep, "Connectivity", "Interfaces", true, HW_UNSUPPORTED,
                      "%s/class/net has no entries", hw_sysfs_root());
    }
    for (int i = 0; i < n.n; i++) {
        const mica_net_iface *f = &n.v[i];
        printf("  Device:   %-8s %s %s\n", f->name, f->kind,
               f->driver[0] ? f->driver : "(driver unknown)");
        printf("  Link:     %s (operstate %s, carrier %s)\n", f->up ? "UP" : "DOWN",
               or_dash(f->state), diag_yn(f->carrier));
        printf("  IP:       %s%s%s\n", f->has_ip4 ? f->ip4 : "-",
               f->has_ip6 ? "  " : "", f->has_ip6 ? f->ip6 : "");
        printf("  MAC:      %s\n", or_dash(f->mac));
        if (f->wireless) printf("  Wireless: level %d %s\n", f->wifi_level, f->ssid);
        printf("  Traffic:  rx %llu B  tx %llu B\n",
               (unsigned long long)f->rx_bytes, (unsigned long long)f->tx_bytes);
        putchar('\n');
    }
    printf("  Gateway:  %s\n", n.have_gateway ? n.gateway : "-");
    printf("  DNS:      ");
    for (int i = 0; i < n.ndns; i++) printf("%s%s", n.dns[i], i + 1 < n.ndns ? ", " : "");
    if (!n.ndns) printf("-");
    printf("   (%s)\n\n", n.resolv_source);

    int pi = n.primary;
    const mica_net_iface *pri = pi >= 0 ? &n.v[pi] : NULL;

    diag_section("Connectivity");
    hw_result link = HW_UNSUPPORTED;
    if (pri) {
        /* The rule lives in hardware/hwprobe.c so that maclite-hardware and
         * this tool can never disagree: link layer comes from sysfs and is
         * assertable everywhere, an address needs a live socket and is its own
         * row — NOT TESTED under fixtures rather than borrowed from the host. */
        ml_link_status l = ml_net_link_status(&n);
        link = l.link_up ? HW_PASS : HW_FAIL;
        hw_report_add(&rep, "Connectivity", "Link", true, link, "%s", l.evidence);
        hw_report_add(&rep, "Connectivity", "Address", true,
                      l.address_readable ? (l.has_ip4 ? HW_PASS : HW_FAIL) : HW_NOT_TESTED,
                      "%s", !l.address_readable ? "fixture mode: an address cannot be modelled, ioctl not performed"
                                                : (l.has_ip4 ? l.ip4 : "no IPv4 address on the primary interface"));
        char ev[192];
        bool lease = dhcp_lease_found(pri, ev, sizeof ev);
        bool link_local = pri->has_ip4 && strncmp(pri->ip4, "169.254.", 8) == 0;
        hw_report_add(&rep, "Connectivity", "DHCP", true,
                      fixture ? HW_NOT_TESTED : (link_local ? HW_FAIL : (lease ? HW_PASS : HW_PARTIAL)),
                      "%s%s", fixture ? "fixture mode: lease state of this host is not the modelled machine. " : "",
                      fixture ? "read /run/systemd/netif/leases on the real machine"
                              : (link_local ? "link-local 169.254.x address: no lease was obtained. " : ev));
        hw_report_add(&rep, "Connectivity", "Gateway", false,
                      n.have_gateway ? HW_PASS : HW_FAIL, "%s", n.have_gateway ? n.gateway : "no default route");
    } else {
        hw_report_add(&rep, "Connectivity", "Link", true,
                      n.neth || n.nwifi ? HW_FAIL : HW_UNSUPPORTED,
                      "%s", ml_net_link_status(&n).evidence);
        hw_report_add(&rep, "Connectivity", "DHCP", true, HW_NOT_TESTED, "no usable interface");
    }

    bool dns_ok = false;
    bool inet_ok = false;
    if (fixture) {
        hw_report_add(&rep, "Connectivity", "DNS", true, HW_NOT_TESTED,
                      "%d nameserver(s) from %s; live lookup not run in fixture mode", n.ndns, n.resolv_source);
        hw_report_add(&rep, "Connectivity", "Internet", true, HW_NOT_TESTED,
                      "fixture mode: TCP probe would measure this sandbox, not the target machine");
    } else if (n.ndns) {
        dns_ok = tcp_reachable(n.dns[0], 53, 2500) && name_resolves(host);
        hw_report_add(&rep, "Connectivity", "DNS", true, dns_ok ? HW_PASS : HW_FAIL,
                      "%s reachable on 53 and %s resolved", n.dns[0], host);
    } else {
        hw_report_add(&rep, "Connectivity", "DNS", true, HW_FAIL, "no nameserver in %s", n.resolv_source);
    }
    if (!fixture) {
        inet_ok = n.ndns ? tcp_reachable(n.dns[0], 53, 2500) : false;
        hw_report_add(&rep, "Connectivity", "Internet", true, inet_ok ? HW_PASS : HW_FAIL,
                      "%s", inet_ok ? "TCP connect to the resolver succeeded" : "TCP connect to the resolver failed");
    }

    bool v6 = false;
    for (int i = 0; i < n.n; i++) if (n.v[i].has_ip6) v6 = true;
    hw_report_add(&rep, "Connectivity", "IPv6", false, v6 ? HW_PASS : HW_UNSUPPORTED,
                  v6 ? "a global IPv6 address is present" : "no global IPv6 address");

    hw_report_add(&rep, "Wireless", "Wi-Fi", false, n.nwifi ? HW_PASS : HW_UNSUPPORTED,
                  "%d wireless interface(s) %s", n.nwifi, fixture ? "in the fixture" : "on this host");
    hw_report_add(&rep, "Wired", "Ethernet", false, n.neth ? HW_PASS : HW_UNSUPPORTED,
                  "%d wired interface(s) %s", n.neth, fixture ? "in the fixture" : "on this host");

    for (int i = 0; i < rep.n; i++)
        printf("  %-14s %-10s %s\n", rep.v[i].name, hw_result_str(rep.v[i].result), rep.v[i].evidence);
    printf("\nOverall: %s\n", hw_result_str(hw_report_overall(&rep)));
    if (fixture)
        printf("\nNote: fixture mode — interface list and resolver come from the fixture tree,\n"
               "      link/DNS/Internet checks are NOT TESTED. Run this on the iMac and\n"
               "      record the numbers in docs/testing.md.\n");
    else if (!inet_ok || !dns_ok)
        printf("\nNote: DNS/Internet are measured from the machine running this tool. On a\n"
               "      MacLiteOS boot that is the iMac itself; record results in docs/testing.md.\n");
    return hw_report_exit(&rep);
}
