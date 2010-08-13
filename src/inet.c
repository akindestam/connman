/*
 *
 *  Connection Manager
 *
 *  Copyright (C) 2007-2010  Intel Corporation. All rights reserved.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 2 as
 *  published by the Free Software Foundation.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 *
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <stdio.h>
#include <errno.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <net/route.h>
#include <net/ethernet.h>
#include <linux/if_arp.h>
#include <linux/wireless.h>

#include "connman.h"

int connman_inet_ifindex(const char *name)
{
	struct ifreq ifr;
	int sk, err;

	if (name == NULL)
		return -1;

	sk = socket(PF_INET, SOCK_DGRAM, 0);
	if (sk < 0)
		return -1;

	memset(&ifr, 0, sizeof(ifr));
	strncpy(ifr.ifr_name, name, sizeof(ifr.ifr_name));

	err = ioctl(sk, SIOCGIFINDEX, &ifr);

	close(sk);

	if (err < 0)
		return -1;

	return ifr.ifr_ifindex;
}

char *connman_inet_ifname(int index)
{
	struct ifreq ifr;
	int sk, err;

	if (index < 0)
		return NULL;

	sk = socket(PF_INET, SOCK_DGRAM, 0);
	if (sk < 0)
		return NULL;

	memset(&ifr, 0, sizeof(ifr));
	ifr.ifr_ifindex = index;

	err = ioctl(sk, SIOCGIFNAME, &ifr);

	close(sk);

	if (err < 0)
		return NULL;

	return strdup(ifr.ifr_name);
}

short int connman_inet_ifflags(int index)
{
	struct ifreq ifr;
	int sk, err;

	sk = socket(PF_INET, SOCK_DGRAM, 0);
	if (sk < 0)
		return -errno;

	memset(&ifr, 0, sizeof(ifr));
	ifr.ifr_ifindex = index;

	if (ioctl(sk, SIOCGIFNAME, &ifr) < 0) {
		err = -errno;
		goto done;
	}

	if (ioctl(sk, SIOCGIFFLAGS, &ifr) < 0) {
		err = -errno;
		goto done;
	}

	err = ifr.ifr_flags;

done:
	close(sk);

	return err;
}

int connman_inet_ifup(int index)
{
	struct ifreq ifr;
	int sk, err;

	sk = socket(PF_INET, SOCK_DGRAM, 0);
	if (sk < 0)
		return -errno;

	memset(&ifr, 0, sizeof(ifr));
	ifr.ifr_ifindex = index;

	if (ioctl(sk, SIOCGIFNAME, &ifr) < 0) {
		err = -errno;
		goto done;
	}

	if (ioctl(sk, SIOCGIFFLAGS, &ifr) < 0) {
		err = -errno;
		goto done;
	}

	if (ifr.ifr_flags & IFF_UP) {
		err = -EALREADY;
		goto done;
	}

	ifr.ifr_flags |= IFF_UP;

	if (ioctl(sk, SIOCSIFFLAGS, &ifr) < 0) {
		err = -errno;
		goto done;
	}

	err = 0;

done:
	close(sk);

	return err;
}

int connman_inet_ifdown(int index)
{
	struct ifreq ifr;
	int sk, err;

	sk = socket(PF_INET, SOCK_DGRAM, 0);
	if (sk < 0)
		return -errno;

	memset(&ifr, 0, sizeof(ifr));
	ifr.ifr_ifindex = index;

	if (ioctl(sk, SIOCGIFNAME, &ifr) < 0) {
		err = -errno;
		goto done;
	}

	if (ioctl(sk, SIOCGIFFLAGS, &ifr) < 0) {
		err = -errno;
		goto done;
	}

	if (!(ifr.ifr_flags & IFF_UP)) {
		err = -EALREADY;
		goto done;
	}

	ifr.ifr_flags &= ~IFF_UP;

	if (ioctl(sk, SIOCSIFFLAGS, &ifr) < 0)
		err = -errno;
	else
		err = 0;

done:
	close(sk);

	return err;
}

static unsigned short index2type(int index)
{
	struct ifreq ifr;
	int sk, err;

	if (index < 0)
		return ARPHRD_VOID;

	sk = socket(PF_INET, SOCK_DGRAM, 0);
	if (sk < 0)
		return ARPHRD_VOID;

	memset(&ifr, 0, sizeof(ifr));
	ifr.ifr_ifindex = index;

	err = ioctl(sk, SIOCGIFNAME, &ifr);

	if (err == 0)
		err = ioctl(sk, SIOCGIFHWADDR, &ifr);

	close(sk);

	if (err < 0)
		return ARPHRD_VOID;

	return ifr.ifr_hwaddr.sa_family;
}

static char *index2addr(int index)
{
	struct ifreq ifr;
	struct ether_addr eth;
	char *str;
	int sk, err;

	if (index < 0)
		return NULL;

	sk = socket(PF_INET, SOCK_DGRAM, 0);
	if (sk < 0)
		return NULL;

	memset(&ifr, 0, sizeof(ifr));
	ifr.ifr_ifindex = index;

	err = ioctl(sk, SIOCGIFNAME, &ifr);

	if (err == 0)
		err = ioctl(sk, SIOCGIFHWADDR, &ifr);

	close(sk);

	if (err < 0)
		return NULL;

	str = malloc(18);
	if (!str)
		return NULL;

	memcpy(&eth, &ifr.ifr_hwaddr.sa_data, sizeof(eth));
	snprintf(str, 18, "%02X:%02X:%02X:%02X:%02X:%02X",
						eth.ether_addr_octet[0],
						eth.ether_addr_octet[1],
						eth.ether_addr_octet[2],
						eth.ether_addr_octet[3],
						eth.ether_addr_octet[4],
						eth.ether_addr_octet[5]);

	return str;
}

static char *index2ident(int index, const char *prefix)
{
	struct ifreq ifr;
	struct ether_addr eth;
	char *str;
	int sk, err, len;

	if (index < 0)
		return NULL;

	sk = socket(PF_INET, SOCK_DGRAM, 0);
	if (sk < 0)
		return NULL;

	memset(&ifr, 0, sizeof(ifr));
	ifr.ifr_ifindex = index;

	err = ioctl(sk, SIOCGIFNAME, &ifr);

	if (err == 0)
		err = ioctl(sk, SIOCGIFHWADDR, &ifr);

	close(sk);

	if (err < 0)
		return NULL;

	len = prefix ? strlen(prefix) + 18 : 18;

	str = malloc(len);
	if (!str)
		return NULL;

	memcpy(&eth, &ifr.ifr_hwaddr.sa_data, sizeof(eth));
	snprintf(str, len, "%s%02x%02x%02x%02x%02x%02x",
						prefix ? prefix : "",
						eth.ether_addr_octet[0],
						eth.ether_addr_octet[1],
						eth.ether_addr_octet[2],
						eth.ether_addr_octet[3],
						eth.ether_addr_octet[4],
						eth.ether_addr_octet[5]);

	return str;
}

connman_bool_t connman_inet_is_cfg80211(int index)
{
	connman_bool_t result = FALSE;
	char phy80211_path[PATH_MAX];
	struct stat st;
	struct ifreq ifr;
	int sk;

	sk = socket(PF_INET, SOCK_DGRAM, 0);
	if (sk < 0)
		return FALSE;

	memset(&ifr, 0, sizeof(ifr));
	ifr.ifr_ifindex = index;

	if (ioctl(sk, SIOCGIFNAME, &ifr) < 0)
		goto done;

	snprintf(phy80211_path, PATH_MAX,
				"/sys/class/net/%s/phy80211", ifr.ifr_name);

	if (stat(phy80211_path, &st) == 0 && (st.st_mode & S_IFDIR))
		result = TRUE;

done:
	close(sk);

	return result;
}

enum connman_device_type __connman_inet_get_device_type(int index)
{
	enum connman_device_type devtype = CONNMAN_DEVICE_TYPE_UNKNOWN;
	unsigned short type = index2type(index);
	const char *devname;
	struct ifreq ifr;
	int sk;

	sk = socket(PF_INET, SOCK_DGRAM, 0);
	if (sk < 0)
		return devtype;

	memset(&ifr, 0, sizeof(ifr));
	ifr.ifr_ifindex = index;

	if (ioctl(sk, SIOCGIFNAME, &ifr) < 0)
		goto done;

	devname = ifr.ifr_name;

	if (type == ARPHRD_ETHER) {
		char phy80211_path[PATH_MAX];
		char bonding_path[PATH_MAX];
		char bridge_path[PATH_MAX];
		char wimax_path[PATH_MAX];
		struct stat st;
		struct iwreq iwr;

		snprintf(phy80211_path, PATH_MAX,
					"/sys/class/net/%s/phy80211", devname);
		snprintf(bonding_path, PATH_MAX,
					"/sys/class/net/%s/bonding", devname);
		snprintf(bridge_path, PATH_MAX,
					"/sys/class/net/%s/bridge", devname);
		snprintf(wimax_path, PATH_MAX,
					"/sys/class/net/%s/wimax", devname);

		memset(&iwr, 0, sizeof(iwr));
		strncpy(iwr.ifr_ifrn.ifrn_name, devname, IFNAMSIZ);

		if (g_str_has_prefix(devname, "vmnet") == TRUE)
			devtype = CONNMAN_DEVICE_TYPE_UNKNOWN;
		else if (g_str_has_prefix(ifr.ifr_name, "vboxnet") == TRUE)
			devtype = CONNMAN_DEVICE_TYPE_UNKNOWN;
		else if (g_str_has_prefix(devname, "bnep") == TRUE)
			devtype = CONNMAN_DEVICE_TYPE_UNKNOWN;
		else if (g_str_has_prefix(devname, "wmx") == TRUE)
			devtype = CONNMAN_DEVICE_TYPE_WIMAX;
		else if (stat(wimax_path, &st) == 0 && (st.st_mode & S_IFDIR))
			devtype = CONNMAN_DEVICE_TYPE_WIMAX;
		else if (stat(bridge_path, &st) == 0 && (st.st_mode & S_IFDIR))
			devtype = CONNMAN_DEVICE_TYPE_UNKNOWN;
		else if (stat(bonding_path, &st) == 0 && (st.st_mode & S_IFDIR))
			devtype = CONNMAN_DEVICE_TYPE_UNKNOWN;
		else if (stat(phy80211_path, &st) == 0 && (st.st_mode & S_IFDIR))
			devtype = CONNMAN_DEVICE_TYPE_WIFI;
		else if (ioctl(sk, SIOCGIWNAME, &iwr) == 0)
			devtype = CONNMAN_DEVICE_TYPE_WIFI;
		else
			devtype = CONNMAN_DEVICE_TYPE_ETHERNET;
	}

done:
	close(sk);

	return devtype;
}

struct connman_device *connman_inet_create_device(int index)
{
	enum connman_device_mode mode = CONNMAN_DEVICE_MODE_UNKNOWN;
	enum connman_device_type type;
	struct connman_device *device;
	char *devname, *ident = NULL;
	char *addr = NULL, *name = NULL, *node = NULL;

	if (index < 0)
		return NULL;

	devname = connman_inet_ifname(index);
	if (devname == NULL)
		return NULL;

	if (__connman_element_device_isfiltered(devname) == TRUE) {
		connman_info("Ignoring interface %s (filtered)", devname);
		return NULL;
	}

	__connman_udev_get_devtype(devname);

	type = __connman_inet_get_device_type(index);

	switch (type) {
	case CONNMAN_DEVICE_TYPE_UNKNOWN:
		connman_info("Ignoring interface %s (type unknown)", devname);
		g_free(devname);
		return NULL;
	case CONNMAN_DEVICE_TYPE_ETHERNET:
	case CONNMAN_DEVICE_TYPE_WIFI:
	case CONNMAN_DEVICE_TYPE_WIMAX:
		name = index2ident(index, "");
		addr = index2addr(index);
		break;
	case CONNMAN_DEVICE_TYPE_BLUETOOTH:
	case CONNMAN_DEVICE_TYPE_CELLULAR:
	case CONNMAN_DEVICE_TYPE_GPS:
	case CONNMAN_DEVICE_TYPE_VENDOR:
		name = strdup(devname);
		break;
	}

	device = connman_device_create(name, type);
	if (device == NULL)
		goto done;

	switch (type) {
	case CONNMAN_DEVICE_TYPE_UNKNOWN:
	case CONNMAN_DEVICE_TYPE_VENDOR:
	case CONNMAN_DEVICE_TYPE_GPS:
		mode = CONNMAN_DEVICE_MODE_UNKNOWN;
		break;
	case CONNMAN_DEVICE_TYPE_ETHERNET:
		mode = CONNMAN_DEVICE_MODE_NETWORK_SINGLE;
		ident = index2ident(index, NULL);
		break;
	case CONNMAN_DEVICE_TYPE_WIFI:
	case CONNMAN_DEVICE_TYPE_WIMAX:
		mode = CONNMAN_DEVICE_MODE_NETWORK_SINGLE;
		ident = index2ident(index, NULL);
		break;
	case CONNMAN_DEVICE_TYPE_BLUETOOTH:
		mode = CONNMAN_DEVICE_MODE_NETWORK_MULTIPLE;
		break;
	case CONNMAN_DEVICE_TYPE_CELLULAR:
		mode = CONNMAN_DEVICE_MODE_NETWORK_SINGLE;
		ident = index2ident(index, NULL);
		break;
	}

	connman_device_set_mode(device, mode);

	connman_device_set_index(device, index);
	connman_device_set_interface(device, devname, node);

	if (ident != NULL) {
		connman_device_set_ident(device, ident);
		free(ident);
	}

	connman_device_set_string(device, "Address", addr);

done:
	g_free(devname);
	g_free(node);
	free(name);
	free(addr);

	return device;
}

struct in6_ifreq {
	struct in6_addr ifr6_addr;
	__u32 ifr6_prefixlen;
	unsigned int ifr6_ifindex;
};

int connman_inet_set_ipv6_address(int index,
		struct connman_ipaddress *ipaddress)
{
	int sk, err;
	struct in6_ifreq ifr6;

	DBG("index %d ipaddress->local %s", index, ipaddress->local);

	if (ipaddress->local == NULL)
		return 0;

	sk = socket(PF_INET6, SOCK_DGRAM, 0);
	if (sk < 0) {
		err = -1;
		goto out;
	}

	memset(&ifr6, 0, sizeof(ifr6));

	err = inet_pton(AF_INET6, ipaddress->local, &ifr6.ifr6_addr);
	if (err < 0)
		goto out;

	ifr6.ifr6_ifindex = index;
	ifr6.ifr6_prefixlen = ipaddress->prefixlen;

	err = ioctl(sk, SIOCSIFADDR, &ifr6);
	close(sk);
out:
	if (err < 0)
		connman_error("Set IPv6 address error");

	return err;
}

int connman_inet_set_address(int index, struct connman_ipaddress *ipaddress)
{
	struct ifreq ifr;
	struct sockaddr_in addr;
	int sk, err;

	sk = socket(PF_INET, SOCK_DGRAM, 0);
	if (sk < 0)
		return -1;

	memset(&ifr, 0, sizeof(ifr));
	ifr.ifr_ifindex = index;

	if (ioctl(sk, SIOCGIFNAME, &ifr) < 0) {
		close(sk);
		return -1;
	}

	DBG("ifname %s", ifr.ifr_name);

	if (ipaddress->local == NULL) {
		close(sk);
		return -1;
	}

	memset(&addr, 0, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_addr.s_addr = inet_addr(ipaddress->local);
	memcpy(&ifr.ifr_addr, &addr, sizeof(ifr.ifr_addr));

	err = ioctl(sk, SIOCSIFADDR, &ifr);

	if (err < 0)
		DBG("address setting failed (%s)", strerror(errno));

	memset(&addr, 0, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_addr.s_addr = htonl(~(0xfffffffflu >> ipaddress->prefixlen));
	memcpy(&ifr.ifr_netmask, &addr, sizeof(ifr.ifr_netmask));

	err = ioctl(sk, SIOCSIFNETMASK, &ifr);

	if (err < 0)
		DBG("netmask setting failed (%s)", strerror(errno));

	memset(&addr, 0, sizeof(addr));
	addr.sin_family = AF_INET;

	if (ipaddress->broadcast != NULL)
		addr.sin_addr.s_addr = inet_addr(ipaddress->broadcast);
	else
		addr.sin_addr.s_addr = inet_addr(ipaddress->local) |
				htonl(0xfffffffflu >> ipaddress->prefixlen);

	memcpy(&ifr.ifr_broadaddr, &addr, sizeof(ifr.ifr_broadaddr));

	err = ioctl(sk, SIOCSIFBRDADDR, &ifr);

	if (err < 0)
		DBG("broadcast setting failed (%s)", strerror(errno));

	close(sk);

	return 0;
}

int connman_inet_clear_ipv6_address(int index, const char *address,
							int prefix_len)
{
	struct in6_ifreq ifr6;
	int sk, err;

	DBG("index %d address %s prefix_len %d", index, address, prefix_len);

	memset(&ifr6, 0, sizeof(ifr6));

	err = inet_pton(AF_INET6, address, &ifr6.ifr6_addr);
	if (err < 0)
		goto out;

	ifr6.ifr6_ifindex = index;
	ifr6.ifr6_prefixlen = prefix_len;

	sk = socket(PF_INET6, SOCK_DGRAM, 0);
	if (sk < 0) {
		err = -1;
		goto out;
	}

	err = ioctl(sk, SIOCDIFADDR, &ifr6);
	close(sk);
out:
	if (err < 0)
		connman_error("Clear IPv6 address error");

	return err;
}

int connman_inet_clear_address(int index)
{


	struct ifreq ifr;
	struct sockaddr_in addr;
	int sk, err;

	sk = socket(PF_INET, SOCK_DGRAM, 0);
	if (sk < 0)
		return -1;

	memset(&ifr, 0, sizeof(ifr));
	ifr.ifr_ifindex = index;

	if (ioctl(sk, SIOCGIFNAME, &ifr) < 0) {
		close(sk);
		return -1;
	}

	DBG("ifname %s", ifr.ifr_name);

	memset(&addr, 0, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_addr.s_addr = INADDR_ANY;
	memcpy(&ifr.ifr_addr, &addr, sizeof(ifr.ifr_addr));

	//err = ioctl(sk, SIOCDIFADDR, &ifr);
	err = ioctl(sk, SIOCSIFADDR, &ifr);

	close(sk);

	if (err < 0 && errno != EADDRNOTAVAIL) {
		DBG("address removal failed (%s)", strerror(errno));
		return -1;
	}

	return 0;
}

int connman_inet_add_host_route(int index, const char *host, const char *gateway)
{
	struct ifreq ifr;
	struct rtentry rt;
	struct sockaddr_in addr;
	int sk, err;

	sk = socket(PF_INET, SOCK_DGRAM, 0);
	if (sk < 0)
		return -1;

	memset(&ifr, 0, sizeof(ifr));
	ifr.ifr_ifindex = index;

	if (ioctl(sk, SIOCGIFNAME, &ifr) < 0) {
		close(sk);
		return -1;
	}

	DBG("ifname %s", ifr.ifr_name);

	memset(&rt, 0, sizeof(rt));
	rt.rt_flags = RTF_UP | RTF_HOST;
	if (gateway != NULL)
		rt.rt_flags |= RTF_GATEWAY;

	memset(&addr, 0, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_addr.s_addr = inet_addr(host);
	memcpy(&rt.rt_dst, &addr, sizeof(rt.rt_dst));

	memset(&addr, 0, sizeof(addr));
	addr.sin_family = AF_INET;
	if (gateway != NULL)
		addr.sin_addr.s_addr = inet_addr(gateway);
	else
		addr.sin_addr.s_addr = INADDR_ANY;
	memcpy(&rt.rt_gateway, &addr, sizeof(rt.rt_gateway));

	memset(&addr, 0, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_addr.s_addr = INADDR_ANY;
	memcpy(&rt.rt_genmask, &addr, sizeof(rt.rt_genmask));

	rt.rt_dev = ifr.ifr_name;

	err = ioctl(sk, SIOCADDRT, &rt);
	if (err < 0)
		connman_error("Adding host route failed (%s)",
							strerror(errno));

	close(sk);

	return err;
}

int connman_inet_del_host_route(int index, const char *host)
{
	struct ifreq ifr;
	struct rtentry rt;
	struct sockaddr_in addr;
	int sk, err;

	sk = socket(PF_INET, SOCK_DGRAM, 0);
	if (sk < 0)
		return -1;

	memset(&ifr, 0, sizeof(ifr));
	ifr.ifr_ifindex = index;

	if (ioctl(sk, SIOCGIFNAME, &ifr) < 0) {
		close(sk);
		return -1;
	}

	DBG("ifname %s", ifr.ifr_name);

	memset(&rt, 0, sizeof(rt));
	rt.rt_flags = RTF_UP | RTF_HOST;

	memset(&addr, 0, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_addr.s_addr = inet_addr(host);
	memcpy(&rt.rt_dst, &addr, sizeof(rt.rt_dst));

	rt.rt_dev = ifr.ifr_name;

	err = ioctl(sk, SIOCDELRT, &rt);
	if (err < 0)
		connman_error("Deleting host route failed (%s)",
							strerror(errno));

	close(sk);

	return err;
}

int connman_inet_del_ipv6_host_route(int index, const char *host)
{
	struct in6_rtmsg rt;
	int sk, err;

	DBG("index %d host %s", index, host);

	if (host == NULL)
		return -EINVAL;

	memset(&rt, 0, sizeof(rt));

	rt.rtmsg_dst_len = 128;

	err = inet_pton(AF_INET6, host, &rt.rtmsg_dst);
	if (err < 0)
		goto out;

	rt.rtmsg_flags = RTF_UP | RTF_HOST;

	rt.rtmsg_metric = 1;
	rt.rtmsg_ifindex = index;

	sk = socket(AF_INET6, SOCK_DGRAM, 0);
	if (sk < 0) {
		err = -1;
		goto out;
	}

	err = ioctl(sk, SIOCDELRT, &rt);
	close(sk);
out:
	if (err < 0)
		connman_error("Del IPv6 host route error");

	return err;
}

int connman_inet_add_ipv6_host_route(int index, const char *host,
						const char *gateway)
{
	struct in6_rtmsg rt;
	int sk, err;

	DBG("index %d host %s gateway %s", index, host, gateway);

	if (host == NULL)
		return -EINVAL;

	memset(&rt, 0, sizeof(rt));

	rt.rtmsg_dst_len = 128;

	err = inet_pton(AF_INET6, host, &rt.rtmsg_dst);
	if (err < 0)
		goto out;

	rt.rtmsg_flags = RTF_UP | RTF_HOST;

	if (gateway != NULL) {
		rt.rtmsg_flags |= RTF_GATEWAY;
		inet_pton(AF_INET6, gateway, &rt.rtmsg_gateway);
	}

	rt.rtmsg_metric = 1;
	rt.rtmsg_ifindex = index;

	sk = socket(AF_INET6, SOCK_DGRAM, 0);
	if (sk < 0) {
		err = -1;
		goto out;
	}

	err = ioctl(sk, SIOCADDRT, &rt);
	close(sk);
out:
	if (err < 0)
		connman_error("Set IPv6 host route error");

	return err;
}

int connman_inet_set_ipv6_gateway_address(int index, const char *gateway)
{
	struct in6_rtmsg rt;
	int sk, err;

	DBG("index %d, gateway %s", index, gateway);

	if (gateway == NULL)
		return -EINVAL;

	memset(&rt, 0, sizeof(rt));

	err = inet_pton(AF_INET6, gateway, &rt.rtmsg_gateway);
	if (err < 0)
		goto out;

	rt.rtmsg_flags = RTF_UP | RTF_GATEWAY;
	rt.rtmsg_metric = 1;
	rt.rtmsg_dst_len = 0;
	rt.rtmsg_ifindex = index;

	sk = socket(AF_INET6, SOCK_DGRAM, 0);
	if (sk < 0) {
		err = -1;
		goto out;
	}

	err = ioctl(sk, SIOCADDRT, &rt);
	close(sk);
out:
	if (err < 0)
		connman_error("Set default IPv6 gateway error");

	return err;
}

int connman_inet_clear_ipv6_gateway_address(int index, const char *gateway)
{
	struct in6_rtmsg rt;
	int sk, err;

	DBG("index %d, gateway %s", index, gateway);

	if (gateway == NULL)
		return -EINVAL;

	memset(&rt, 0, sizeof(rt));

	err = inet_pton(AF_INET6, gateway, &rt.rtmsg_gateway);
	if (err < 0)
		goto out;

	rt.rtmsg_flags = RTF_UP | RTF_GATEWAY;
	rt.rtmsg_metric = 1;
	rt.rtmsg_dst_len = 0;
	rt.rtmsg_ifindex = index;

	sk = socket(AF_INET6, SOCK_DGRAM, 0);
	if (sk < 0) {
		err = -1;
		goto out;
	}

	err = ioctl(sk, SIOCDELRT, &rt);
	close(sk);
out:
	if (err < 0)
		connman_error("Clear default IPv6 gateway error");

	return err;
}

int connman_inet_set_gateway_address(int index, const char *gateway)
{
	struct ifreq ifr;
	struct rtentry rt;
	struct sockaddr_in addr;
	int sk, err;

	sk = socket(PF_INET, SOCK_DGRAM, 0);
	if (sk < 0)
		return -1;

	memset(&ifr, 0, sizeof(ifr));
	ifr.ifr_ifindex = index;

	if (ioctl(sk, SIOCGIFNAME, &ifr) < 0) {
		close(sk);
		return -1;
	}

	DBG("ifname %s", ifr.ifr_name);

	memset(&rt, 0, sizeof(rt));
	rt.rt_flags = RTF_UP | RTF_GATEWAY;

	memset(&addr, 0, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_addr.s_addr = INADDR_ANY;
	memcpy(&rt.rt_dst, &addr, sizeof(rt.rt_dst));

	memset(&addr, 0, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_addr.s_addr = inet_addr(gateway);
	memcpy(&rt.rt_gateway, &addr, sizeof(rt.rt_gateway));

	memset(&addr, 0, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_addr.s_addr = INADDR_ANY;
	memcpy(&rt.rt_genmask, &addr, sizeof(rt.rt_genmask));

	err = ioctl(sk, SIOCADDRT, &rt);
	if (err < 0)
		connman_error("Setting default gateway route failed (%s)",
							strerror(errno));

	close(sk);

	return err;
}

int connman_inet_set_gateway_interface(int index)
{
	struct ifreq ifr;
	struct rtentry rt;
	struct sockaddr_in addr;
	int sk, err;

	DBG("");

	sk = socket(PF_INET, SOCK_DGRAM, 0);
	if (sk < 0)
		return -1;

	memset(&ifr, 0, sizeof(ifr));
	ifr.ifr_ifindex = index;

	if (ioctl(sk, SIOCGIFNAME, &ifr) < 0) {
		close(sk);
		return -1;
	}

	DBG("ifname %s", ifr.ifr_name);

	memset(&rt, 0, sizeof(rt));
	rt.rt_flags = RTF_UP;

	memset(&addr, 0, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_addr.s_addr = INADDR_ANY;

	memcpy(&rt.rt_genmask, &addr, sizeof(rt.rt_genmask));
	memcpy(&rt.rt_dst, &addr, sizeof(rt.rt_dst));
	memcpy(&rt.rt_gateway, &addr, sizeof(rt.rt_gateway));

	rt.rt_dev = ifr.ifr_name;

	err = ioctl(sk, SIOCADDRT, &rt);
	if (err < 0)
		connman_error("Setting default interface route failed (%s)",
							strerror(errno));
	close(sk);

	return err;
}

int connman_inet_clear_gateway_address(int index, const char *gateway)
{
	struct ifreq ifr;
	struct rtentry rt;
	struct sockaddr_in addr;
	int sk, err;

	DBG("");

	sk = socket(PF_INET, SOCK_DGRAM, 0);
	if (sk < 0)
		return -1;

	memset(&ifr, 0, sizeof(ifr));
	ifr.ifr_ifindex = index;

	if (ioctl(sk, SIOCGIFNAME, &ifr) < 0) {
		close(sk);
		return -1;
	}

	DBG("ifname %s", ifr.ifr_name);

	memset(&rt, 0, sizeof(rt));
	rt.rt_flags = RTF_UP | RTF_GATEWAY;

	memset(&addr, 0, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_addr.s_addr = INADDR_ANY;
	memcpy(&rt.rt_dst, &addr, sizeof(rt.rt_dst));

	memset(&addr, 0, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_addr.s_addr = inet_addr(gateway);
	memcpy(&rt.rt_gateway, &addr, sizeof(rt.rt_gateway));

	memset(&addr, 0, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_addr.s_addr = INADDR_ANY;
	memcpy(&rt.rt_genmask, &addr, sizeof(rt.rt_genmask));

	err = ioctl(sk, SIOCDELRT, &rt);
	if (err < 0)
		connman_error("Removing default gateway route failed (%s)",
							strerror(errno));

	close(sk);

	return err;
}

int connman_inet_clear_gateway_interface(int index)
{
	struct ifreq ifr;
	struct rtentry rt;
	struct sockaddr_in addr;
	int sk, err;

	DBG("");

	sk = socket(PF_INET, SOCK_DGRAM, 0);
	if (sk < 0)
		return -1;

	memset(&ifr, 0, sizeof(ifr));
	ifr.ifr_ifindex = index;

	if (ioctl(sk, SIOCGIFNAME, &ifr) < 0) {
		close(sk);
		return -1;
	}

	DBG("ifname %s", ifr.ifr_name);

	memset(&rt, 0, sizeof(rt));
	rt.rt_flags = RTF_UP;

	memset(&addr, 0, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_addr.s_addr = INADDR_ANY;

	memcpy(&rt.rt_genmask, &addr, sizeof(rt.rt_genmask));
	memcpy(&rt.rt_dst, &addr, sizeof(rt.rt_dst));
	memcpy(&rt.rt_gateway, &addr, sizeof(rt.rt_gateway));

	rt.rt_dev = ifr.ifr_name;

	err = ioctl(sk, SIOCDELRT, &rt);
	if (err < 0)
		connman_error("Removing default interface route failed (%s)",
							strerror(errno));
	close(sk);

	return err;
}

connman_bool_t connman_inet_compare_subnet(int index, const char *host)
{
	struct ifreq ifr;
	struct in_addr _host_addr;
	in_addr_t host_addr, netmask_addr, if_addr;
	struct sockaddr_in *netmask, *addr;
	int sk;

	DBG("host %s", host);

	if (host == NULL)
		return FALSE;

	if (inet_aton(host, &_host_addr) == 0)
		return -1;
	host_addr = _host_addr.s_addr;

	sk = socket(PF_INET, SOCK_DGRAM, 0);
	if (sk < 0)
		return FALSE;

	memset(&ifr, 0, sizeof(ifr));
	ifr.ifr_ifindex = index;

	if (ioctl(sk, SIOCGIFNAME, &ifr) < 0) {
		close(sk);
		return FALSE;
	}

	if (ioctl(sk, SIOCGIFNETMASK, &ifr) < 0) {
		close(sk);
		return FALSE;
	}

	netmask = (struct sockaddr_in *)&ifr.ifr_netmask;
	netmask_addr = netmask->sin_addr.s_addr;

	if (ioctl(sk, SIOCGIFADDR, &ifr) < 0) {
		close(sk);
		return FALSE;
	}
	addr = (struct sockaddr_in *)&ifr.ifr_addr;
	if_addr = addr->sin_addr.s_addr;

	return ((if_addr & netmask_addr) == (host_addr & netmask_addr));
}
