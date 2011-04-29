/*
 *
 *  neard - Near Field Communication manager
 *
 *  Copyright (C) 2011  Intel Corporation. All rights reserved.
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
#include <stdlib.h>
#include <errno.h>

#include <glib.h>

#include <gdbus.h>

#include <linux/nfc.h>

#include "near.h"

static DBusConnection *connection = NULL;

static GHashTable *adapter_hash;

struct near_adapter {
	char *path;

	char *name;
	uint32_t idx;
	uint32_t protocols;

	near_bool_t powered;
	near_bool_t polling;

	struct near_target *target;
};

static void free_adapter(gpointer data)
{
	struct near_adapter *adapter = data;

	g_free(adapter->name);
	g_free(adapter->path);
	g_free(adapter);
}

static void append_path(gpointer key, gpointer value, gpointer user_data)
{
	struct near_adapter *adapter = value;
	DBusMessageIter *iter = user_data;

	DBG("%s", adapter->path);

	if (adapter->path == NULL)
		return;

	dbus_message_iter_append_basic(iter, DBUS_TYPE_OBJECT_PATH,
							&adapter->path);

}

void __near_adapter_list(DBusMessageIter *iter, void *user_data)
{
	g_hash_table_foreach(adapter_hash, append_path, iter);
}

static void append_protocols(DBusMessageIter *iter, void *user_data)
{
	struct near_adapter *adapter = user_data;
	const char *str;

	DBG("protocols 0x%x", adapter->protocols);

	if (adapter->protocols & NFC_PROTO_FELICA) {
		str = "Felica";

		dbus_message_iter_append_basic(iter,
				DBUS_TYPE_STRING, &str);
	}

	if (adapter->protocols & NFC_PROTO_MIFARE) {
		str = "MIFARE";

		dbus_message_iter_append_basic(iter,
				DBUS_TYPE_STRING, &str);
	}

	if (adapter->protocols & NFC_PROTO_JEWEL) {
		str = "Jewel";

		dbus_message_iter_append_basic(iter,
				DBUS_TYPE_STRING, &str);
	}

	if (adapter->protocols & NFC_PROTO_ISO14443_4) {
		str = "ISO-DEP";

		dbus_message_iter_append_basic(iter,
				DBUS_TYPE_STRING, &str);
	}

	if (adapter->protocols & NFC_PROTO_NFC_DEP) {
		str = "NFC-DEP";

		dbus_message_iter_append_basic(iter,
				DBUS_TYPE_STRING, &str);
	}
}

static DBusMessage *get_properties(DBusConnection *conn,
					DBusMessage *msg, void *data)
{
	struct near_adapter *adapter = data;
	DBusMessage *reply;
	DBusMessageIter array, dict;

	DBG("conn %p", conn);

	reply = dbus_message_new_method_return(msg);
	if (reply == NULL)
		return NULL;

	dbus_message_iter_init_append(reply, &array);

	near_dbus_dict_open(&array, &dict);

	near_dbus_dict_append_basic(&dict, "Powered",
				    DBUS_TYPE_BOOLEAN, &adapter->powered);

	near_dbus_dict_append_basic(&dict, "Polling",
				    DBUS_TYPE_BOOLEAN, &adapter->polling);

	near_dbus_dict_append_array(&dict, "Protocols",
				DBUS_TYPE_STRING, append_protocols, adapter);

	if (adapter->target != NULL) {
		const char *target_path;

		target_path = __near_target_get_path(adapter->target);

		if (target_path != NULL) {
			near_dbus_dict_append_basic(&dict, "CurrentTarget",
				DBUS_TYPE_OBJECT_PATH, &target_path);
		}
	}

	near_dbus_dict_close(&array, &dict);

	return reply;
}

static DBusMessage *set_property(DBusConnection *conn,
					DBusMessage *msg, void *data)
{
	DBG("conn %p", conn);

	return g_dbus_create_reply(msg, DBUS_TYPE_INVALID);
}

static DBusMessage *start_poll(DBusConnection *conn,
					DBusMessage *msg, void *data)
{
	struct near_adapter *adapter = data;
	int err;

	DBG("conn %p", conn);

	err = __near_netlink_start_poll(adapter->idx, adapter->protocols);
	if (err < 0)
		return __near_error_failed(msg, -err);

	adapter->polling = TRUE;

	return g_dbus_create_reply(msg, DBUS_TYPE_INVALID);
}

static DBusMessage *stop_poll(DBusConnection *conn,
					DBusMessage *msg, void *data)
{
	struct near_adapter *adapter = data;
	int err;

	DBG("conn %p", conn);

	err = __near_netlink_stop_poll(adapter->idx);
	if (err < 0)
		return __near_error_failed(msg, -err);

	adapter->polling = FALSE;

	return g_dbus_create_reply(msg, DBUS_TYPE_INVALID);
}

static GDBusMethodTable adapter_methods[] = {
	{ "GetProperties",     "",      "a{sv}", get_properties     },
	{ "SetProperty",       "sv",    "",      set_property       },
	{ "StartPoll",         "",      "",      start_poll         },
	{ "StopPoll",          "",      "",      stop_poll          },
	{ },
};

static GDBusSignalTable adapter_signals[] = {
	{ "PropertyChanged",		"sv"	},
	{ "TargetFound",		"o"	},
	{ "TargetLost",			"o"	},
	{ }
};

struct near_adapter * __near_adapter_create(uint32_t idx,
					const char *name, uint32_t protocols)
{
	struct near_adapter *adapter;

	adapter = g_try_malloc0(sizeof(struct near_adapter));
	if (adapter == NULL)
		return NULL;

	adapter->name = g_strdup(name);
	if (adapter->name == NULL) {
		g_free(adapter);
		return NULL;
	}
	adapter->idx = idx;
	adapter->protocols = protocols;
	adapter->powered = TRUE;

	adapter->path = g_strdup_printf("%s/nfc%d", NFC_PATH, idx);

	return adapter;
}

void __near_adapter_destroy(struct near_adapter *adapter)
{
	DBG("");

	free_adapter(adapter);
}

const char *__near_adapter_get_path(struct near_adapter *adapter)
{
	return adapter->path;
}

struct near_adapter *__near_adapter_get(uint32_t idx)
{
	return g_hash_table_lookup(adapter_hash, GINT_TO_POINTER(idx));
}

int __near_adapter_add(struct near_adapter *adapter)
{
	uint32_t idx = adapter->idx;

	DBG("%s", adapter->path);

	if (g_hash_table_lookup(adapter_hash, GINT_TO_POINTER(idx)) != NULL)
		return -EEXIST;

	g_hash_table_insert(adapter_hash, GINT_TO_POINTER(idx), adapter);

	DBG("connection %p", connection);

	g_dbus_register_interface(connection, adapter->path,
					NFC_ADAPTER_INTERFACE,
					adapter_methods, adapter_signals,
							NULL, adapter, NULL);

	return 0;
}

void __near_adapter_remove(struct near_adapter *adapter)
{
	DBG("%s", adapter->path);

	g_dbus_unregister_interface(connection, adapter->path,
						NFC_ADAPTER_INTERFACE);

	g_hash_table_remove(adapter_hash, GINT_TO_POINTER(adapter->idx));
}

int __near_adapter_add_target(uint32_t idx, struct near_target *target)
{
	struct near_adapter *adapter;

	DBG("idx %d", idx);

	adapter = g_hash_table_lookup(adapter_hash, GINT_TO_POINTER(idx));
	if (adapter == NULL)
		return -ENODEV;

	adapter->target = target;
	adapter->polling = FALSE;

	return 0;
}

int __near_adapter_remove_target(uint32_t idx)
{
	struct near_adapter *adapter;

	DBG("idx %d", idx);

	adapter = g_hash_table_lookup(adapter_hash, GINT_TO_POINTER(idx));
	if (adapter == NULL)
		return -ENODEV;

	adapter->target = NULL;

	return 0;
}

int near_adapter_connect(uint32_t idx)
{
	struct near_adapter *adapter;
	uint32_t target_idx, protocols;

	DBG("idx %d", idx);

	adapter = g_hash_table_lookup(adapter_hash, GINT_TO_POINTER(idx));
	if (adapter == NULL)
		return -ENODEV;

	if (adapter->target == NULL)
		return -ENOLINK;

	target_idx = __near_target_get_idx(adapter->target);
	protocols = __near_target_get_protocols(adapter->target);

	return __near_netlink_activate_target(idx, target_idx, protocols);
}

int near_adapter_disconnect(uint32_t idx)
{
	struct near_adapter *adapter;
	uint32_t target_idx;

	DBG("idx %d", idx);

	adapter = g_hash_table_lookup(adapter_hash, GINT_TO_POINTER(idx));
	if (adapter == NULL)
		return -ENODEV;

	if (adapter->target == NULL)
		return -ENOLINK;

	target_idx = __near_target_get_idx(adapter->target);

	return __near_netlink_deactivate_target(idx, target_idx);
}

int __near_adapter_init(void)
{
	DBG("");

	connection = near_dbus_get_connection();

	adapter_hash = g_hash_table_new_full(g_direct_hash, g_direct_equal,
							NULL, free_adapter);

	return 0;
}

void __near_adapter_cleanup(void)
{
	g_hash_table_destroy(adapter_hash);
	adapter_hash = NULL;
}
