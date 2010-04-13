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

#include <errno.h>
#include <string.h>

#include <gdbus.h>

#include "connman.h"

static unsigned int hidden_counter = 0;

struct connman_network {
	struct connman_element element;
	enum connman_network_type type;
	enum connman_network_protocol protocol;
	connman_bool_t available;
	connman_bool_t connected;
	connman_bool_t roaming;
	connman_bool_t hidden;
	connman_uint8_t strength;
	connman_uint16_t frequency;
	char *identifier;
	char *address;
	char *name;
	char *node;
	char *group;
	struct connman_ipconfig *ipconfig;

	struct connman_network_driver *driver;
	void *driver_data;

	connman_bool_t registered;
	connman_bool_t connecting;
	connman_bool_t associating;

	struct connman_device *device;

	struct {
		void *ssid;
		int ssid_len;
		char *mode;
		unsigned short channel;
		char *security;
		char *passphrase;
		char *eap;
		char *identity;
		char *ca_cert_path;
		char *client_cert_path;
		char *private_key_path;
		char *private_key_passphrase;
		char *phase2_auth;
	} wifi;
};

static const char *type2string(enum connman_network_type type)
{
	switch (type) {
	case CONNMAN_NETWORK_TYPE_UNKNOWN:
	case CONNMAN_NETWORK_TYPE_VENDOR:
		break;
	case CONNMAN_NETWORK_TYPE_ETHERNET:
		return "ethernet";
	case CONNMAN_NETWORK_TYPE_WIFI:
		return "wifi";
	case CONNMAN_NETWORK_TYPE_WIMAX:
		return "wimax";
	case CONNMAN_NETWORK_TYPE_BLUETOOTH_PAN:
	case CONNMAN_NETWORK_TYPE_BLUETOOTH_DUN:
		return "bluetooth";
	case CONNMAN_NETWORK_TYPE_CELLULAR:
	case CONNMAN_NETWORK_TYPE_MBM:
	case CONNMAN_NETWORK_TYPE_HSO:
		return "cellular";
	}

	return NULL;
}

static DBusMessage *get_properties(DBusConnection *conn,
					DBusMessage *msg, void *data)
{
	struct connman_network *network = data;
	DBusMessage *reply;
	DBusMessageIter array, dict;

	DBG("conn %p", conn);

	if (__connman_security_check_privilege(msg,
					CONNMAN_SECURITY_PRIVILEGE_PUBLIC) < 0)
		return __connman_error_permission_denied(msg);

	reply = dbus_message_new_method_return(msg);
	if (reply == NULL)
		return NULL;

	dbus_message_iter_init_append(reply, &array);

	connman_dbus_dict_open(&array, &dict);

	if (network->device) {
		const char *path = connman_device_get_path(network->device);
		if (path != NULL)
			connman_dbus_dict_append_basic(&dict, "Device",
						DBUS_TYPE_OBJECT_PATH, &path);
	}

	if (network->address != NULL)
		connman_dbus_dict_append_basic(&dict, "Address",
					DBUS_TYPE_STRING, &network->address);

	if (network->name != NULL)
		connman_dbus_dict_append_basic(&dict, "Name",
					DBUS_TYPE_STRING, &network->name);

	connman_dbus_dict_append_basic(&dict, "Connected",
				DBUS_TYPE_BOOLEAN, &network->connected);

	if (network->strength > 0)
		connman_dbus_dict_append_basic(&dict, "Strength",
					DBUS_TYPE_BYTE, &network->strength);

	if (network->frequency > 0)
		connman_dbus_dict_append_basic(&dict, "Frequency",
					DBUS_TYPE_UINT16, &network->frequency);

	if (network->wifi.ssid != NULL && network->wifi.ssid_len > 0)
		connman_dbus_dict_append_fixed_array(&dict, "WiFi.SSID",
				DBUS_TYPE_BYTE, &network->wifi.ssid,
						network->wifi.ssid_len);

	if (network->wifi.mode != NULL)
		connman_dbus_dict_append_basic(&dict, "WiFi.Mode",
				DBUS_TYPE_STRING, &network->wifi.mode);

	if (network->wifi.channel > 0)
		connman_dbus_dict_append_basic(&dict, "WiFi.Channel",
				DBUS_TYPE_UINT16, &network->wifi.channel);

	if (network->wifi.security != NULL) {
		connman_dbus_dict_append_basic(&dict, "WiFi.Security",
				DBUS_TYPE_STRING, &network->wifi.security);

		if (g_strcmp0(network->wifi.security, "ieee8021x") == 0 &&
						    network->wifi.eap != NULL)
			connman_dbus_dict_append_basic(&dict, "WiFi.EAP",
					DBUS_TYPE_STRING, &network->wifi.eap);
	}


	if (network->wifi.passphrase != NULL &&
			__connman_security_check_privilege(msg,
				CONNMAN_SECURITY_PRIVILEGE_SECRET) == 0)
		connman_dbus_dict_append_basic(&dict, "WiFi.Passphrase",
				DBUS_TYPE_STRING, &network->wifi.passphrase);

	connman_dbus_dict_close(&array, &dict);

	return reply;
}

static GDBusMethodTable network_methods[] = {
	{ "GetProperties", "",   "a{sv}", get_properties },
	{ },
};

static GDBusSignalTable network_signals[] = {
	{ "PropertyChanged", "sv" },
	{ },
};

static DBusConnection *connection;

static void append_networks(DBusMessageIter *iter, void *user_data)
{
	struct connman_device *device = user_data;

	__connman_element_list((struct connman_element *) device,
					CONNMAN_ELEMENT_TYPE_NETWORK, iter);
}

static void emit_networks_signal(struct connman_device *device)
{
	const char *path = connman_device_get_path(device);

	connman_dbus_property_changed_array(path,
			CONNMAN_DEVICE_INTERFACE, "Networks",
			DBUS_TYPE_OBJECT_PATH, append_networks, device);
}

static int register_interface(struct connman_element *element)
{
	struct connman_network *network = element->network;

	DBG("element %p name %s", element, element->name);

	if (g_dbus_register_interface(connection, element->path,
					CONNMAN_NETWORK_INTERFACE,
					network_methods, network_signals,
					NULL, network, NULL) == FALSE) {
		connman_error("Failed to register %s network", element->path);
		return -EIO;
	}

	network->registered = TRUE;

	emit_networks_signal(network->device);

	return 0;
}

static void unregister_interface(struct connman_element *element)
{
	struct connman_network * network = element->network;

	DBG("element %p name %s", element, element->name);

	network->registered = FALSE;

	if (network->device != NULL)
		emit_networks_signal(network->device);

	g_dbus_unregister_interface(connection, element->path,
						CONNMAN_NETWORK_INTERFACE);
}

connman_bool_t __connman_network_has_driver(struct connman_network *network)
{
	if (network == NULL || network->driver == NULL)
		return FALSE;

	return network->registered;
}

static GSList *driver_list = NULL;

static gint compare_priority(gconstpointer a, gconstpointer b)
{
	const struct connman_network_driver *driver1 = a;
	const struct connman_network_driver *driver2 = b;

	return driver2->priority - driver1->priority;
}

/**
 * connman_network_driver_register:
 * @driver: network driver definition
 *
 * Register a new network driver
 *
 * Returns: %0 on success
 */
int connman_network_driver_register(struct connman_network_driver *driver)
{
	DBG("driver %p name %s", driver, driver->name);

	driver_list = g_slist_insert_sorted(driver_list, driver,
							compare_priority);

	return 0;
}

/**
 * connman_network_driver_unregister:
 * @driver: network driver definition
 *
 * Remove a previously registered network driver
 */
void connman_network_driver_unregister(struct connman_network_driver *driver)
{
	DBG("driver %p name %s", driver, driver->name);

	driver_list = g_slist_remove(driver_list, driver);
}

static void network_destruct(struct connman_element *element)
{
	struct connman_network *network = element->network;

	DBG("element %p name %s", element, element->name);

	g_free(network->wifi.ssid);
	g_free(network->wifi.mode);
	g_free(network->wifi.security);
	g_free(network->wifi.passphrase);

	g_free(network->group);
	g_free(network->node);
	g_free(network->name);
	g_free(network->address);
	g_free(network->identifier);

	if (network->ipconfig) {
		connman_ipconfig_unref(network->ipconfig);
		network->ipconfig = NULL;
	}

	network->device = NULL;
}

/**
 * connman_network_create:
 * @identifier: network identifier (for example an unqiue name)
 *
 * Allocate a new network and assign the #identifier to it.
 *
 * Returns: a newly-allocated #connman_network structure
 */
struct connman_network *connman_network_create(const char *identifier,
						enum connman_network_type type)
{
	struct connman_network *network;
	connman_uint8_t strength = 0;
	const char *str;
	char *temp;

	DBG("identifier %s type %d", identifier, type);

	network = g_try_new0(struct connman_network, 1);
	if (network == NULL)
		return NULL;

	DBG("network %p", network);

	__connman_element_initialize(&network->element);

	//temp = connman_dbus_encode_string(identifier);
	if (identifier == NULL) {
		temp = g_strdup_printf("hidden_%d", hidden_counter++);
		network->hidden = TRUE;
	} else
		temp = g_strdup(identifier);

	if (temp == NULL) {
		g_free(network);
		return NULL;
	}

	network->element.name = temp;
	network->element.type = CONNMAN_ELEMENT_TYPE_NETWORK;

	network->element.network = network;
	network->element.destruct = network_destruct;

	str = type2string(type);
	if (str != NULL)
		connman_element_set_string(&network->element, "Type", str);

	connman_element_set_uint8(&network->element, "Strength", strength);

	network->type       = type;
	network->identifier = g_strdup(temp);

	return network;
}

/**
 * connman_network_ref:
 * @network: network structure
 *
 * Increase reference counter of  network
 */
struct connman_network *connman_network_ref(struct connman_network *network)
{
	if (connman_element_ref(&network->element) == NULL)
		return NULL;

	return network;
}

/**
 * connman_network_unref:
 * @network: network structure
 *
 * Decrease reference counter of network
 */
void connman_network_unref(struct connman_network *network)
{
	connman_element_unref(&network->element);
}

const char *__connman_network_get_type(struct connman_network *network)
{
	return type2string(network->type);
}

/**
 * connman_network_get_type:
 * @network: network structure
 *
 * Get type of network
 */
enum connman_network_type connman_network_get_type(struct connman_network *network)
{
	return network->type;
}

/**
 * connman_network_get_identifier:
 * @network: network structure
 *
 * Get identifier of network
 */
const char *connman_network_get_identifier(struct connman_network *network)
{
	return network->identifier;
}

/**
 * connman_network_get_path:
 * @network: network structure
 *
 * Get path name of network
 */
const char *connman_network_get_path(struct connman_network *network)
{
	return network->element.path;
}

/**
 * connman_network_set_index:
 * @network: network structure
 * @index: index number
 *
 * Set index number of network
 */
void connman_network_set_index(struct connman_network *network, int index)
{
	struct connman_service *service;

	service = __connman_service_lookup_from_network(network);
	if (service == NULL)
		goto done;

	if (network->element.index < 0)
		/*
		 * This is needed for plugins that havent set their ipconfig
		 * layer yet, due to not being able to get a network index
		 * prior to creating a service.
		 */
		__connman_service_create_ipconfig(service, index);
	else {
		struct connman_ipconfig *ipconfig;

		/* If index changed, the index of ipconfig must be reset. */
		ipconfig = __connman_service_get_ipconfig(service);
		if (ipconfig == NULL)
			goto done;

		__connman_ipconfig_set_index(ipconfig, index);
	}

done:
	network->element.index = index;
}

/**
 * connman_network_get_index:
 * @network: network structure
 *
 * Get index number of network
 */
int connman_network_get_index(struct connman_network *network)
{
	return network->element.index;
}

/**
 * connman_network_get_element:
 * @network: network structure
 *
 * Get connman_element of network
 */
struct connman_element *connman_network_get_element(
				struct connman_network *network)
{
	return &network->element;
}

/**
 * connman_network_set_protocol:
 * @network: network structure
 * @protocol: network protocol
 *
 * Change protocol of network
 */
void connman_network_set_protocol(struct connman_network *network,
					enum connman_network_protocol protocol)
{
	network->protocol = protocol;
}

/**
 * connman_network_set_group:
 * @network: network structure
 * @group: group name
 *
 * Set group name for automatic clustering
 */
void connman_network_set_group(struct connman_network *network,
							const char *group)
{
	switch (network->type) {
	case CONNMAN_NETWORK_TYPE_UNKNOWN:
	case CONNMAN_NETWORK_TYPE_VENDOR:
		return;
	case CONNMAN_NETWORK_TYPE_ETHERNET:
	case CONNMAN_NETWORK_TYPE_BLUETOOTH_PAN:
	case CONNMAN_NETWORK_TYPE_BLUETOOTH_DUN:
	case CONNMAN_NETWORK_TYPE_CELLULAR:
	case CONNMAN_NETWORK_TYPE_MBM:
	case CONNMAN_NETWORK_TYPE_HSO:
	case CONNMAN_NETWORK_TYPE_WIFI:
	case CONNMAN_NETWORK_TYPE_WIMAX:
		break;
	}

	if (g_strcmp0(network->group, group) == 0) {
		if (group != NULL)
			__connman_profile_update_network(network);
		return;
	}

	if (network->group != NULL) {
		__connman_profile_remove_network(network);

		g_free(network->group);
	}

	network->group = g_strdup(group);

	if (network->group != NULL)
		__connman_profile_add_network(network);
}

/**
 * connman_network_get_group:
 * @network: network structure
 *
 * Get group name for automatic clustering
 */
const char *connman_network_get_group(struct connman_network *network)
{
	return network->group;
}

const char *__connman_network_get_ident(struct connman_network *network)
{
	if (network->device == NULL)
		return NULL;

	return __connman_device_get_ident(network->device);
}

connman_bool_t __connman_network_get_weakness(struct connman_network *network)
{
	switch (network->type) {
	case CONNMAN_NETWORK_TYPE_UNKNOWN:
	case CONNMAN_NETWORK_TYPE_VENDOR:
	case CONNMAN_NETWORK_TYPE_ETHERNET:
	case CONNMAN_NETWORK_TYPE_BLUETOOTH_PAN:
	case CONNMAN_NETWORK_TYPE_BLUETOOTH_DUN:
	case CONNMAN_NETWORK_TYPE_CELLULAR:
	case CONNMAN_NETWORK_TYPE_MBM:
	case CONNMAN_NETWORK_TYPE_HSO:
	case CONNMAN_NETWORK_TYPE_WIMAX:
		break;
	case CONNMAN_NETWORK_TYPE_WIFI:
		if (g_strcmp0(network->wifi.mode, "adhoc") == 0)
			return TRUE;
		if (network->strength > 0 && network->strength < 20)
			return TRUE;
		break;
	}

	return FALSE;
}

connman_bool_t __connman_network_get_connecting(struct connman_network *network)
{
	return network->connecting;
}

/**
 * connman_network_set_available:
 * @network: network structure
 * @available: availability state
 *
 * Change availability state of network (in range)
 */
int connman_network_set_available(struct connman_network *network,
						connman_bool_t available)
{
	DBG("network %p available %d", network, available);

	if (network->available == available)
		return -EALREADY;

	network->available = available;

	return 0;
}

/**
 * connman_network_get_available:
 * @network: network structure
 *
 * Get network available setting
 */
connman_bool_t connman_network_get_available(struct connman_network *network)
{
	if (network->hidden == TRUE)
		return TRUE;

	return network->available;
}

/**
 * connman_network_set_associating:
 * @network: network structure
 * @associating: associating state
 *
 * Change associating state of network
 */
int connman_network_set_associating(struct connman_network *network,
						connman_bool_t associating)
{
	DBG("network %p associating %d", network, associating);

	if (network->associating == associating)
		return -EALREADY;

	network->associating = associating;

	if (associating == TRUE) {
		struct connman_service *service;

		service = __connman_service_lookup_from_network(network);
		__connman_service_indicate_state(service,
					CONNMAN_SERVICE_STATE_ASSOCIATION);
	}

	return 0;
}

static void set_associate_error(struct connman_network *network)
{
	struct connman_service *service;

	if (network->associating == FALSE)
		return ;

	network->associating = FALSE;

	service = __connman_service_lookup_from_network(network);

	__connman_service_indicate_state(service,
					CONNMAN_SERVICE_STATE_FAILURE);
}

static void set_configure_error(struct connman_network *network)
{
	struct connman_service *service;

	network->connecting = FALSE;

	service = __connman_service_lookup_from_network(network);

	__connman_service_indicate_state(service,
					CONNMAN_SERVICE_STATE_FAILURE);
}

void connman_network_set_method(struct connman_network *network,
					enum connman_ipconfig_method method)
{
	struct connman_service *service;
	struct connman_ipconfig *ipconfig;

	network->element.ipv4.method = method;

	service = __connman_service_lookup_from_network(network);
	if (service == NULL)
		return;

	ipconfig = __connman_service_get_ipconfig(service);
	if (ipconfig == NULL)
		return;

	connman_ipconfig_set_method(ipconfig, method);
}

void connman_network_set_error(struct connman_network *network,
					enum connman_network_error error)
{
	DBG("nework %p, error %d", network, error);

	network->connecting = FALSE;

	switch (error) {
	case CONNMAN_NETWORK_ERROR_UNKNOWN:
		return;
	case CONNMAN_NETWORK_ERROR_ASSOCIATE_FAIL:
		set_associate_error(network);
		break;
	case CONNMAN_NETWORK_ERROR_CONFIGURE_FAIL:
		set_configure_error(network);
		break;
	}
}

static void set_configuration(struct connman_network *network)
{
	struct connman_service *service;

	DBG("network %p", network);

	__connman_device_increase_connections(network->device);

	__connman_device_set_network(network->device, network);

	connman_device_set_disconnected(network->device, FALSE);

	service = __connman_service_lookup_from_network(network);
	__connman_service_indicate_state(service,
					CONNMAN_SERVICE_STATE_CONFIGURATION);
}

static void set_connected_manual(struct connman_network *network)
{
	struct connman_service *service;
	struct connman_ipconfig *ipconfig;
	int err;

	DBG("network %p", network);

	service = __connman_service_lookup_from_network(network);

	ipconfig = __connman_service_get_ipconfig(service);

	set_configuration(network);

	err = __connman_ipconfig_set_address(ipconfig);
	if (err < 0) {
		connman_network_set_error(network,
			CONNMAN_NETWORK_ERROR_CONFIGURE_FAIL);
		return;
	}

	network->connecting = FALSE;

	connman_network_set_associating(network, FALSE);

	__connman_service_indicate_state(service, CONNMAN_SERVICE_STATE_READY);
}

static int set_connected_dhcp(struct connman_network *network)
{
	struct connman_element *element;
	int error;

	DBG("network %p", network);

	if (network->protocol != CONNMAN_NETWORK_PROTOCOL_IP)
		return -EINVAL;

	element = connman_element_create(NULL);
	if (element == NULL)
		return -ENOMEM;

	element->type  = CONNMAN_ELEMENT_TYPE_DHCP;
	element->index = network->element.index;

	error = connman_element_register(element, &network->element);
	if (error < 0) {
		connman_element_unref(element);
		return error;
	}

	set_configuration(network);

	return 0;
}

static gboolean set_connected(gpointer user_data)
{
	struct connman_network *network = user_data;
	struct connman_service *service;
	struct connman_ipconfig *ipconfig;
	enum connman_ipconfig_method method;

	service = __connman_service_lookup_from_network(network);

	ipconfig = __connman_service_get_ipconfig(service);

	method = __connman_ipconfig_get_method(ipconfig);

	DBG("method %d", method);

	if (network->connected == TRUE) {
		switch (method) {
		case CONNMAN_IPCONFIG_METHOD_UNKNOWN:
		case CONNMAN_IPCONFIG_METHOD_OFF:
			return FALSE;
		case CONNMAN_IPCONFIG_METHOD_MANUAL:
		case CONNMAN_IPCONFIG_METHOD_FIXED:
			set_connected_manual(network);
			return TRUE;
		case CONNMAN_IPCONFIG_METHOD_DHCP:
			if (set_connected_dhcp(network) < 0) {
				connman_network_set_error(network,
					CONNMAN_NETWORK_ERROR_ASSOCIATE_FAIL);
				return FALSE;
			}
		}

	} else {
		struct connman_service *service;

		connman_element_unregister_children(&network->element);

		__connman_device_set_network(network->device, NULL);
		network->hidden = FALSE;

		__connman_device_decrease_connections(network->device);

		service = __connman_service_lookup_from_network(network);

		__connman_service_indicate_state(service,
						CONNMAN_SERVICE_STATE_IDLE);
	}

	network->connecting = FALSE;

	connman_network_set_associating(network, FALSE);

	return FALSE;
}

/**
 * connman_network_set_connected:
 * @network: network structure
 * @connected: connected state
 *
 * Change connected state of network
 */
int connman_network_set_connected(struct connman_network *network,
						connman_bool_t connected)
{
	DBG("network %p connected %d", network, connected);

	if ((network->connecting == TRUE || network->associating == TRUE) &&
							connected == FALSE) {
		connman_element_set_error(&network->element,
					CONNMAN_ELEMENT_ERROR_CONNECT_FAILED);
		__connman_network_disconnect(network);
	}

	if (network->connected == connected)
		return -EALREADY;

	network->connected = connected;

	if (network->registered == FALSE) {
		g_idle_add(set_connected, network);
		return 0;
	}

	connman_dbus_property_changed_basic(network->element.path,
				CONNMAN_NETWORK_INTERFACE, "Connected",
						DBUS_TYPE_BOOLEAN, &connected);

	set_connected(network);

	return 0;
}

/**
 * connman_network_get_connected:
 * @network: network structure
 *
 * Get network connection status
 */
connman_bool_t connman_network_get_connected(struct connman_network *network)
{
	return network->connected;
}

/**
 * connman_network_get_associating:
 * @network: network structure
 *
 * Get network associating status
 */
connman_bool_t connman_network_get_associating(struct connman_network *network)
{
	return network->associating;
}

/**
 * __connman_network_connect:
 * @network: network structure
 *
 * Connect network
 */
int __connman_network_connect(struct connman_network *network)
{
	int err;

	DBG("network %p", network);

	if (network->connected == TRUE)
		return -EISCONN;

	if (network->connecting == TRUE || network->associating == TRUE)
		return -EALREADY;

	if (network->driver == NULL)
		return -EUNATCH;

	if (network->driver->connect == NULL)
		return -ENOSYS;

	__connman_device_disconnect(network->device);

	network->connecting = TRUE;

	err = network->driver->connect(network);
	if (err < 0) {
		if (err == -EINPROGRESS)
			connman_network_set_associating(network, TRUE);
		else {
			network->connecting = FALSE;
			network->hidden = FALSE;
		}

		return err;
	}

	network->connected = TRUE;
	set_connected(network);

	return err;
}

/**
 * __connman_network_disconnect:
 * @network: network structure
 *
 * Disconnect network
 */
int __connman_network_disconnect(struct connman_network *network)
{
	int err;

	DBG("network %p", network);

	if (network->connected == FALSE && network->connecting == FALSE &&
						network->associating == FALSE)
		return -ENOTCONN;

	if (network->driver == NULL)
		return -EUNATCH;

	if (network->driver->disconnect == NULL)
		return -ENOSYS;

	network->connecting = FALSE;

	err = network->driver->disconnect(network);
	if (err == 0) {
		connman_network_set_connected(network, FALSE);
		set_connected(network);
	}

	return err;
}

/**
 * connman_network_set_address:
 * @network: network structure
 * @address: binary address value
 * @size: binary address length
 *
 * Set unique address value for network
 */
int connman_network_set_address(struct connman_network *network,
				const void *address, unsigned int size)
{
	const unsigned char *addr_octet = address;
	char *str;

	DBG("network %p size %d", network, size);

	if (size != 6)
		return -EINVAL;

	str = g_strdup_printf("%02X:%02X:%02X:%02X:%02X:%02X",
				addr_octet[0], addr_octet[1], addr_octet[2],
				addr_octet[3], addr_octet[4], addr_octet[5]);
	if (str == NULL)
		return -ENOMEM;

	g_free(network->address);
	network->address = str;

	return connman_element_set_string(&network->element,
						"Address", network->address);
}

/**
 * connman_network_set_name:
 * @network: network structure
 * @name: name value
 *
 * Set display name value for network
 */
int connman_network_set_name(struct connman_network *network,
							const char *name)
{
	DBG("network %p name %s", network, name);

	g_free(network->name);
	network->name = g_strdup(name);

	return connman_element_set_string(&network->element, "Name", name);
}

/**
 * connman_network_set_strength:
 * @network: network structure
 * @strength: strength value
 *
 * Set signal strength value for network
 */
int connman_network_set_strength(struct connman_network *network,
						connman_uint8_t strength)
{
	DBG("network %p strengh %d", network, strength);

	network->strength = strength;

	return connman_element_set_uint8(&network->element,
						"Strength", strength);
}

/**
 * connman_network_set_roaming:
 * @network: network structure
 * @roaming: roaming state
 *
 * Set roaming state for network
 */
int connman_network_set_roaming(struct connman_network *network,
						connman_bool_t roaming)
{
	DBG("network %p roaming %d", network, roaming);

	network->roaming = roaming;

	return connman_element_set_bool(&network->element,
						"Roaming", roaming);
}

/**
 * connman_network_set_string:
 * @network: network structure
 * @key: unique identifier
 * @value: string value
 *
 * Set string value for specific key
 */
int connman_network_set_string(struct connman_network *network,
					const char *key, const char *value)
{
	int err;

	DBG("network %p key %s value %s", network, key, value);

	if (g_strcmp0(key, "Name") == 0)
		return connman_network_set_name(network, value);

	if (g_str_equal(key, "Address") == TRUE) {
		g_free(network->address);
		network->address = g_strdup(value);
	} else if (g_str_equal(key, "Node") == TRUE) {
		g_free(network->node);
		network->node = g_strdup(value);
	} else if (g_str_equal(key, "WiFi.Mode") == TRUE) {
		g_free(network->wifi.mode);
		network->wifi.mode = g_strdup(value);
	} else if (g_str_equal(key, "WiFi.Security") == TRUE) {
		g_free(network->wifi.security);
		network->wifi.security = g_strdup(value);
	} else if (g_str_equal(key, "WiFi.Passphrase") == TRUE) {
		g_free(network->wifi.passphrase);
		network->wifi.passphrase = g_strdup(value);
	} else if (g_str_equal(key, "WiFi.EAP") == TRUE) {
		g_free(network->wifi.eap);
		network->wifi.eap = g_strdup(value);
	} else if (g_str_equal(key, "WiFi.Identity") == TRUE) {
		g_free(network->wifi.identity);
		network->wifi.identity = g_strdup(value);
	} else if (g_str_equal(key, "WiFi.CACertFile") == TRUE) {
		g_free(network->wifi.ca_cert_path);
		network->wifi.ca_cert_path = g_strdup(value);
	} else if (g_str_equal(key, "WiFi.ClientCertFile") == TRUE) {
		g_free(network->wifi.client_cert_path);
		network->wifi.client_cert_path = g_strdup(value);
	} else if (g_str_equal(key, "WiFi.PrivateKeyFile") == TRUE) {
		g_free(network->wifi.private_key_path);
		network->wifi.private_key_path = g_strdup(value);
	} else if (g_str_equal(key, "WiFi.PrivateKeyPassphrase") == TRUE) {
		g_free(network->wifi.private_key_passphrase);
		network->wifi.private_key_passphrase = g_strdup(value);
	} else if (g_str_equal(key, "WiFi.Phase2") == TRUE) {
		g_free(network->wifi.phase2_auth);
		network->wifi.phase2_auth = g_strdup(value);
	}

	err = connman_element_set_string(&network->element, key, value);
	if (err < 0)
		return err;

	if (network->driver == NULL)
		return 0;

	if (network->driver->setup)
		return network->driver->setup(network, key);

	return 0;
}

/**
 * connman_network_get_string:
 * @network: network structure
 * @key: unique identifier
 *
 * Get string value for specific key
 */
const char *connman_network_get_string(struct connman_network *network,
							const char *key)
{
	DBG("network %p key %s", network, key);

	if (g_str_equal(key, "Address") == TRUE)
		return network->address;
	else if (g_str_equal(key, "Name") == TRUE)
		return network->name;
	else if (g_str_equal(key, "Node") == TRUE)
		return network->node;
	else if (g_str_equal(key, "WiFi.Mode") == TRUE)
		return network->wifi.mode;
	else if (g_str_equal(key, "WiFi.Security") == TRUE)
		return network->wifi.security;
	else if (g_str_equal(key, "WiFi.Passphrase") == TRUE)
		return network->wifi.passphrase;
	else if (g_str_equal(key, "WiFi.EAP") == TRUE)
		return network->wifi.eap;
	else if (g_str_equal(key, "WiFi.Identity") == TRUE)
		return network->wifi.identity;
	else if (g_str_equal(key, "WiFi.CACertFile") == TRUE)
		return network->wifi.ca_cert_path;
	else if (g_str_equal(key, "WiFi.ClientCertFile") == TRUE)
		return network->wifi.client_cert_path;
	else if (g_str_equal(key, "WiFi.PrivateKeyFile") == TRUE)
		return network->wifi.private_key_path;
	else if (g_str_equal(key, "WiFi.PrivateKeyPassphrase") == TRUE)
		return network->wifi.private_key_passphrase;
	else if (g_str_equal(key, "WiFi.Phase2") == TRUE)
		return network->wifi.phase2_auth;

	return connman_element_get_string(&network->element, key);
}

/**
 * connman_network_set_bool:
 * @network: network structure
 * @key: unique identifier
 * @value: boolean value
 *
 * Set boolean value for specific key
 */
int connman_network_set_bool(struct connman_network *network,
					const char *key, connman_bool_t value)
{
	DBG("network %p key %s value %d", network, key, value);

	if (g_strcmp0(key, "Roaming") == 0)
		return connman_network_set_roaming(network, value);

	return connman_element_set_bool(&network->element, key, value);
}

/**
 * connman_network_get_bool:
 * @network: network structure
 * @key: unique identifier
 *
 * Get boolean value for specific key
 */
connman_bool_t connman_network_get_bool(struct connman_network *network,
							const char *key)
{
	DBG("network %p key %s", network, key);

	if (g_str_equal(key, "Roaming") == TRUE)
		return network->roaming;

	return connman_element_get_bool(&network->element, key);
}

/**
 * connman_network_set_uint8:
 * @network: network structure
 * @key: unique identifier
 * @value: integer value
 *
 * Set integer value for specific key
 */
int connman_network_set_uint8(struct connman_network *network,
					const char *key, connman_uint8_t value)
{
	DBG("network %p key %s value %d", network, key, value);

	if (g_strcmp0(key, "Strength") == 0)
		return connman_network_set_strength(network, value);

	return connman_element_set_uint8(&network->element, key, value);
}

/**
 * connman_network_get_uint8:
 * @network: network structure
 * @key: unique identifier
 *
 * Get integer value for specific key
 */
connman_uint8_t connman_network_get_uint8(struct connman_network *network,
							const char *key)
{
	DBG("network %p key %s", network, key);

	if (g_str_equal(key, "Strength") == TRUE)
		return network->strength;

	return connman_element_get_uint8(&network->element, key);
}

/**
 * connman_network_set_uint16:
 * @network: network structure
 * @key: unique identifier
 * @value: integer value
 *
 * Set integer value for specific key
 */
int connman_network_set_uint16(struct connman_network *network,
				const char *key, connman_uint16_t value)
{
	DBG("network %p key %s value %d", network, key, value);

	if (g_str_equal(key, "Frequency") == TRUE)
		network->frequency = value;
	else if (g_str_equal(key, "WiFi.Channel") == TRUE)
		network->wifi.channel = value;

	return -EINVAL;
}

/**
 * connman_network_get_uint16:
 * @network: network structure
 * @key: unique identifier
 *
 * Get integer value for specific key
 */
connman_uint16_t connman_network_get_uint16(struct connman_network *network,
							const char *key)
{
	DBG("network %p key %s", network, key);

	if (g_str_equal(key, "Frequency") == TRUE)
		return network->frequency;
	else if (g_str_equal(key, "WiFi.Channel") == TRUE)
		return network->wifi.channel;

	return 0;
}

/**
 * connman_network_set_blob:
 * @network: network structure
 * @key: unique identifier
 * @data: blob data
 * @size: blob size
 *
 * Set binary blob value for specific key
 */
int connman_network_set_blob(struct connman_network *network,
			const char *key, const void *data, unsigned int size)
{
	DBG("network %p key %s size %d", network, key, size);

	if (g_strcmp0(key, "Address") == 0)
		return connman_network_set_address(network, data, size);

	if (g_str_equal(key, "WiFi.SSID") == TRUE) {
		g_free(network->wifi.ssid);
		network->wifi.ssid = g_try_malloc(size);
		if (network->wifi.ssid != NULL) {
			memcpy(network->wifi.ssid, data, size);
			network->wifi.ssid_len = size;
		} else
			network->wifi.ssid_len = 0;
	}

	return connman_element_set_blob(&network->element, key, data, size);
}

/**
 * connman_network_get_blob:
 * @network: network structure
 * @key: unique identifier
 * @size: pointer to blob size
 *
 * Get binary blob value for specific key
 */
const void *connman_network_get_blob(struct connman_network *network,
					const char *key, unsigned int *size)
{
	DBG("network %p key %s", network, key);

	if (g_str_equal(key, "WiFi.SSID") == TRUE) {
		if (size != NULL)
			*size = network->wifi.ssid_len;
		return network->wifi.ssid;
	}

	return connman_element_get_blob(&network->element, key, size);
}

void __connman_network_set_device(struct connman_network *network,
					struct connman_device *device)
{
	network->device = device;
}

/**
 * connman_network_get_device:
 * @network: network structure
 *
 * Get parent device of network
 */
struct connman_device *connman_network_get_device(struct connman_network *network)
{
	return network->device;
}

/**
 * connman_network_get_data:
 * @network: network structure
 *
 * Get private network data pointer
 */
void *connman_network_get_data(struct connman_network *network)
{
	return network->driver_data;
}

/**
 * connman_network_set_data:
 * @network: network structure
 * @data: data pointer
 *
 * Set private network data pointer
 */
void connman_network_set_data(struct connman_network *network, void *data)
{
	network->driver_data = data;
}

static gboolean match_driver(struct connman_network *network,
					struct connman_network_driver *driver)
{
	if (network->type == driver->type ||
			driver->type == CONNMAN_NETWORK_TYPE_UNKNOWN)
		return TRUE;

	return FALSE;
}

static int network_probe(struct connman_element *element)
{
	struct connman_network *network = element->network;
	GSList *list;
	int err;

	DBG("element %p name %s", element, element->name);

	if (network == NULL)
		return -ENODEV;

	for (list = driver_list; list; list = list->next) {
		struct connman_network_driver *driver = list->data;

		if (match_driver(network, driver) == FALSE)
			continue;

		DBG("driver %p name %s", driver, driver->name);

		if (driver->probe(network) == 0) {
			network->driver = driver;
			break;
		}
	}

	if (network->driver == NULL)
		return -ENODEV;

	err = register_interface(element);
	if (err < 0) {
		if (network->driver->remove)
			network->driver->remove(network);
		return err;
	}

	switch (network->type) {
	case CONNMAN_NETWORK_TYPE_UNKNOWN:
	case CONNMAN_NETWORK_TYPE_VENDOR:
		break;
	case CONNMAN_NETWORK_TYPE_ETHERNET:
	case CONNMAN_NETWORK_TYPE_BLUETOOTH_PAN:
	case CONNMAN_NETWORK_TYPE_BLUETOOTH_DUN:
	case CONNMAN_NETWORK_TYPE_CELLULAR:
	case CONNMAN_NETWORK_TYPE_MBM:
	case CONNMAN_NETWORK_TYPE_HSO:
	case CONNMAN_NETWORK_TYPE_WIFI:
	case CONNMAN_NETWORK_TYPE_WIMAX:
		if (network->group != NULL)
			__connman_profile_add_network(network);
		break;
	}

	return 0;
}

static void network_remove(struct connman_element *element)
{
	struct connman_network *network = element->network;

	DBG("element %p name %s", element, element->name);

	if (network == NULL)
		return;

	if (network->driver == NULL)
		return;

	switch (network->type) {
	case CONNMAN_NETWORK_TYPE_UNKNOWN:
	case CONNMAN_NETWORK_TYPE_VENDOR:
		break;
	case CONNMAN_NETWORK_TYPE_ETHERNET:
	case CONNMAN_NETWORK_TYPE_BLUETOOTH_PAN:
	case CONNMAN_NETWORK_TYPE_BLUETOOTH_DUN:
	case CONNMAN_NETWORK_TYPE_CELLULAR:
	case CONNMAN_NETWORK_TYPE_MBM:
	case CONNMAN_NETWORK_TYPE_HSO:
	case CONNMAN_NETWORK_TYPE_WIFI:
	case CONNMAN_NETWORK_TYPE_WIMAX:
		if (network->group != NULL) {
			__connman_profile_remove_network(network);

			g_free(network->group);
			network->group = NULL;
		}
		break;
	}

	unregister_interface(element);

	if (network->driver->remove)
		network->driver->remove(network);
}

static void network_change(struct connman_element *element)
{
	struct connman_network *network = element->network;

	DBG("element %p name %s", element, element->name);

	if (element->state != CONNMAN_ELEMENT_STATE_ERROR)
		return;

	if (element->error != CONNMAN_ELEMENT_ERROR_DHCP_FAILED)
		return;

	if (network->connected == FALSE)
		return;

	connman_element_unregister_children(element);

	connman_device_set_disconnected(network->device, TRUE);

	if (network->driver && network->driver->disconnect) {
		network->driver->disconnect(network);
		return;
	}

	network->connected = FALSE;
}

static struct connman_driver network_driver = {
	.name		= "network",
	.type		= CONNMAN_ELEMENT_TYPE_NETWORK,
	.priority	= CONNMAN_DRIVER_PRIORITY_LOW,
	.probe		= network_probe,
	.remove		= network_remove,
	.change		= network_change,
};

int __connman_network_init(void)
{
	DBG("");

	connection = connman_dbus_get_connection();

	return connman_driver_register(&network_driver);
}

void __connman_network_cleanup(void)
{
	DBG("");

	connman_driver_unregister(&network_driver);

	dbus_connection_unref(connection);
}
