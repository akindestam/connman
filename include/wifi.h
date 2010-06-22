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

#ifndef __CONNMAN_WIFI_H
#define __CONNMAN_WIFI_H

#ifdef __cplusplus
extern "C" {
#endif

char *connman_wifi_build_group_name(const unsigned char *ssid,
						unsigned int ssid_len,
							const char *mode,
							const char *security);

char **connman_wifi_load_ssid(void);

#ifdef __cplusplus
}
#endif

#endif /* __CONNMAN_WIFI_H */
