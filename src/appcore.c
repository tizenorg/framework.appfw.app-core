/*
 *  app-core
 *
 * Copyright (c) 2000 - 2011 Samsung Electronics Co., Ltd. All rights reserved.
 *
 * Contact: Jayoun Lee <airjany@samsung.com>, Sewook Park <sewook7.park@samsung.com>, Jaeho Lee <jaeho81.lee@samsung.com>
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 */


#define _GNU_SOURCE

#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <sys/types.h>
#include <unistd.h>
#include <malloc.h>
#include <locale.h>
#include <linux/limits.h>
#include <glib.h>
#include <sys/time.h>
#include <dlfcn.h>
#include <stdbool.h>
#include <dbus/dbus.h>
#include <dbus/dbus-glib-lowlevel.h>
#include <vconf.h>
#include <aul.h>
#include <bundle_internal.h>
#include <launch/app_signal.h>
#include "appcore-internal.h"

#define SQLITE_FLUSH_MAX		(1024*1024)
#define MAX_LOCAL_BUFSZ 128

#define PKGNAME_MAX 256
#define PATH_APP_ROOT "/opt/usr/apps"
#define PATH_RO_APP_ROOT "/usr/apps"
#define PATH_RES "/res"
#define PATH_LOCALE "/locale"

static struct appcore core;
static pid_t _pid;
char caller_appid[APPID_MAX];

static enum appcore_event to_ae[SE_MAX] = {
	APPCORE_EVENT_UNKNOWN,	/* SE_UNKNOWN */
	APPCORE_EVENT_LOW_MEMORY,	/* SE_LOWMEM */
	APPCORE_EVENT_LOW_BATTERY,	/* SE_LOWBAT */
	APPCORE_EVENT_LANG_CHANGE,	/* SE_LANGCGH */
	APPCORE_EVENT_REGION_CHANGE,
	APPCORE_EVENT_SUSPENDED_STATE_CHANGE,
};

static int appcore_event_initialized[SE_MAX] = {0};

enum cb_type {			/* callback */
	_CB_NONE,
	_CB_SYSNOTI,
	_CB_APPNOTI,
	_CB_VCONF,
};

struct evt_ops {
	enum cb_type type;
	union {
		enum appcore_event sys;
		enum app_event app;
		const char *vkey;
	} key;

	int (*cb_pre) (void *);
	int (*cb) (void *);
	int (*cb_post) (void *);

	int (*vcb_pre) (void *, void *);
	int (*vcb) (void *, void *);
	int (*vcb_post) (void *, void *);
};

struct open_s {
	int (*callback) (void *);
	void *cbdata;
};

static struct open_s open;

static int __app_terminate(void *data);
static int __app_resume(void *data);
static int __app_reset(void *data, bundle *k);

static int __sys_lowmem_post(void *data, void *evt);
static int __sys_lowmem(void *data, void *evt);
static int __sys_lowbatt(void *data, void *evt);
static int __sys_langchg_pre(void *data, void *evt);
static int __sys_langchg(void *data, void *evt);
static int __sys_regionchg_pre(void *data, void *evt);
static int __sys_regionchg(void *data, void *evt);
extern void aul_finalize();


static struct evt_ops evtops[] = {
	{
	 .type = _CB_VCONF,
	 .key.vkey = VCONFKEY_SYSMAN_LOW_MEMORY,
	 .vcb_post = __sys_lowmem_post,
	 .vcb = __sys_lowmem,
	 },
	{
	 .type = _CB_VCONF,
	 .key.vkey = VCONFKEY_SYSMAN_BATTERY_STATUS_LOW,
	 .vcb = __sys_lowbatt,
	 },
	{
	 .type = _CB_VCONF,
	 .key.vkey = VCONFKEY_LANGSET,
	 .vcb_pre = __sys_langchg_pre,
	 .vcb = __sys_langchg,
	 },
	{
	 .type = _CB_VCONF,
	 .key.vkey = VCONFKEY_REGIONFORMAT,
	 .vcb_pre = __sys_regionchg_pre,
	 .vcb = __sys_regionchg,
	 },
	{
	 .type = _CB_VCONF,
	 .key.vkey = VCONFKEY_REGIONFORMAT_TIME1224,
	 .vcb = __sys_regionchg,
	 },
};

#ifdef _APPFW_FEATURE_BACKGROUND_MANAGEMENT
static DBusConnection *bus = NULL;
static int __suspend_dbus_handler_initialized = 0;
#endif

static int __get_dir_name(char *dirname)
{
	char pkgid[PKGNAME_MAX];
	int r;
	int pid;
	char *cmdline = NULL;
	char *name = NULL;
	int len = 0;

	pid = getpid();
	if (pid < 0)
		return -1;

	cmdline = aul_get_cmdline_bypid(pid);
	if (cmdline == NULL) {
		_ERR("cmdline is null");
		return -1;
	}

	//for handling wrt case
	if (strncmp(cmdline, PATH_APP_ROOT, strlen(PATH_APP_ROOT)) != 0
			&& strncmp(cmdline, PATH_RO_APP_ROOT, strlen(PATH_RO_APP_ROOT)) != 0) {
		free(cmdline);

		if (aul_app_get_pkgid_bypid(pid, pkgid, PKGNAME_MAX) != AUL_R_OK)
			return -1;

		r = snprintf(dirname, PATH_MAX, PATH_APP_ROOT "/%s" PATH_RES PATH_LOCALE, pkgid);
		if (r < 0)
			return -1;

		if (access(dirname, R_OK) == 0)
			return 0;

		r = snprintf(dirname, PATH_MAX, PATH_RO_APP_ROOT "/%s" PATH_RES PATH_LOCALE, pkgid);
		if (r < 0)
			return -1;
	}
	else {
		name = (char *)g_strrstr(cmdline, "/");
		name[0] = '\0';
		name = (char *)g_strrstr(cmdline, "/");
		name[0] = '\0';

		len = strlen(cmdline);
		strncpy(dirname, cmdline, len);
		dirname[len] = '\0';

		len = strlen(dirname);
		strncat(dirname, "/res/locale", PATH_MAX - len);
		free(cmdline);
	}

	return 0;
}

static int __app_terminate(void *data)
{
	struct appcore *ac = data;

	_retv_if(ac == NULL || ac->ops == NULL, -1);
	_retv_if(ac->ops->cb_app == NULL, 0);

	ac->ops->cb_app(AE_TERMINATE, ac->ops->data, NULL);

	return 0;
}

static int __bgapp_terminate(void *data)
{
        struct appcore *ac = data;

        _retv_if(ac == NULL || ac->ops == NULL, -1);
        _retv_if(ac->ops->cb_app == NULL, 0);

        ac->ops->cb_app(AE_TERMINATE_BGAPP, ac->ops->data, NULL);

        return 0;
}

static gboolean __prt_ltime(gpointer data)
{
	int msec;

	msec = appcore_measure_time_from(NULL);
	if (msec)
		_DBG("[APP %d] first idle after reset: %d msec", _pid, msec);

	return FALSE;
}

static int __app_reset(void *data, bundle * k)
{
	struct appcore *ac = data;
	_retv_if(ac == NULL || ac->ops == NULL, -1);
	_retv_if(ac->ops->cb_app == NULL, 0);

	g_idle_add(__prt_ltime, ac);

	ac->ops->cb_app(AE_RESET, ac->ops->data, k);

	return 0;
}

static int __app_resume(void *data)
{
	x_raise_win(getpid());
	return 0;
}

static int __app_pause(void *data)
{
	x_pause_win(getpid());
	return 0;
}

static int __sys_do_default(struct appcore *ac, enum sys_event event)
{
	int r;

	switch (event) {
	case SE_LOWBAT:
		/*r = __def_lowbatt(ac);*/
		r = 0;
		break;
	default:
		r = 0;
		break;
	};

	return r;
}

static int __sys_do(struct appcore *ac, void *event_info, enum sys_event event)
{
	struct sys_op *op;

	_retv_if(ac == NULL || event >= SE_MAX, -1);

	op = &ac->sops[event];

	if (op->func == NULL)
		return __sys_do_default(ac, event);

	return op->func(event_info, op->data);
}

static int __sys_lowmem_post(void *data, void *evt)
{
	keynode_t *key = evt;
	int val;

	val = vconf_keynode_get_int(key);

	if (val >= VCONFKEY_SYSMAN_LOW_MEMORY_SOFT_WARNING)	{
#if defined(MEMORY_FLUSH_ACTIVATE)
		struct appcore *ac = data;

		_retv_if(ac == NULL || ac->ops == NULL, -1);
		_retv_if(ac->ops->cb_app == NULL, 0);

		ac->ops->cb_app(AE_LOWMEM_POST, ac->ops->data, NULL);
#else
		malloc_trim(0);
#endif
	}
	return 0;
}

static int __sys_lowmem(void *data, void *evt)
{
	keynode_t *key = evt;
	int val;

	val = vconf_keynode_get_int(key);

	if (val >= VCONFKEY_SYSMAN_LOW_MEMORY_SOFT_WARNING)
		return __sys_do(data, (void *)&val, SE_LOWMEM);

	return 0;
}

static int __sys_lowbatt(void *data, void *evt)
{
	keynode_t *key = evt;
	int val;

	val = vconf_keynode_get_int(key);

	/* VCONFKEY_SYSMAN_BAT_CRITICAL_LOW or VCONFKEY_SYSMAN_POWER_OFF */
	if (val <= VCONFKEY_SYSMAN_BAT_CRITICAL_LOW)
		return __sys_do(data, (void *)&val, SE_LOWBAT);

	return 0;
}

static int __sys_langchg_pre(void *data, void *evt)
{
	update_lang();
	return 0;
}

static int __sys_langchg(void *data, void *evt)
{
	keynode_t *key = evt;
	char *val;

	val = vconf_keynode_get_str(key);

	return __sys_do(data, (void *)val, SE_LANGCHG);
}

static int __sys_regionchg_pre(void *data, void *evt)
{
	update_region();
	return 0;
}

static int __sys_regionchg(void *data, void *evt)
{
	keynode_t *key = evt;
	char *val = NULL;
	const char *name;

	name = vconf_keynode_get_name(key);
	if (name && !strcmp(name, VCONFKEY_REGIONFORMAT))
		val = vconf_keynode_get_str(key);

	return __sys_do(data, (void *)val, SE_REGIONCHG);
}

static void __vconf_do(struct evt_ops *eo, keynode_t * key, void *data)
{
	_ret_if(eo == NULL);

	if (eo->vcb_pre)
		eo->vcb_pre(data, key);

	if (eo->vcb)
		eo->vcb(data, key);

	if (eo->vcb_post)
		eo->vcb_post(data, key);
}

static void __vconf_cb(keynode_t *key, void *data)
{
	int i;
	const char *name;

	name = vconf_keynode_get_name(key);
	_ret_if(name == NULL);

	_DBG("[APP %d] vconf changed: %s", _pid, name);

	for (i = 0; i < sizeof(evtops) / sizeof(evtops[0]); i++) {
		struct evt_ops *eo = &evtops[i];

		switch (eo->type) {
		case _CB_VCONF:
			if (!strcmp(name, eo->key.vkey))
				__vconf_do(eo, key, data);
			break;
		default:
			/* do nothing */
			break;
		}
	}
}

static int __add_vconf(struct appcore *ac, enum sys_event se)
{
	int r;

	switch (se) {
	case SE_LOWMEM:
		r = vconf_notify_key_changed(VCONFKEY_SYSMAN_LOW_MEMORY, __vconf_cb, ac);
		break;
	case SE_LOWBAT:
		r = vconf_notify_key_changed(VCONFKEY_SYSMAN_BATTERY_STATUS_LOW, __vconf_cb, ac);
		break;
	case SE_LANGCHG:
		r = vconf_notify_key_changed(VCONFKEY_LANGSET, __vconf_cb, ac);
		break;
	case SE_REGIONCHG:
		r = vconf_notify_key_changed(VCONFKEY_REGIONFORMAT, __vconf_cb, ac);
		if (r < 0)
			break;

		r = vconf_notify_key_changed(VCONFKEY_REGIONFORMAT_TIME1224, __vconf_cb, ac);
		break;
	default:
		r = -1;
		break;
	}

	return r;
}

static int __del_vconf(enum sys_event se)
{
	int r;

	switch (se) {
	case SE_LOWMEM:
		r = vconf_ignore_key_changed(VCONFKEY_SYSMAN_LOW_MEMORY, __vconf_cb);
		break;
	case SE_LOWBAT:
		r = vconf_ignore_key_changed(VCONFKEY_SYSMAN_BATTERY_STATUS_LOW, __vconf_cb);
		break;
	case SE_LANGCHG:
		r = vconf_ignore_key_changed(VCONFKEY_LANGSET, __vconf_cb);
		break;
	case SE_REGIONCHG:
		r = vconf_ignore_key_changed(VCONFKEY_REGIONFORMAT, __vconf_cb);
		if (r < 0)
			break;

		r = vconf_ignore_key_changed(VCONFKEY_REGIONFORMAT_TIME1224, __vconf_cb);
		break;
	default:
		r = -1;
		break;
	}

	return r;
}

static int __del_vconf_list(void)
{
	int r;
	enum sys_event se;

	for (se = SE_LOWMEM; se < SE_MAX; se++) {
		if (appcore_event_initialized[se]) {
			r = __del_vconf(se);
			if (r < 0)
				_ERR("Delete vconf callback failed");
			else
				appcore_event_initialized[se] = 0;
		}
	}

	return 0;
}

EXPORT_API int _appcore_request_to_suspend(int pid)
{
#ifdef _APPFW_FEATURE_BACKGROUND_MANAGEMENT
	static DBusConnection* conn = NULL;
	DBusMessage *message;
	DBusError err;

	dbus_error_init(&err);

	if (conn == NULL) {
		conn = dbus_bus_get(DBUS_BUS_SYSTEM, &err);
		if (!conn) {
			_ERR("Fail to dbus_bus_get : %s", err.message);
			return -1;
		}
	}

	message = dbus_message_new_signal(APPFW_SUSPEND_HINT_PATH,
			APPFW_SUSPEND_HINT_INTERFACE,
			APPFW_SUSPEND_HINT_SIGNAL);

	if (dbus_message_append_args(message,
				DBUS_TYPE_INT32, &pid,
				DBUS_TYPE_INVALID) == FALSE) {
		_ERR("Failed to load data error");
		return -1;
	}

	if (dbus_connection_send(conn, message, NULL) == FALSE) {
		_ERR("dbus send error");
		return -1;
	}

	dbus_connection_flush(conn);
	dbus_message_unref(message);

	SECURE_LOGD("[__SUSPEND__] Send suspend hint, pid: %d", pid);
#endif
	return 0;
}

#ifdef _APPFW_FEATURE_BACKGROUND_MANAGEMENT
static gboolean __flush_memory(gpointer data)
{
	int suspend = APPCORE_SUSPENDED_STATE_WILL_ENTER_SUSPEND;
	struct appcore *ac = (struct appcore *)data;

	appcore_flush_memory();

	if (!ac) {
		return FALSE;
	}
	ac->tid = 0;

	if (!ac->allowed_bg && !ac->suspended_state) {
		_DBG("[__SUSPEND__] flush case");
		__sys_do(ac, &suspend, SE_SUSPENDED_STATE);
		_appcore_request_to_suspend(getpid()); //send dbus signal to resourced
		ac->suspended_state = true;
	}

	return FALSE;
}
#endif

#ifdef _APPFW_FEATURE_BACKGROUND_MANAGEMENT
static void __add_suspend_timer(struct appcore *ac)
{
	ac->tid = g_timeout_add_seconds(5, __flush_memory, ac);
}
#endif

static void __remove_suspend_timer(struct appcore *ac)
{
#ifdef _APPFW_FEATURE_BACKGROUND_MANAGEMENT
	if (ac->tid > 0) {
		g_source_remove(ac->tid);
		ac->tid = 0;
	}
#endif
}

static int __aul_handler(aul_type type, bundle *b, void *data)
{
	int ret;
	const char *str = NULL;
	const char *bg = NULL;
	struct appcore *ac = data;

	switch (type) {
	case AUL_START:
		_DBG("[APP %d]     AUL event: AUL_START", _pid);
#ifdef _APPFW_FEATURE_EXPANSION_PKG_INSTALL
		const char *tep_path = NULL;
		tep_path = bundle_get_val(b, AUL_TEP_PATH);
		if (tep_path) {
			ret = aul_check_tep_mount(tep_path);
			if (ret == -1) {
				_ERR("mount request not completed within 1 sec");
				exit(-1);
			}
		}
#endif
		bg = bundle_get_val(b, AUL_K_ALLOWED_BG);
		if (bg && strncmp(bg, "ALLOWED_BG", strlen("ALLOWED_BG")) == 0) {
			_DBG("[__SUSPEND__] allowed background");
			ac->allowed_bg = true;
			__remove_suspend_timer(data);
		}

		__app_reset(data, b);
		str = bundle_get_val(b, AUL_K_CALLER_APPID);
		SECURE_LOGD("caller_appid : %s", str);
		if(str) {
			strncpy(caller_appid, str, APPID_MAX-1);
			caller_appid[APPID_MAX-1] = '\0';
		}
		break;
	case AUL_RESUME:
		_DBG("[APP %d]     AUL event: AUL_RESUME", _pid);
		bg = bundle_get_val(b, AUL_K_ALLOWED_BG);
		if (bg && strncmp(bg, "ALLOWED_BG", strlen("ALLOWED_BG")) == 0) {
			_DBG("[__SUSPEND__] allowed background");
			ac->allowed_bg = true;
			__remove_suspend_timer(data);
		}

		if(open.callback) {
			ret = open.callback(open.cbdata);
			if (ret == 0)
				__app_resume(data);
		} else {
			__app_resume(data);
		}
		break;
	case AUL_TERMINATE:
		_DBG("[APP %d]     AUL event: AUL_TERMINATE", _pid);
		if (!ac->allowed_bg) {
			__remove_suspend_timer(data);
		}
		__app_terminate(data);
		break;
	case AUL_TERMINATE_BGAPP:
		_DBG("[APP %d]     AUL event: AUL_TERMINATE_BGAPP", _pid);
		if (!ac->allowed_bg) {
			__remove_suspend_timer(data);
		}
		__bgapp_terminate(data);
		break;
	case AUL_PAUSE:
		_DBG("[APP %d]	   AUL event: AUL_PAUSE", _pid);
		__app_pause(data);
		break;
	default:
		_DBG("[APP %d]     AUL event: %d", _pid, type);
		/* do nothing */
		break;
	}

	return 0;
}


static void __clear(struct appcore *ac)
{
	memset(ac, 0, sizeof(struct appcore));
}

EXPORT_API char *appcore_get_caller_appid()
{
	return caller_appid;
}

EXPORT_API int appcore_set_open_cb(int (*cb) (void *),
				       void *data)
{
	open.callback = cb;
	open.cbdata = data;

	return 0;
}

EXPORT_API int appcore_set_event_callback(enum appcore_event event,
					  int (*cb) (void *, void *), void *data)
{
	struct appcore *ac = &core;
	struct sys_op *op;
	enum sys_event se;
	int r = 0;

	for (se = SE_UNKNOWN; se < SE_MAX; se++) {
		if (event == to_ae[se])
			break;
	}

	if (se == SE_UNKNOWN || se >= SE_MAX) {
		_ERR("Unregistered event");
		errno = EINVAL;
		return -1;
	}

	op = &ac->sops[se];

	op->func = cb;
	op->data = data;

	if (op->func && !appcore_event_initialized[se]) {
		r = __add_vconf(ac, se);
		if (r < 0)
			_ERR("Add vconf callback failed");
		else
			appcore_event_initialized[se] = 1;
	} else if (!op->func && appcore_event_initialized[se]) {
		r = __del_vconf(se);
		if (r < 0)
			_ERR("Delete vconf callback failed");
		else
			appcore_event_initialized[se] = 0;
	}

	return r;
}

EXPORT_API int appcore_init(const char *name, const struct ui_ops *ops,
		int argc, char **argv)
{
	int r;
	char dirname[PATH_MAX];

	if (core.state != 0) {
		_ERR("Already in use");
		errno = EALREADY;
		return -1;
	}

	if (ops == NULL || ops->cb_app == NULL) {
		_ERR("ops or callback function is null");
		errno = EINVAL;
		return -1;
	}

	r = __get_dir_name(dirname);
	SECURE_LOGD("dir : %s", dirname);
	r = set_i18n(name, dirname);
	_retv_if(r == -1, -1);

	r = _appcore_init_suspend_dbus_handler(&core);
	if (r == -1) {
		_ERR("Initailzing suspended state handler failed");
		goto err;
	}

	r = aul_launch_init(__aul_handler, &core);
	if (r < 0) {
		_ERR("Aul init failed: %d", r);
		goto err;
	}

	r = aul_launch_argv_handler(argc, argv);
	if (r < 0) {
		_ERR("Aul argv handler failed: %d", r);
		goto err;
	}

	core.ops = ops;
	core.state = 1;		/* TODO: use enum value */
	core.tid = 0;
	core.suspended_state = false;
	core.allowed_bg = false;

	_pid = getpid();

	return 0;
 err:
	__del_vconf_list();
	__clear(&core);
	_appcore_fini_suspend_dbus_handler(&core);
	return -1;
}

EXPORT_API void appcore_exit(void)
{
	if (core.state) {
		__del_vconf_list();
		__clear(&core);
		_appcore_fini_suspend_dbus_handler(&core);
		__remove_suspend_timer(&core);
	}
	aul_finalize();
}

EXPORT_API void appcore_get_app_core(struct appcore **ac)
{
	*ac = &core;
}

EXPORT_API int appcore_flush_memory(void)
{
	int (*flush_fn) (int);
	struct appcore *ac = &core;

	if (!core.state) {
		_ERR("Appcore not initialized");
		return -1;
	}

	//_DBG("[APP %d] Flushing memory ...", _pid);
	_retv_if(ac == NULL || ac->ops == NULL, -1);

	if (ac->ops->cb_app) {
		ac->ops->cb_app(AE_MEM_FLUSH, ac->ops->data, NULL);
	}

	flush_fn = dlsym(RTLD_DEFAULT, "sqlite3_release_memory");
	if (flush_fn) {
		flush_fn(SQLITE_FLUSH_MAX);
	}

	malloc_trim(0);
	/*
	*Disabled - the impact of stack_trim() is unclear
	*stack_trim();
	*/

	//_DBG("[APP %d] Flushing memory DONE", _pid);

	return 0;
}

#ifdef _APPFW_FEATURE_BACKGROUND_MANAGEMENT
static DBusHandlerResult
__suspend_dbus_signal_filter(DBusConnection *conn, DBusMessage *message, void *user_data)
{
	const char *sender;
	const char *interface;
	int pid;
	int state;
	int suspend;

	DBusError error;
	dbus_error_init(&error);

	sender = dbus_message_get_sender(message);
	if (sender == NULL)
		return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;

	interface = dbus_message_get_interface(message);
	if (interface == NULL) {
		_ERR("reject by security issue - no interface\n");
		return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
	}

	if (dbus_message_is_signal(message, interface, RESOURCED_FREEZER_SIGNAL)) {
		if (dbus_message_get_args(message, &error, DBUS_TYPE_INT32, &state,
					DBUS_TYPE_INT32, &pid, DBUS_TYPE_INVALID) == FALSE) {
			_ERR("Failed to get data: %s", error.message);
			dbus_error_free(&error);
			return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
		}

		if (pid == getpid() && state == 0) { //thawed
			suspend = APPCORE_SUSPENDED_STATE_DID_EXIT_FROM_SUSPEND;
		    SECURE_LOGD("[__SUSPEND__] state: %d (0: thawed, 1: frozen), pid: %d", state, pid);

			struct appcore *ac = (struct appcore *)user_data;
			if (!ac->allowed_bg && ac->suspended_state) {
				__remove_suspend_timer(ac);
				__sys_do(user_data, &suspend, SE_SUSPENDED_STATE);
				ac->suspended_state = false;
				__add_suspend_timer(ac);
			}
		}
	}

	return DBUS_HANDLER_RESULT_HANDLED;
}
#endif

int _appcore_init_suspend_dbus_handler(void *data)
{
#ifdef _APPFW_FEATURE_BACKGROUND_MANAGEMENT
	DBusError error;
	char rule[MAX_LOCAL_BUFSZ];

	if (__suspend_dbus_handler_initialized)
		return 0;

	dbus_error_init(&error);
	if (!bus) {
		bus = dbus_bus_get_private(DBUS_BUS_SYSTEM, &error);
		if (!bus) {
			_ERR("Failed to connect to the D-BUS daemon: %s", error.message);
			dbus_error_free(&error);
			return -1;
		}
	}
	dbus_connection_setup_with_g_main(bus, NULL);

	snprintf(rule, MAX_LOCAL_BUFSZ,
		 "path='%s',type='signal',interface='%s'", RESOURCED_FREEZER_PATH, RESOURCED_FREEZER_INTERFACE);
	/* listening to messages */
	dbus_bus_add_match(bus, rule, &error);
	if (dbus_error_is_set(&error)) {
		_ERR("Fail to rule set: %s", error.message);
		dbus_error_free(&error);
		return -1;
	}

	if (dbus_connection_add_filter(bus, __suspend_dbus_signal_filter, data, NULL) == FALSE) {
		_ERR("add filter fail");
		return -1;
	}

	__suspend_dbus_handler_initialized = 1;
	_DBG("[__SUSPEND__] suspend signal initialized");
#endif

	return 0;
}

int _appcore_fini_suspend_dbus_handler(void* data)
{
#ifdef _APPFW_FEATURE_BACKGROUND_MANAGEMENT
	DBusError error;
	char rule[MAX_LOCAL_BUFSZ];

	if (!__suspend_dbus_handler_initialized)
		return 0;

	dbus_error_init(&error);

	dbus_connection_remove_filter(bus, __suspend_dbus_signal_filter, data);

	snprintf(rule, MAX_LOCAL_BUFSZ,
		 "path='%s',type='signal',interface='%s'", RESOURCED_FREEZER_PATH, RESOURCED_FREEZER_INTERFACE);
	dbus_bus_remove_match(bus, rule, &error);
	if (dbus_error_is_set(&error)) {
		_ERR("Fail to rule unset: %s", error.message);
		dbus_error_free(&error);
		return -1;
	}

	dbus_connection_close(bus);
	dbus_connection_unref(bus);

	bus = NULL;

	__suspend_dbus_handler_initialized = 0;
	_DBG("[__SUSPEND__] suspend signal finalized");
#endif

	return 0;
}
