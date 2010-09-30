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

#include <connman/device.h>
#include <connman/network.h>

struct supplicant_driver {
	const char *name;
	void (*probe) (void);
	void (*remove) (void);
};

int supplicant_register(struct supplicant_driver *driver);
void supplicant_unregister(struct supplicant_driver *driver);

int supplicant_start(struct connman_device *device);
int supplicant_stop(struct connman_device *device);
int supplicant_scan(struct connman_device *device);

int supplicant_connect(struct connman_network *network);
int supplicant_disconnect(struct connman_network *network);

void supplicant_remove_network(struct connman_network *network);
