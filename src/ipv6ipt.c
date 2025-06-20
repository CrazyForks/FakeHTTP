/*
 * ipv6ipt.c - FakeHTTP: https://github.com/MikeWang000000/FakeHTTP
 *
 * Copyright (C) 2025  MikeWang000000
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#define _GNU_SOURCE
#include "ipv6ipt.h"

#include <inttypes.h>
#include <stdlib.h>
#include <net/if.h>

#include "globvar.h"
#include "logging.h"
#include "process.h"

static int ipt6_iface_setup(void)
{
    char iface_str[IFNAMSIZ];
    size_t i, cnt;
    int res;
    char *ipt_alliface_cmd[] = {"ip6tables", "-w",         "-t",
                                "mangle",    "-A",         "FAKEHTTP",
                                "-j",        "FAKEHTTP_R", NULL};

    char *ipt_iface_cmd[] = {"ip6tables", "-w",         "-t", "mangle",
                             "-A",        "FAKEHTTP",   "-i", iface_str,
                             "-j",        "FAKEHTTP_R", NULL};

    if (g_ctx.alliface) {
        res = fh_execute_command(ipt_alliface_cmd, 0, NULL);
        if (res < 0) {
            E(T(fh_execute_command));
            return -1;
        }
        return 0;
    }

    cnt = sizeof(g_ctx.iface) / sizeof(*g_ctx.iface);

    for (i = 0; i < cnt && g_ctx.iface[i]; i++) {
        res = snprintf(iface_str, sizeof(iface_str), "%s", g_ctx.iface[i]);
        if (res < 0 || (size_t) res >= sizeof(iface_str)) {
            E("ERROR: snprintf(): %s", "failure");
            return -1;
        }

        res = fh_execute_command(ipt_iface_cmd, 0, NULL);
        if (res < 0) {
            E(T(fh_execute_command));
            return -1;
        }
    }
    return 0;
}


int fh_ipt6_setup(void)
{
    char xmark_str[64], nfqnum_str[32];
    size_t i, ipt_cmds_cnt, ipt_opt_cmds_cnt;
    int res;
    char *ipt_cmds[][32] = {
        {"ip6tables", "-w", "-t", "mangle", "-N", "FAKEHTTP", NULL},

        {"ip6tables", "-w", "-t", "mangle", "-I", "PREROUTING", "-j",
         "FAKEHTTP", NULL},

        {"ip6tables", "-w", "-t", "mangle", "-N", "FAKEHTTP_R", NULL},

        /*
            exclude marked packets
        */
        {"ip6tables", "-w", "-t", "mangle", "-A", "FAKEHTTP_R", "-m", "mark",
         "--mark", xmark_str, "-j", "CONNMARK", "--set-xmark", xmark_str,
         NULL},

        {"ip6tables", "-w", "-t", "mangle", "-A", "FAKEHTTP_R", "-m",
         "connmark", "--mark", xmark_str, "-j", "MARK", "--set-xmark",
         xmark_str, NULL},

        {"ip6tables", "-w", "-t", "mangle", "-A", "FAKEHTTP_R", "-m", "mark",
         "--mark", xmark_str, "-j", "RETURN", NULL},

        /*
            exclude special IPv6 addresses
        */
        {"ip6tables", "-w", "-t", "mangle", "-A", "FAKEHTTP_R", "-s", "::/127",
         "-j", "RETURN", NULL},

        {"ip6tables", "-w", "-t", "mangle", "-A", "FAKEHTTP_R", "-s",
         "::ffff:0:0/96", "-j", "RETURN", NULL},

        {"ip6tables", "-w", "-t", "mangle", "-A", "FAKEHTTP_R", "-s",
         "64:ff9b::/96", "-j", "RETURN", NULL},

        {"ip6tables", "-w", "-t", "mangle", "-A", "FAKEHTTP_R", "-s",
         "64:ff9b:1::/48", "-j", "RETURN", NULL},

        {"ip6tables", "-w", "-t", "mangle", "-A", "FAKEHTTP_R", "-s",
         "2002::/16", "-j", "RETURN", NULL},

        {"ip6tables", "-w", "-t", "mangle", "-A", "FAKEHTTP_R", "-s",
         "fc00::/7", "-j", "RETURN", NULL},

        {"ip6tables", "-w", "-t", "mangle", "-A", "FAKEHTTP_R", "-s",
         "fe80::/10", "-j", "RETURN", NULL},

        /*
            send to nfqueue
        */
        {"ip6tables", "-w", "-t", "mangle", "-A", "FAKEHTTP_R", "-p", "tcp",
         "--tcp-flags", "ACK,FIN,RST", "ACK", "-j", "NFQUEUE",
         "--queue-bypass", "--queue-num", nfqnum_str, NULL}};

    char *ipt_opt_cmds[][32] = {
        /*
            exclude packets from connections with more than 32 packets
        */
        {"ip6tables", "-w", "-t", "mangle", "-I", "FAKEHTTP_R", "-m",
         "connbytes", "!", "--connbytes", "0:32", "--connbytes-dir", "both",
         "--connbytes-mode", "packets", "-j", "RETURN", NULL},

        /*
            exclude big packets
        */
        {"ip6tables", "-w", "-t", "mangle", "-I", "FAKEHTTP_R", "-m", "length",
         "!", "--length", "0:120", "-j", "RETURN", NULL}};

    ipt_cmds_cnt = sizeof(ipt_cmds) / sizeof(*ipt_cmds);
    ipt_opt_cmds_cnt = sizeof(ipt_opt_cmds) / sizeof(*ipt_opt_cmds);

    res = snprintf(xmark_str, sizeof(xmark_str), "%" PRIu32 "/%" PRIu32,
                   g_ctx.fwmark, g_ctx.fwmask);
    if (res < 0 || (size_t) res >= sizeof(xmark_str)) {
        E("ERROR: snprintf(): %s", "failure");
        return -1;
    }

    res = snprintf(nfqnum_str, sizeof(nfqnum_str), "%" PRIu32, g_ctx.nfqnum);
    if (res < 0 || (size_t) res >= sizeof(nfqnum_str)) {
        E("ERROR: snprintf(): %s", "failure");
        return -1;
    }

    fh_ipt6_cleanup();

    for (i = 0; i < ipt_cmds_cnt; i++) {
        res = fh_execute_command(ipt_cmds[i], 0, NULL);
        if (res < 0) {
            E(T(fh_execute_command));
            return -1;
        }
    }

    for (i = 0; i < ipt_opt_cmds_cnt; i++) {
        fh_execute_command(ipt_opt_cmds[i], 1, NULL);
    }

    res = ipt6_iface_setup();
    if (res < 0) {
        E(T(ipt6_iface_setup));
        return -1;
    }

    return 0;
}


void fh_ipt6_cleanup(void)
{
    size_t i, cnt;
    char *ipt_cmds[][32] = {
        {"ip6tables", "-w", "-t", "mangle", "-F", "FAKEHTTP_R", NULL},

        {"ip6tables", "-w", "-t", "mangle", "-F", "FAKEHTTP", NULL},

        {"ip6tables", "-w", "-t", "mangle", "-D", "PREROUTING", "-j",
         "FAKEHTTP", NULL},

        {"ip6tables", "-w", "-t", "mangle", "-D", "INPUT", "-j", "FAKEHTTP",
         NULL},

        {"ip6tables", "-w", "-t", "mangle", "-D", "FORWARD", "-j", "FAKEHTTP",
         NULL},

        {"ip6tables", "-w", "-t", "mangle", "-D", "OUTPUT", "-j", "FAKEHTTP",
         NULL},

        {"ip6tables", "-w", "-t", "mangle", "-D", "POSTROUTING", "-j",
         "FAKEHTTP", NULL},

        {"ip6tables", "-w", "-t", "mangle", "-X", "FAKEHTTP_R", NULL},

        {"ip6tables", "-w", "-t", "mangle", "-X", "FAKEHTTP", NULL}};

    cnt = sizeof(ipt_cmds) / sizeof(*ipt_cmds);
    for (i = 0; i < cnt; i++) {
        fh_execute_command(ipt_cmds[i], 1, NULL);
    }
}
