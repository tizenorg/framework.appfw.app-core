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

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <stdio.h>
#include <unistd.h>
#include <stdarg.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <X11/Xatom.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/extensions/Xcomposite.h>
#include <X11/extensions/XShm.h>
#include <sys/shm.h>
#ifdef WEARABLE
#include <proc_stat.h>
#endif

#include <Ecore_X.h>
#include <Ecore.h>
#include <Ecore_Evas.h>
#include <Evas.h>
#include <Ecore_Input_Evas.h>
#include <Elementary.h>
#include <glib-object.h>
#include <malloc.h>
#include <glib.h>
#include <stdbool.h>
#include <aul.h>
#include <ttrace.h>
#include "appcore-internal.h"
#include "appcore-efl.h"
#include "virtual_canvas.h"
#ifdef _APPFW_FEATURE_VISIBILITY_CHECK_BY_LCD_STATUS
#include <vconf/vconf.h>
#endif

#include <bundle_internal.h>

#define SYSMAN_MAXSTR 100
#define SYSMAN_MAXARG 16
#define SYSNOTI_SOCKET_PATH "/tmp/sn"
#define RETRY_READ_COUNT	10
#define MAX_PACKAGE_STR_SIZE 512

#define PREDEF_BACKGRD				"backgrd"
#define PREDEF_FOREGRD				"foregrd"

enum sysnoti_cmd {
	ADD_SYSMAN_ACTION,
	CALL_SYSMAN_ACTION
};

struct sysnoti {
	int pid;
	int cmd;
	char *type;
	char *path;
	int argc;
	char *argv[SYSMAN_MAXARG];
};

static pid_t _pid;

static bool resource_reclaiming = TRUE;
static bool prelaunching = FALSE;



struct ui_priv {
	const char *name;
	enum app_state state;

	Ecore_Event_Handler *hshow;
	Ecore_Event_Handler *hhide;
	Ecore_Event_Handler *hvchange;
	Ecore_Event_Handler *hcmsg; /* WM_ROTATE */

	Ecore_Timer *mftimer;	/* Ecore Timer for memory flushing */

	struct appcore *app_core;
	struct appcore_ops *ops;

	void (*mfcb) (void);	/* Memory Flushing Callback */
	void (*prepare_to_suspend) (void *data);
	void (*exit_from_suspend) (void *data);

	/* WM_ROTATE */
	int wm_rot_supported;
	int rot_started;
	int (*rot_cb) (void *event_info, enum appcore_rm, void *);
	void *rot_cb_data;
	enum appcore_rm rot_mode;
};

static struct ui_priv priv;

static const char *_ae_name[AE_MAX] = {
	[AE_UNKNOWN] = "UNKNOWN",
	[AE_CREATE] = "CREATE",
	[AE_TERMINATE] = "TERMINATE",
	[AE_PAUSE] = "PAUSE",
	[AE_RESUME] = "RESUME",
	[AE_RESET] = "RESET",
	[AE_LOWMEM_POST] = "LOWMEM_POST",
	[AE_MEM_FLUSH] = "MEM_FLUSH",
};

static const char *_as_name[] = {
	[AS_NONE] = "NONE",
	[AS_CREATED] = "CREATED",
	[AS_RUNNING] = "RUNNING",
	[AS_PAUSED] = "PAUSED",
	[AS_DYING] = "DYING",
};

static int b_active = -1;
static bool first_launch = 1;
static int is_legacy_lifecycle = 0;

struct win_node {
	unsigned int win;
	bool bfobscured;
};

static struct ui_wm_rotate wm_rotate;

static inline int send_int(int fd, int val)
{
	return write(fd, &val, sizeof(int));
}

static inline int send_str(int fd, char *str)
{
	int len;
	int ret;
	if (str == NULL) {
		len = 0;
		ret = write(fd, &len, sizeof(int));
	} else {
		len = strlen(str);
		if (len > SYSMAN_MAXSTR)
			len = SYSMAN_MAXSTR;
		write(fd, &len, sizeof(int));
		ret = write(fd, str, len);
	}
	return ret;
}

#if 0
static int sysnoti_send(struct sysnoti *msg)
{
	int client_len;
	int client_sockfd;
	int result;
	int r;
	int retry_count = 0;
	struct sockaddr_un clientaddr;
	int i;

	client_sockfd = socket(AF_UNIX, SOCK_STREAM, 0);
	if (client_sockfd == -1) {
		_ERR("%s: socket create failed\n", __FUNCTION__);
		return -1;
	}
	bzero(&clientaddr, sizeof(clientaddr));
	clientaddr.sun_family = AF_UNIX;
	strncpy(clientaddr.sun_path, SYSNOTI_SOCKET_PATH, sizeof(clientaddr.sun_path) - 1);
	client_len = sizeof(clientaddr);

	if (connect(client_sockfd, (struct sockaddr *)&clientaddr, client_len) <
	    0) {
		_ERR("%s: connect failed\n", __FUNCTION__);
		close(client_sockfd);
		return -1;
	}

	send_int(client_sockfd, msg->pid);
	send_int(client_sockfd, msg->cmd);
	send_str(client_sockfd, msg->type);
	send_str(client_sockfd, msg->path);
	send_int(client_sockfd, msg->argc);
	for (i = 0; i < msg->argc; i++)
		send_str(client_sockfd, msg->argv[i]);

	while (retry_count < RETRY_READ_COUNT) {
		r = read(client_sockfd, &result, sizeof(int));
		if (r < 0) {
			if (errno == EINTR) {
				_ERR("Re-read for error(EINTR)");
				retry_count++;
				continue;
			}
			_ERR("Read fail for str length");
			result = -1;
			break;

		}
		break;
	}
	if (retry_count == RETRY_READ_COUNT) {
		_ERR("Read retry failed");
	}

	close(client_sockfd);
	return result;
}
#endif

void __trm_app_info_send_socket(char *write_buf)
{
	const char trm_socket_for_app_info[] = "/dev/socket/app_info";
	int socket_fd = 0;
	int ret = 0;
	struct sockaddr_un addr;

	_DBG("__trm_app_info_send_socket");

	if (access(trm_socket_for_app_info, F_OK) != 0) {
		_ERR("access");
		goto trm_end;
	}

	socket_fd = socket(AF_LOCAL, SOCK_STREAM, 0);
	if (socket_fd < 0) {
		_ERR("socket");
		goto trm_end;
	}

	memset(&addr, 0, sizeof(addr));
	snprintf(addr.sun_path, sizeof(addr.sun_path), "%s", trm_socket_for_app_info);
	addr.sun_family = AF_LOCAL;

	ret = connect(socket_fd, (struct sockaddr *) &addr ,sizeof(sa_family_t) + strlen(trm_socket_for_app_info) );
	if (ret != 0) {
		close(socket_fd);
		goto trm_end;
	}

	ret = send(socket_fd, write_buf, strlen(write_buf), MSG_DONTWAIT | MSG_NOSIGNAL);
	if (ret < 0) {
		_ERR("send() failed, errno: %d (%s)", errno, strerror(errno));
	} else {
		_DBG("send");
	}

	close(socket_fd);
trm_end:
	return;
}

#if 0
static int _call_predef_action(const char *type, int num, ...)
{
	struct sysnoti *msg;
	int ret;
	va_list argptr;

	int i;
	char *args = NULL;

	if (type == NULL || num > SYSMAN_MAXARG) {
		errno = EINVAL;
		return -1;
	}

	msg = malloc(sizeof(struct sysnoti));

	if (msg == NULL) {
		/* Do something for not enought memory error */
		return -1;
	}

	msg->pid = getpid();
	msg->cmd = CALL_SYSMAN_ACTION;
	msg->type = (char *)type;
	msg->path = NULL;

	msg->argc = num;
	va_start(argptr, num);
	for (i = 0; i < num; i++) {
		args = va_arg(argptr, char *);
		msg->argv[i] = args;
	}
	va_end(argptr);

	ret = sysnoti_send(msg);
	free(msg);

	return ret;
}

static int _inform_foregrd(void)
{
	char buf[255];
	snprintf(buf, sizeof(buf), "%d", getpid());
	return _call_predef_action(PREDEF_FOREGRD, 1, buf);
}

static int _inform_backgrd(void)
{
	char buf[255];
	snprintf(buf, sizeof(buf), "%d", getpid());
	return _call_predef_action(PREDEF_BACKGRD, 1, buf);
}
#endif

char appid[APPID_MAX];

#ifdef _APPFW_FEATURE_CAPTURE_FOR_TASK_MANAGER
bool taskmanage;
static void _capture_and_make_file(Ecore_X_Window win, int pid, const char *package);
static bool __check_skip(Ecore_X_Window xwin);
#endif

static int WIN_COMP(gconstpointer data1, gconstpointer data2)
{
	struct win_node *a = (struct win_node *)data1;
	struct win_node *b = (struct win_node *)data2;
	return (int)((a->win)-(b->win));
}

GSList *g_winnode_list = NULL;

static void __appcore_efl_prepare_to_suspend(void *data)
{
#ifdef _APPFW_FEATURE_BACKGROUND_MANAGEMENT
	struct ui_priv *ui = (struct ui_priv *)data;
	struct sys_op *op = NULL;
	int suspend = APPCORE_SUSPENDED_STATE_WILL_ENTER_SUSPEND;

	if (ui->app_core && !ui->app_core->allowed_bg && !ui->app_core->suspended_state) {
		op = &ui->app_core->sops[SE_SUSPENDED_STATE];
		if (op && op->func) {
			op->func((void *)&suspend, op->data); //calls c-api handler
		}
		_appcore_request_to_suspend(getpid()); //send dbus signal to resourced
		ui->app_core->suspended_state = true;
	}
	_DBG("[__SUSPEND__]");
#endif
}

static void __appcore_efl_exit_from_suspend(void *data)
{
#ifdef _APPFW_FEATURE_BACKGROUND_MANAGEMENT
	struct ui_priv *ui = (struct ui_priv *)data;
	struct sys_op *op = NULL;
	int suspend = APPCORE_SUSPENDED_STATE_DID_EXIT_FROM_SUSPEND;

	if (ui->app_core && !ui->app_core->allowed_bg && ui->app_core->suspended_state) {
		op = &ui->app_core->sops[SE_SUSPENDED_STATE];
		if (op && op->func) {
			op->func((void *)&suspend, op->data); //calls c-api handler
		}
		ui->app_core->suspended_state = false;
	}
	_DBG("[__SUSPEND__]");
#endif
}

#if defined(MEMORY_FLUSH_ACTIVATE)
static Eina_Bool __appcore_memory_flush_cb(void *data)
{
	_DBG("[__SUSPEND__]");
	struct ui_priv *ui = (struct ui_priv *)data;

	appcore_flush_memory();
	if (ui)
		ui->mftimer = NULL;

#ifdef _APPFW_FEATURE_BACKGROUND_MANAGEMENT
	if (ui && ui->prepare_to_suspend) {
		_DBG("[__SUSPEND__] flush case");
		ui->prepare_to_suspend(ui);
	}
#endif

	return ECORE_CALLBACK_CANCEL;
}

static int __appcore_low_memory_post_cb(struct ui_priv *ui)
{
	if (ui->state == AS_PAUSED) {
	//	appcore_flush_memory();
	} else {
		malloc_trim(0);
	}

	return 0;
}

static void __appcore_timer_add(struct ui_priv *ui)
{
	ui->mftimer = ecore_timer_add(5, __appcore_memory_flush_cb, ui);
}

static void __appcore_timer_del(struct ui_priv *ui)
{
	if (ui->mftimer) {
		ecore_timer_del(ui->mftimer);
		ui->mftimer = NULL;
	}
}

#else

static int __appcore_low_memory_post_cb(ui_priv *ui)
{
	return -1;
}

#define __appcore_timer_add(ui) 0
#define __appcore_timer_del(ui) 0

#endif

static void __appcore_efl_memory_flush_cb(void)
{
	//_DBG("[APP %d]   __appcore_efl_memory_flush_cb()", _pid);
	elm_cache_all_flush();
}

#ifdef _APPFW_FEATURE_CAPTURE_FOR_TASK_MANAGER
static int __is_sub_app()
{
	static int lpid = -1;
	int cpid = getpid();

	if (lpid == -1)
		lpid = aul_app_group_get_leader_pid(cpid);

	if (lpid == cpid)
		return 0;

	return 1;
}

static Eina_Bool __appcore_mimiapp_capture_cb(void *data)
{
	GSList *iter = NULL;
	struct win_node *entry = NULL;

	for (iter = g_winnode_list; iter != NULL; iter = g_slist_next(iter)) {
		entry = iter->data;
		if(__check_skip(entry->win) == FALSE)
			break;
	}
	if(iter) {
		entry = iter->data;
		if (taskmanage || __is_sub_app()) {
			_capture_and_make_file(entry->win, getpid(), appid);
		}
	}

	return ECORE_CALLBACK_CANCEL;
}
#endif

static void __do_app(enum app_event event, void *data, bundle * b)
{
	int r = -1;
	struct ui_priv *ui = data;
	char trm_buf[MAX_PACKAGE_STR_SIZE];
#ifdef _APPFW_FEATURE_CAPTURE_FOR_TASK_MANAGER
	const char *miniapp = NULL;
#endif

	if (ui == NULL || event >= AE_MAX) {
		return;
	}

	_INFO("[APP %d] Event: %s State: %s", _pid, _ae_name[event],
		_as_name[ui->state]);

	if (event == AE_MEM_FLUSH) {
		ui->mfcb();
		return;
	}

	if ((event == AE_LOWMEM_POST) &&
		(__appcore_low_memory_post_cb(ui) == 0)) {
			return;
	}

	if (!(ui->state == AS_PAUSED && event == AE_PAUSE))
		__appcore_timer_del(ui);

	if (ui->state == AS_DYING) {
		_ERR("Skip the event in dying state");
		return;
	}

	if (event == AE_TERMINATE) {
		_DBG("[APP %d] TERMINATE", _pid);
		elm_exit();
		aul_status_update(STATUS_DYING);
		return;
	}

	/* _ret_if(ui->ops == NULL); */

	switch (event) {
	case AE_RESET:
		_DBG("[APP %d] RESET", _pid);
		LOG(LOG_DEBUG, "LAUNCH", "[%s:Application:reset:start]",
			ui->name);

#ifdef _APPFW_FEATURE_BACKGROUND_MANAGEMENT
		if (ui->exit_from_suspend) {
			_DBG("[__SUSPEND__] reset case");
			ui->exit_from_suspend(ui);
		}
#endif

		if (ui->ops->reset) {
			traceBegin(TTRACE_TAG_APPLICATION_MANAGER,
					"APPCORE:RESET");
			ui->ops->reset(b, ui->ops->data);
			traceEnd(TTRACE_TAG_APPLICATION_MANAGER);
		}
		LOG(LOG_DEBUG, "LAUNCH", "[%s:Application:reset:done]",	ui->name);

		if (!first_launch) {
			is_legacy_lifecycle = aul_get_support_legacy_lifecycle();
			_INFO("Legacy lifecycle: %d", is_legacy_lifecycle);
			if (!is_legacy_lifecycle) {
				_INFO("[APP %d] App already running, raise the window", _pid);
				x_raise_win(getpid());
			}
		}
		first_launch = 0;

#ifdef _APPFW_FEATURE_CAPTURE_FOR_TASK_MANAGER
		miniapp = bundle_get_val(b, "http://tizen.org/appcontrol/data/miniapp");
		if(miniapp && strncmp(miniapp, "on", 2) == 0) {
			ecore_timer_add(0.5, __appcore_mimiapp_capture_cb, NULL);
		}
#endif
		break;
	case AE_PAUSE:
		if (ui->state == AS_RUNNING) {
			_DBG("[APP %d] PAUSE", _pid);
			if (ui->ops->pause) {
				traceBegin(TTRACE_TAG_APPLICATION_MANAGER,
						"APPCORE:PAUSE");
				r = ui->ops->pause(ui->ops->data);
				traceEnd(TTRACE_TAG_APPLICATION_MANAGER);
			}
			ui->state = AS_PAUSED;
			if (r >= 0) {
				if (resource_reclaiming == TRUE)
					__appcore_timer_add(ui);
#ifdef _APPFW_FEATURE_BACKGROUND_MANAGEMENT
				else if (resource_reclaiming == FALSE && ui->prepare_to_suspend) {
					_DBG("[__SUSPEND__] pause case");
					ui->prepare_to_suspend(ui);
				}
#endif
			}
		}
		/* TODO : rotation stop */
		//r = appcore_pause_rotation_cb();

		snprintf(trm_buf, MAX_PACKAGE_STR_SIZE, "appinfo_pause:[PID]%d", getpid());
		__trm_app_info_send_socket(trm_buf);
#ifdef WEARABLE
		proc_group_change_status(PROC_CGROUP_SET_BACKGRD, getpid(), NULL);
#endif
		aul_invoke_status_local_cb(STATUS_BG);
		//_inform_backgrd();
		break;
	case AE_RESUME:
		LOG(LOG_DEBUG, "LAUNCH", "[%s:Application:resume:start]",
			ui->name);

#ifdef _APPFW_FEATURE_BACKGROUND_MANAGEMENT
		if (ui->exit_from_suspend) {
			_DBG("[__SUSPEND__] resume case");
			ui->exit_from_suspend(ui);
		}
#endif

		if (ui->state == AS_PAUSED || ui->state == AS_CREATED) {
			_DBG("[APP %d] RESUME", _pid);

			if (ui->state == AS_CREATED) {
				is_legacy_lifecycle = aul_get_support_legacy_lifecycle();

				_INFO("Legacy lifecycle: %d", is_legacy_lifecycle);
				if (!is_legacy_lifecycle) {
					_INFO("[APP %d] Initial Launching, call the resume_cb", _pid);
					if (ui->ops->resume) {
						traceBegin(TTRACE_TAG_APPLICATION_MANAGER,
								"APPCORE:RESUME");
						ui->ops->resume(ui->ops->data);
						traceEnd(TTRACE_TAG_APPLICATION_MANAGER);
					}
				}
			} else {
				if (ui->ops->resume) {
					traceBegin(TTRACE_TAG_APPLICATION_MANAGER,
						"APPCORE:RESUME");
					ui->ops->resume(ui->ops->data);
					traceEnd(TTRACE_TAG_APPLICATION_MANAGER);
				}
			}
			ui->state = AS_RUNNING;

		}
		/*TODO : rotation start*/
		//r = appcore_resume_rotation_cb();
		LOG(LOG_DEBUG, "LAUNCH", "[%s:Application:resume:done]",
		    ui->name);
		LOG(LOG_DEBUG, "LAUNCH", "[%s:Application:Launching:done]",
		    ui->name);

#ifdef _GATE_TEST_ENABLE
		if(strncmp(ui->name, "wrt-client", 10) != 0) {
			LOG(LOG_DEBUG, "GATE-M", "<GATE-M>APP_FULLY_LOADED_%s<GATE-M>", ui->name);
		}
#endif

#ifdef WEARABLE
		proc_group_change_status(PROC_CGROUP_SET_FOREGRD, getpid(), NULL);
#endif
		snprintf(trm_buf, MAX_PACKAGE_STR_SIZE,"appinfo_resume:[PID]%d", getpid());
		__trm_app_info_send_socket(trm_buf);
		//_inform_foregrd();

		aul_invoke_status_local_cb(STATUS_VISIBLE);
		break;
	case AE_TERMINATE_BGAPP:
		if (ui->state == AS_PAUSED) {
			_DBG("[APP %d] is paused. TERMINATE", _pid);
                        ui->state = AS_DYING;
			aul_status_update(STATUS_DYING);
                        elm_exit();
		} else if (ui->state == AS_RUNNING) {
			_DBG("[APP %d] is running.", _pid);
		} else {
			_DBG("[APP %d] is another state", _pid);
		}
		break;
	default:
		/* do nothing */
		break;
	}
}

static struct ui_ops efl_ops = {
	.data = &priv,
	.cb_app = __do_app,
};


static bool __check_visible(void)
{
	GSList *iter = NULL;
	struct win_node *entry = NULL;

	for (iter = g_winnode_list; iter != NULL; iter = g_slist_next(iter)) {
		entry = iter->data;
		//_DBG("win : %x obscured : %d\n", entry->win, entry->bfobscured);
		if(entry->bfobscured == FALSE)
			return TRUE;
	}
	return FALSE;
}

#ifdef _APPFW_FEATURE_CAPTURE_FOR_TASK_MANAGER
static bool __check_skip(Ecore_X_Window xwin)
{
	unsigned int i, num;
	Ecore_X_Window_State *state;
	int ret;

	ret = ecore_x_netwm_window_state_get(xwin, &state, &num);
	_DBG("ret(%d), win(%x), state(%x), num(%d)", ret, xwin, state, num);
	if (state) {
		for (i = 0; i < num; i++) {
			_DBG("state[%d] : %d", i, state[i]);
			switch (state[i]) {
				case ECORE_X_WINDOW_STATE_SKIP_TASKBAR:
					free(state);
					return TRUE;
					break;
				case ECORE_X_WINDOW_STATE_SKIP_PAGER:
					free(state);
					return TRUE;
					break;
				default:
					/* Ignore */
					break;
			}
		}
	}
	free(state);
	return FALSE;
}
#endif

static bool __exist_win(unsigned int win)
{
	struct win_node temp;
	GSList *f;

	temp.win = win;

	f = g_slist_find_custom(g_winnode_list, &temp, WIN_COMP);
	if (f == NULL) {
		return FALSE;
	} else {
		return TRUE;
	}

}

static bool __add_win(unsigned int win)
{
	struct win_node *t;
	GSList *f;

	t = calloc(1, sizeof(struct win_node));
	if (t == NULL)
		return FALSE;

	t->win = win;
	t->bfobscured = TRUE;

	_DBG("[EVENT_TEST][EVENT] __add_win WIN:%x\n", win);

	f = g_slist_find_custom(g_winnode_list, t, WIN_COMP);

	if (f) {
		errno = ENOENT;
		_DBG("[EVENT_TEST][EVENT] ERROR There is already window : %x \n", win);
		free(t);
		return 0;
	}

	g_winnode_list = g_slist_append(g_winnode_list, t);

	return TRUE;

}

static bool __delete_win(unsigned int win)
{
	struct win_node temp;
	GSList *f;

	temp.win = win;

	f = g_slist_find_custom(g_winnode_list, &temp, WIN_COMP);
	if (f == NULL) {
		errno = ENOENT;
		_DBG("[EVENT_TEST][EVENT] ERROR There is no window : %x \n",
		     win);
		return 0;
	}

	free(f->data);
	g_winnode_list = g_slist_delete_link(g_winnode_list, f);

	return TRUE;
}

static bool __update_win(unsigned int win, bool bfobscured)
{
	struct win_node temp;
	GSList *f;

	struct win_node *t;

	_DBG("[EVENT_TEST][EVENT] __update_win WIN:%x fully_obscured %d\n", win,
	     bfobscured);

	temp.win = win;

	f = g_slist_find_custom(g_winnode_list, &temp, WIN_COMP);

	if (f == NULL) {
		errno = ENOENT;
		_DBG("[EVENT_TEST][EVENT] ERROR There is no window : %x \n", win);
		return FALSE;
	}

	free(f->data);
	g_winnode_list = g_slist_delete_link(g_winnode_list, f);

	t = calloc(1, sizeof(struct win_node));
	if (t == NULL)
		return FALSE;

	t->win = win;
	t->bfobscured = bfobscured;

	g_winnode_list = g_slist_append(g_winnode_list, t);

	return TRUE;

}

/* WM_ROTATE */
static Ecore_X_Atom _WM_WINDOW_ROTATION_SUPPORTED = 0;
#if 0
static Ecore_X_Atom _WM_WINDOW_ROTATION_CHANGE_REQUEST = 0;
#endif

static int __check_wm_rotation_support(void)
{
	_DBG("Disable window manager rotation");
	return -1;
#if 0
	Ecore_X_Window root, win, win2;
	int ret;

	if (!_WM_WINDOW_ROTATION_SUPPORTED) {
		_WM_WINDOW_ROTATION_SUPPORTED =
					ecore_x_atom_get("_E_WINDOW_ROTATION_SUPPORTED");
	}

	if (!_WM_WINDOW_ROTATION_CHANGE_REQUEST) {
		_WM_WINDOW_ROTATION_CHANGE_REQUEST =
					ecore_x_atom_get("_E_WINDOW_ROTATION_CHANGE_REQUEST");
	}

	root = ecore_x_window_root_first_get();
	ret = ecore_x_window_prop_xid_get(root,
			_WM_WINDOW_ROTATION_SUPPORTED,
			ECORE_X_ATOM_WINDOW,
			&win, 1);
	if ((ret == 1) && (win))
	{
		ret = ecore_x_window_prop_xid_get(win,
				_WM_WINDOW_ROTATION_SUPPORTED,
				ECORE_X_ATOM_WINDOW,
				&win2, 1);
		if ((ret == 1) && (win2 == win))
			return 0;
	}

	return -1;
#endif
}

static void __set_wm_rotation_support(unsigned int win, unsigned int set)
{
	GSList *iter = NULL;
	struct win_node *entry = NULL;

	if (0 == win) {
		for (iter = g_winnode_list; iter != NULL; iter = g_slist_next(iter)) {
			entry = iter->data;
			if (entry->win) {
				ecore_x_window_prop_card32_set(entry->win,
						_WM_WINDOW_ROTATION_SUPPORTED,
						&set, 1);
			}
		}
	} else {
		ecore_x_window_prop_card32_set(win,
				_WM_WINDOW_ROTATION_SUPPORTED,
				&set, 1);
	}
}

Ecore_X_Atom atom_parent;
Ecore_X_Atom xsend_Atom;
Ecore_X_Atom win_req_lower;
#ifdef _APPFW_FEATURE_VISIBILITY_CHECK_BY_LCD_STATUS
Ecore_X_Atom lcd_Atom;
#endif

static Eina_Bool __show_cb(void *data, int type, void *event)
{
	Ecore_X_Event_Window_Show *ev;
	int ret;
	Ecore_X_Window parent;

	ev = event;

	ret = ecore_x_window_prop_window_get(ev->win, atom_parent, &parent, 1);
	if (ret != 1)
	{
		// This is child window. Skip!!!
		_ERR(" This is child window. Skip!!! WIN:%x\n", ev->win);
		return ECORE_CALLBACK_PASS_ON;
	}

	_WARN("[EVENT_TEST][EVENT] GET SHOW EVENT!!!. WIN:%x\n", ev->win);

	if (!__exist_win((unsigned int)ev->win)) {
		/* WM_ROTATE */
		if ((priv.wm_rot_supported) && (1 == priv.rot_started)) {
			__set_wm_rotation_support(ev->win, 1);
		}
		__add_win((unsigned int)ev->win);
	}
	else
		__update_win((unsigned int)ev->win, TRUE);

	appcore_group_attach();
	return ECORE_CALLBACK_RENEW;
}

static Eina_Bool __hide_cb(void *data, int type, void *event)
{
	Ecore_X_Event_Window_Hide *ev;
	int bvisibility = 0;

	ev = event;

	_WARN("[EVENT_TEST][EVENT] GET HIDE EVENT!!!. WIN:%x\n", ev->win);

	if (__exist_win((unsigned int)ev->win)) {
		__delete_win((unsigned int)ev->win);

		bvisibility = __check_visible();
		if (!bvisibility && b_active != 0) {
			_DBG(" Go to Pasue state \n");
			b_active = 0;
			__do_app(AE_PAUSE, data, NULL);
#ifdef _APPFW_FEATURE_CAPTURE_FOR_TASK_MANAGER
			if (taskmanage || __is_sub_app()) {
				_capture_and_make_file(ev->win, getpid(), appid);
			} else if ( aul_is_subapp() ) {
				_capture_and_make_file(ev->win, getpid(), appcore_get_caller_appid());
			}
#endif
		}
	}

	return ECORE_CALLBACK_RENEW;
}

static Eina_Bool __visibility_cb(void *data, int type, void *event)
{
	Ecore_X_Event_Window_Visibility_Change *ev;
	int bvisibility = 0;
#ifdef _APPFW_FEATURE_VISIBILITY_CHECK_BY_LCD_STATUS
	int lcd_status = 0;
	int r = -1;
#endif

	ev = event;

	__update_win((unsigned int)ev->win, ev->fully_obscured);
	bvisibility = __check_visible();

	_DBG("bvisibility %d, b_active %d", bvisibility, b_active);

	if (bvisibility && b_active != 1) {
		_DBG(" Go to Resume state\n");
		b_active = 1;

#ifdef _APPFW_FEATURE_VISIBILITY_CHECK_BY_LCD_STATUS
		r = vconf_get_int(VCONFKEY_PM_STATE, &lcd_status);
		if (r == VCONF_OK && lcd_status == VCONFKEY_PM_STATE_LCDOFF) {
			_WARN("LCD status is off, skip the AE_RESUME event");
			return ECORE_CALLBACK_RENEW;
		}
#endif
		__do_app(AE_RESUME, data, NULL);
	} else if (!bvisibility && (b_active != 0)) {
		_DBG(" Go to Pasue state \n");
		b_active = 0;
		__do_app(AE_PAUSE, data, NULL);
#ifdef _APPFW_FEATURE_CAPTURE_FOR_TASK_MANAGER
		if (taskmanage || __is_sub_app()) {
			_capture_and_make_file(ev->win, getpid(), appid);
		} else if ( aul_is_subapp() ) {
			_capture_and_make_file(ev->win, getpid(), appcore_get_caller_appid());
		}
#endif
	} else
		_DBG(" No change state \n");

	return ECORE_CALLBACK_RENEW;

}

static Eina_Bool __cmsg_cb(void *data, int type, void *event)
{
	Ecore_X_Event_Client_Message *e = event;

	if (e->message_type == win_req_lower) {
		_DBG("win_req_lower");

		appcore_group_lower();
		return ECORE_CALLBACK_PASS_ON;
	}

#ifdef _APPFW_FEATURE_CAPTURE_FOR_TASK_MANAGER
	if (e->message_type == xsend_Atom) {
		_DBG("_E_ILLUME_ATOM_APPCORE_RECAPTURE_REQUEST win(%x)", e->win);
		_capture_and_make_file(e->win, getpid(), appid);
		return ECORE_CALLBACK_PASS_ON;
	}
#endif
#ifdef _APPFW_FEATURE_VISIBILITY_CHECK_BY_LCD_STATUS
	if (e->message_type == lcd_Atom) {
		if (e->data.l[0] == 1) {
			_WARN("LCD On. Resume the topmost app");
			__do_app(AE_RESUME, data, NULL);
		}
		else if (e->data.l[0] == 0) {
			_WARN("LCD Off. Pause the topmost app");
			__do_app(AE_PAUSE, data, NULL);
		}
		else {
			_ERR("Invalid value.");
		}
		return ECORE_CALLBACK_PASS_ON;
	}
#endif
	return ECORE_CALLBACK_PASS_ON;
}

static void __add_climsg_cb(struct ui_priv *ui)
{
	_ret_if(ui == NULL);

	atom_parent = ecore_x_atom_get("_E_PARENT_BORDER_WINDOW");
	if (!atom_parent)
	{
		// Do Error Handling
		_ERR("atom_parent is NULL");
	}
	xsend_Atom = ecore_x_atom_get("_E_ILLUME_ATOM_APPCORE_RECAPTURE_REQUEST");
	if (!xsend_Atom)
	{
		// Do Error Handling
		_ERR("xsend_Atom is NULL");
	}
#ifdef _APPFW_FEATURE_VISIBILITY_CHECK_BY_LCD_STATUS
	lcd_Atom =  ecore_x_atom_get("_SEND_EVENT_TO_WINDOW_BY_LCD_ON_OFF_");
	if (!lcd_Atom)
	{
		// Do Error Handling
		_ERR("lcd_Atom is NULL");
	}
#endif

	win_req_lower = ecore_x_atom_get("_E_ILLUME_ATOM_WIN_REQ_LOWER");
	if (!win_req_lower)
	{
		// Do Error Handling
		_ERR("win_req_lower is NULL");

	}

	ui->hshow = ecore_event_handler_add(ECORE_X_EVENT_WINDOW_SHOW, __show_cb, ui);
	ui->hhide = ecore_event_handler_add(ECORE_X_EVENT_WINDOW_HIDE, __hide_cb, ui);
	ui->hvchange = ecore_event_handler_add(ECORE_X_EVENT_WINDOW_VISIBILITY_CHANGE, __visibility_cb, ui);
	ui->hcmsg = ecore_event_handler_add(ECORE_X_EVENT_CLIENT_MESSAGE, __cmsg_cb, ui);

	/* Add client message callback for WM_ROTATE */
	if(!__check_wm_rotation_support())
	{
		ui->wm_rot_supported = 1;
		appcore_set_wm_rotation(&wm_rotate);
	}
}

static int __before_loop(struct ui_priv *ui, int *argc, char ***argv)
{
	int r;
	char *hwacc = NULL;
	struct appcore *ac = NULL;

	if (argc == NULL || argv == NULL) {
		_ERR("argc/argv is NULL");
		errno = EINVAL;
		return -1;
	}

#if !(GLIB_CHECK_VERSION(2, 36, 0))
	g_type_init();
#endif
	elm_init(*argc, *argv);

	hwacc = getenv("HWACC");
	if(hwacc && strcmp(hwacc, "USE") == 0) {
		elm_config_accel_preference_set("hw");
		_DBG("elm_config_accel_preference_set : hw");
	} else if(hwacc && strcmp(hwacc, "NOT_USE") == 0) {
		elm_config_accel_preference_set("none");
		_DBG("elm_config_accel_preference_set :  none");
	} else {
		_DBG("elm_config_preferred_engine_set is not called");
	}
#ifdef _APPFW_FEATURE_CAPTURE_FOR_TASK_MANAGER
	char *tm_tmp = NULL;

	tm_tmp = getenv("TASKMANAGE");
	if(tm_tmp && strcmp(tm_tmp, "false") == 0) {
		_DBG("taskmanage is false");
		taskmanage = 0;
	} else {
		_DBG("taskmanage is true %s", tm_tmp);
		taskmanage = 1;
	}
#endif

	r = appcore_init(ui->name, &efl_ops, *argc, *argv);
	_retv_if(r == -1, -1);

	appcore_get_app_core(&ac);
	ui->app_core = ac;
	SECURE_LOGD("[__SUSPEND__] appcore initialized, appcore addr: 0x%x", ac);

	LOG(LOG_DEBUG, "LAUNCH", "[%s:Platform:appcore_init:done]", ui->name);
	if (ui->ops && ui->ops->create) {
		traceBegin(TTRACE_TAG_APPLICATION_MANAGER, "APPCORE:CREATE");
		r = ui->ops->create(ui->ops->data);
		traceEnd(TTRACE_TAG_APPLICATION_MANAGER);
		if (r < 0) {
			_ERR("create() return error");
			appcore_exit();
			if (ui->ops && ui->ops->terminate) {
				traceBegin(TTRACE_TAG_APPLICATION_MANAGER,
						"APPCORE:TERMINATE");
				ui->ops->terminate(ui->ops->data);
				traceEnd(TTRACE_TAG_APPLICATION_MANAGER);
			}
			errno = ECANCELED;
			return -1;
		}
		LOG(LOG_DEBUG, "LAUNCH", "[%s:Application:create:done]",
		    ui->name);
	}
	ui->state = AS_CREATED;

	__add_climsg_cb(ui);

	return 0;
}

static void __after_loop(struct ui_priv *ui)
{
	appcore_unset_rotation_cb();
	appcore_exit();

	if (ui->state == AS_RUNNING) {
		_DBG("[APP %d] PAUSE before termination", _pid);
		if (ui->ops && ui->ops->pause) {
			traceBegin(TTRACE_TAG_APPLICATION_MANAGER,
					"APPCORE:PAUSE");
			ui->ops->pause(ui->ops->data);
			traceEnd(TTRACE_TAG_APPLICATION_MANAGER);
		}
	}

	if (ui->ops && ui->ops->terminate) {
		traceBegin(TTRACE_TAG_APPLICATION_MANAGER, "APPCORE:TERMINATE");
		ui->ops->terminate(ui->ops->data);
		traceEnd(TTRACE_TAG_APPLICATION_MANAGER);
	}

	ui->state = AS_DYING;

	if (ui->hshow)
		ecore_event_handler_del(ui->hshow);
	if (ui->hhide)
		ecore_event_handler_del(ui->hhide);
	if (ui->hvchange)
		ecore_event_handler_del(ui->hvchange);

	__appcore_timer_del(ui);

	// Check the process-pool case
	if (elm_shutdown() > 0)
		elm_shutdown();

#ifdef _GATE_TEST_ENABLE
	if((ui->name) && (strncmp(ui->name, "wrt-client", 10) != 0)) {
		LOG(LOG_DEBUG, "GATE-M", "<GATE-M>APP_CLOSED_%s<GATE-M>", ui->name);
	}
#endif
}

static int __set_data(struct ui_priv *ui, const char *name,
		    struct appcore_ops *ops)
{
	if (ui->name) {
		_ERR("Mainloop already started");
		errno = EINPROGRESS;
		return -1;
	}

	if (name == NULL || name[0] == '\0') {
		_ERR("Invalid name");
		errno = EINVAL;
		return -1;
	}

	if (ops == NULL) {
		_ERR("ops is NULL");
		errno = EINVAL;
		return -1;
	}

	ui->name = strdup(name);
	_retv_if(ui->name == NULL, -1);

	ui->ops = ops;

	ui->mfcb = __appcore_efl_memory_flush_cb;

	_pid = getpid();

	/* WM_ROTATE */
	ui->wm_rot_supported = 0;
	ui->rot_started = 0;
	ui->rot_cb = NULL;
	ui->rot_cb_data = NULL;
	ui->rot_mode = APPCORE_RM_UNKNOWN;

	ui->app_core = NULL;
	ui->prepare_to_suspend = __appcore_efl_prepare_to_suspend;
	ui->exit_from_suspend = __appcore_efl_exit_from_suspend;

	return 0;
}

static void __unset_data(struct ui_priv *ui)
{
	if (ui->name)
		free((void *)ui->name);

	memset(ui, 0, sizeof(struct ui_priv));
}

/* WM_ROTATE */
static int __wm_set_rotation_cb(int (*cb) (void *event_info, enum appcore_rm, void *), void *data)
{
	if (cb == NULL) {
		errno = EINVAL;
		return -1;
	}

	if ((priv.wm_rot_supported) && (0 == priv.rot_started)) {
		__set_wm_rotation_support(0, 1);
	}

	priv.rot_cb = cb;
	priv.rot_cb_data = data;
	priv.rot_started = 1;

	return 0;
}

static int __wm_unset_rotation_cb(void)
{
	if ((priv.wm_rot_supported) && (1 == priv.rot_started)) {
		__set_wm_rotation_support(0, 0);
	}

	priv.rot_cb = NULL;
	priv.rot_cb_data = NULL;
	priv.rot_started = 0;

	return 0;
}

static int __wm_get_rotation_state(enum appcore_rm *curr)
{
	if (curr == NULL) {
		errno = EINVAL;
		return -1;
	}

	*curr = priv.rot_mode;

	return 0;
}

static int __wm_pause_rotation_cb(void)
{
	if ((1 == priv.rot_started) && (priv.wm_rot_supported)) {
		__set_wm_rotation_support(0, 0);
	}

	priv.rot_started = 0;

	return 0;
}

static int __wm_resume_rotation_cb(void)
{
	if ((0 == priv.rot_started) && (priv.wm_rot_supported)) {
		__set_wm_rotation_support(0, 1);
	}

	priv.rot_started = 1;

	return 0;
}

static struct ui_wm_rotate wm_rotate = {
	__wm_set_rotation_cb,
	__wm_unset_rotation_cb,
	__wm_get_rotation_state,
	__wm_pause_rotation_cb,
	__wm_resume_rotation_cb
};

#ifdef _APPFW_FEATURE_CAPTURE_FOR_TASK_MANAGER
static Window _get_parent_window(Window id)
{
	Window root;
	Window parent;
	Window *children;
	unsigned int num;

	if (!XQueryTree(ecore_x_display_get(), id, &root, &parent, &children, &num)) {
		return 0;
	}

	if (children) {
		XFree(children);
	}

	return parent;
}

static Window _find_capture_window(Window id, Visual **visual, int *depth, int *width, int *height)
{
	XWindowAttributes attr;
	Window parent = id;
	Window orig_id = id;

	if (id == 0) {
		return (Window)-1;
	}

	do {
		id = parent;

		if (!XGetWindowAttributes(ecore_x_display_get(), id, &attr)) {
			return (Window)-1;
		}

		parent = _get_parent_window(id);

		if (attr.map_state == IsViewable
		    && attr.override_redirect == True
		    && attr.class == InputOutput && parent == attr.root) {
			*depth = attr.depth;
			*width = attr.width;
			*height = attr.height;
			*visual = attr.visual;
			return id;
		}
	} while (parent != attr.root && parent != 0);

	XGetWindowAttributes(ecore_x_display_get(), orig_id, &attr);
	*depth = attr.depth;
	*width = attr.width;
	*height = attr.height;
	*visual = attr.visual;

	return (Window) 0;

}

static char *_capture_window(Window id, Visual *visual, int width, int height, int depth, int *size)
{
	XShmSegmentInfo si;
	XImage *xim;
	int img_size;
	char *captured_img = NULL;

	/* (depth >> 3) + 1 == 4 byte */
	si.shmid =
	    shmget(IPC_PRIVATE, width * height * ((depth >> 3) + 1),
		   IPC_CREAT | 0666);

	if (si.shmid < 0) {
		_ERR("shmget");
		return NULL;
	}

	si.readOnly = False;
	si.shmaddr = shmat(si.shmid, NULL, 0);

	if (si.shmaddr == (char *)-1) {
		shmdt(si.shmaddr);
		shmctl(si.shmid, IPC_RMID, 0);
		return NULL;
	}

	xim = XShmCreateImage(ecore_x_display_get(), visual, depth, ZPixmap, NULL, &si,
			    width, height);

	if (xim == 0) {
		shmdt(si.shmaddr);
		shmctl(si.shmid, IPC_RMID, 0);

		return NULL;
	}

	img_size = xim->bytes_per_line * xim->height;
	xim->data = si.shmaddr;

	XSync(ecore_x_display_get(), False);
	XShmAttach(ecore_x_display_get(), &si);
	XShmGetImage(ecore_x_display_get(), id, xim, 0, 0, 0xFFFFFFFF);
	XSync(ecore_x_display_get(), False);

	captured_img = calloc(1, img_size);
	if (captured_img) {
		memcpy(captured_img, xim->data, img_size);
	} else {
		_ERR("calloc");
	}

	XShmDetach(ecore_x_display_get(), &si);
	XDestroyImage(xim);


	shmdt(si.shmaddr);
	shmctl(si.shmid, IPC_RMID, 0);

	*size = img_size;

	return captured_img;

}

#define _WND_REQUEST_ANGLE_IDX 0
int _get_angle(Ecore_X_Window win)
{
	int after = -1;

	do {
		int ret, count;
		int angle[2] = {-1, -1};
		unsigned char* prop_data = NULL;

		ret = ecore_x_window_prop_property_get(win,
				ECORE_X_ATOM_E_ILLUME_ROTATE_WINDOW_ANGLE,
				ECORE_X_ATOM_CARDINAL,
				32,
				&prop_data,
				&count);
		if (ret <= 0) {
			if (prop_data) free(prop_data);
			break;
		}

		if (prop_data) {
			memcpy(&angle, prop_data, sizeof (int) *count);
			free(prop_data);
		}

		after= angle[_WND_REQUEST_ANGLE_IDX];
	} while (0);

	if (-1 == after) after = 0;

	return after;
}

static void _rotate_img(Evas_Object *image_object, int angle, int cx, int cy)
{
	Evas_Map *em;

	_ret_if(NULL == image_object);

	em = evas_map_new(4);
	_ret_if(NULL == em);

	evas_map_util_points_populate_from_object(em, image_object);
	evas_map_util_rotate(em, (double) angle, cx, cy);

	evas_object_map_set(image_object, em);
	evas_object_map_enable_set(image_object, EINA_TRUE);

	evas_map_free(em);
}

#define EXTENSION_LEN 128
#define CAPTURE_FILE_PATH "/opt/usr/share/app_capture"
bool _make_capture_file(const char *package, int width, int height, char *img, int angle)
{
	int len;
	char *filename;
	Evas *e;
	Evas_Object *image_object;
	int canvas_width, canvas_height;
	int cx = 0, cy = 0;
	int mx = 0;
	int r = 0;
	char idbuf[APPID_MAX] = { 0, };

	_retv_if(NULL == package, false);
	int lpid = aul_app_group_get_leader_pid(getpid());

	_retv_if(lpid < 0, false);
	if ( AUL_R_OK != aul_app_get_appid_bypid(lpid, idbuf, APPID_MAX - 1))
		return false;

	len = strlen(idbuf) + EXTENSION_LEN;
	filename = malloc(len);
	_retv_if(NULL == filename, false);
	snprintf(filename, len, CAPTURE_FILE_PATH"/%s.jpg", idbuf);

	if (90 == angle || 270 == angle) {
		canvas_width = height;
		canvas_height = width;
	} else {
		canvas_width = width;
		canvas_height = height;
	}

	e = virtual_canvas_create(canvas_width, canvas_height);
	goto_if(NULL == e, error);

	image_object = evas_object_image_add(e);
	goto_if(NULL == image_object, error);

	evas_object_image_size_set(image_object, width, height);
	evas_object_image_data_set(image_object, img);
	evas_object_image_data_update_add(image_object, 0, 0, width, height);
	evas_object_resize(image_object, width, height);
	evas_object_image_filled_set(image_object, EINA_TRUE);
	switch (angle) {
		case 90:
			cx = canvas_width - width / 2;
			cy = canvas_height / 2;
			mx = canvas_width - width;
			break;
		case 180:
			cx = width / 2;
			cy = height / 2;
			break;
		case 270:
			cx = width / 2;
			cy = canvas_height / 2;
			break;
		default:
			break;
	}
	evas_object_move(image_object, mx, 0);
	_rotate_img(image_object, angle, cx, cy);
	evas_object_show(image_object);

	if (access(CAPTURE_FILE_PATH, F_OK) != 0) {
		r = mkdir(CAPTURE_FILE_PATH, 0777);
		goto_if(r < 0, error);
	}
	goto_if(false == virtual_canvas_flush_to_file(e, filename, canvas_width, canvas_height), error);

	evas_object_del(image_object);
	virtual_canvas_destroy(e);
	free(filename);

	return true;

error:
	do {
		free(filename);

		if (!e) break;
		virtual_canvas_destroy(e);

		if (!image_object) break;
		evas_object_del(image_object);
	} while (0);

	return false;
}

int __resize8888(const char* pDataIn, char* pDataOut, int inWidth, int inHeight, int outWidth, int outHeight)
{
	int scaleX = 0;
	int scaleY = 0;
	int i = 0;
	int j = 0;
	int iRow = 0;
	int iIndex = 0;
	char* pOutput = pDataOut;
	char* pOut = pDataOut;
	const char* pIn = NULL;
	int *pColLUT = malloc(sizeof(int) * outWidth);

	/* Calculate X Scale factor */
	scaleX = inWidth * 256 / outWidth;
	/* Calculate Y Scale factor, aspect ratio is not maintained */
	scaleY = inHeight * 256 / outHeight;
	for (j = 0; j < outWidth; j++)
	{
	/* Get input index based on column scale factor */
	/* To get more optimization, this is calculated once and
	* is placed in a LUT and used for indexing
	*/
	pColLUT [j] = ((j * scaleX) >> 8) * 4;
	}
	pOut = pOutput;
	for (i = 0; i < outHeight; i++)
	{
		/* Get input routWidth index based on routWidth scale factor */
		iRow = (i * scaleY >> 8) * inWidth * 4;
		/* Loop could be unrolled for more optimization */
		for (j = 0; j < (outWidth); j++)
		{
			/* Get input index based on column scale factor */
			iIndex = iRow + pColLUT [j];
			pIn = pDataIn + iIndex;
			*pOut++ = *pIn++;
			*pOut++ = *pIn++;
			*pOut++ = *pIn++;
			*pOut++ = *pIn++;
		}
	}

	free(pColLUT);
	return 0;
}

static void _capture_and_make_file(Ecore_X_Window win, int pid, const char *package)
{
	Visual *visual;
	Window redirected_id;

	int width, height, depth;
	int width_out, height_out;
	int size = 0;
	int angle;

	char *img;

	redirected_id = _find_capture_window(win, &visual, &depth, &width, &height);
	_ret_if(redirected_id == (Window) -1 ||
				redirected_id == (Window) 0);

	SECURE_LOGD("Capture : win[%x] -> redirected win[%x] for %s[%d]", win, redirected_id, package, pid);

	img = _capture_window(redirected_id, visual, width, height, depth, &size);
	_ret_if(NULL == img);

	width_out = width/2;
	height_out = height/2;

	if ( width_out < 1 || height_out < 1 ) {
		free(img);
		return;
	}

	__resize8888(img, img, width, height, width_out, height_out);

	angle = _get_angle(win);
	if (false == _make_capture_file(package, width_out, height_out, img, angle)) {
		_ERR("cannot a capture file for the package of [%s]", package);
	}

	free(img);
}
#endif

EXPORT_API int appcore_efl_main(const char *name, int *argc, char ***argv,
				struct appcore_ops *ops)
{
	int r;
	int pid;

	LOG(LOG_DEBUG, "LAUNCH", "[%s:Application:main:done]", name);

	pid = getpid();
	r = aul_app_get_appid_bypid(pid, appid, APPID_MAX);
	if( r < 0) {
		_ERR("aul_app_get_appid_bypid fail");
	}

	r = __set_data(&priv, name, ops);
	_retv_if(r == -1, -1);

	r = __before_loop(&priv, argc, argv);
	if (r == -1) {
		__unset_data(&priv);
		return -1;
	}

	elm_run();

	aul_status_update(STATUS_DYING);

#ifdef _APPFW_FEATURE_CAPTURE_FOR_TASK_MANAGER
	GSList *iter = NULL;
	struct win_node *entry = NULL;

	for (iter = g_winnode_list; iter != NULL; iter = g_slist_next(iter)) {
		entry = iter->data;
		if(__check_skip(entry->win) == FALSE)
			break;
	}
	if (iter) {
		entry = iter->data;
		if (taskmanage || __is_sub_app()) {
			_capture_and_make_file(entry->win, pid, appid);
		}
	}
#endif

	__after_loop(&priv);

	__unset_data(&priv);

	return 0;
}

EXPORT_API int appcore_set_system_resource_reclaiming(bool enable)
{
	resource_reclaiming = enable;

	return 0;
}

EXPORT_API int appcore_set_app_state(int state)
{
	priv.state = state;

	return 0;
}

EXPORT_API int appcore_efl_goto_pause()
{
	_DBG(" Go to Pasue state \n");
	b_active = 0;
	__do_app(AE_PAUSE, &priv, NULL);

	return 0;
}

EXPORT_API int appcore_set_prelaunching(bool value)
{
	prelaunching = value;

	return 0;
}

#ifdef _APPFW_FEATURE_PROCESS_POOL
EXPORT_API int appcore_set_preinit_window_name(const char* win_name)
{
	int ret = -1;
	void *preinit_window = NULL;

	if(!win_name) {
		_ERR("invalid input param");
		return -1;
	}

	preinit_window = aul_get_preinit_window(win_name);
	if(!preinit_window) {
		_ERR("no preinit window");
		return -1;
	}

	const Evas *e = evas_object_evas_get((const Evas_Object *)preinit_window);
	if(e) {
		Ecore_Evas *ee = ecore_evas_ecore_evas_get(e);
		if(ee) {
			ecore_evas_name_class_set(ee, win_name, win_name);
			_DBG("name class set success : %s", win_name);
			ret = 0;
		}
	}

	return ret;
}
#endif

EXPORT_API unsigned int appcore_get_main_window()
{
	struct win_node *entry = NULL;

	if (g_winnode_list != NULL) {
		entry = g_winnode_list->data;
		return (unsigned int) entry->win;
	}
	return 0;
}
