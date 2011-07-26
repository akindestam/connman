/*
 *
 *  Connection Manager
 *
 *  Copyright (C) 2011  BWM CarIT GmbH. All rights reserved.
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

#include <stdlib.h>

#include "gdbus/gdbus.h"

#include "test-connman.h"

#define ENABLE_WRAPPER 1

gboolean util_quit_loop(gpointer data)
{
	struct test_fix *fix = data;

	g_main_loop_quit(fix->main_loop);

	return FALSE;
}

guint util_idle_call(struct test_fix *fix, GSourceFunc func,
			GDestroyNotify notify)
{
	GSource *source;
	guint id;

	source = g_idle_source_new();
	g_source_set_callback(source, func, fix, notify);
	id = g_source_attach(source, g_main_loop_get_context(fix->main_loop));
	g_source_unref(source);

	return id;
}

guint util_call(struct test_fix *fix, GSourceFunc func,
		GDestroyNotify notify)
{
	GSource *source;
	guint id;

	source = g_timeout_source_new(0);
	g_source_set_callback(source, func, fix, notify);
	id = g_source_attach(source, g_main_loop_get_context(fix->main_loop));
	g_source_unref(source);

	return id;
}

void util_setup(struct test_fix *fix, gconstpointer data)
{
	fix->main_loop = g_main_loop_new(NULL, FALSE);
	fix->main_connection = g_dbus_setup_private(DBUS_BUS_SYSTEM,
							NULL, NULL);
}

void util_teardown(struct test_fix *fix, gconstpointer data)
{
	dbus_connection_close(fix->main_connection);
	dbus_connection_unref(fix->main_connection);

	g_main_loop_unref(fix->main_loop);
}

static void util_wrapper(struct test_fix *fix, gconstpointer data)
{
	GSourceFunc func = data;
#if ENABLE_WRAPPER
	if (g_test_trap_fork(60 * 1000 * 1000, 0) == TRUE) {
		util_call(fix, func, NULL);
		g_main_loop_run(fix->main_loop);
		exit(0);
	}

	g_test_trap_assert_passed();
#else
	util_call(fix, func, NULL);
	g_main_loop_run(fix->main_loop);
#endif
}

void util_test_add(const char *test_name, GSourceFunc test_func,
			util_test_setup_cb setup_cb,
			util_test_teardown_cb teardown_cb)
{
	g_test_add(test_name, struct test_fix, test_func,
		setup_cb, util_wrapper, teardown_cb);
}

void util_session_create(struct test_fix *fix, unsigned int max_sessions)
{
	unsigned int i;

	fix->max_sessions = max_sessions;
	fix->session = g_try_new0(struct test_session, max_sessions);

	for (i = 0; i < max_sessions; i++) {
		fix->session[i].fix = fix;
		fix->session[i].info = g_try_new0(struct test_session_info, 1);
		fix->session[i].connection = g_dbus_setup_private(
						DBUS_BUS_SYSTEM, NULL, NULL);
	}
}

void util_session_destroy(gpointer data)
{
	struct test_fix *fix = data;

	unsigned int i;

	for (i = 0; i < fix->max_sessions; i++) {
		dbus_connection_close(fix->session[i].connection);
		g_free(fix->session[i].info);
	}

	g_free(fix->session);
}

void util_session_init(struct test_session *session)
{
	DBusMessage *msg;
	DBusMessageIter iter;
	const char *path;
	int err;

	err = session_notify_register(session, session->notify_path);
	g_assert(err == 0);

	msg = manager_create_session(session->connection,
					session->info,
					session->notify_path);
	g_assert(msg != NULL);
	dbus_message_iter_init(msg, &iter);

	dbus_message_iter_get_basic(&iter, &path);
	session->session_path = g_strdup(path);

	dbus_message_unref(msg);
}

void util_session_cleanup(struct test_session *session)
{
	DBusMessage *msg;
	int err;

	msg = manager_destroy_session(session->connection,
					session->session_path);
	g_assert(msg != NULL);
	dbus_message_unref(msg);

	err = session_notify_unregister(session,
					session->notify_path);
	g_assert(err == 0);

	g_free(session->info->bearer);
	g_free(session->info->name);
	g_free(session->info->interface);
	g_slist_foreach(session->info->allowed_bearers,
			bearer_info_cleanup, NULL);
	g_slist_free(session->info->allowed_bearers);

	session->notify = NULL;
	g_free(session->notify_path);
	g_free(session->session_path);
}
