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

#ifndef __CONNMAN_RESOLVER_H
#define __CONNMAN_RESOLVER_H

#ifdef __cplusplus
extern "C" {
#endif

/**
 * SECTION:resolver
 * @title: Resolver premitives
 * @short_description: Functions for registering resolver modules
 */

int connman_resolver_append(const char *interface, const char *domain,
							const char *server);
int connman_resolver_remove(const char *interface, const char *domain,
							const char *server);
int connman_resolver_remove_all(const char *interface);

int connman_resolver_append_public_server(const char *server);
int connman_resolver_remove_public_server(const char *server);

void connman_resolver_flush(void);

#define CONNMAN_RESOLVER_PRIORITY_LOW      -100
#define CONNMAN_RESOLVER_PRIORITY_DEFAULT     0
#define CONNMAN_RESOLVER_PRIORITY_HIGH      100

struct connman_resolver {
	const char *name;
	int priority;
	int (*append) (const char *interface, const char *domain,
							const char *server);
	int (*remove) (const char *interface, const char *domain,
							const char *server);
	void (*flush) (void);
};

int connman_resolver_register(struct connman_resolver *resolver);
void connman_resolver_unregister(struct connman_resolver *resolver);

#ifdef __cplusplus
}
#endif

#endif /* __CONNMAN_RESOLVER_H */
