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


#include <locale.h>
#include <libintl.h>
#include <stdlib.h>
#include <errno.h>

#include <vconf.h>
#include <iniparser.h>

#include "appcore-internal.h"

static int _set;
static char* current_lang = NULL;

static void __load_lang_info_for_fallback_translated_msg(char *lang)
{
	if(!lang) {
		_ERR("lang value is null");
		return;
	}

	if(current_lang) {
		free(current_lang);
		current_lang = NULL;
	}
	current_lang = strdup(lang);

	return;
}

void update_lang(void)
{
	char *lang = vconf_get_str(VCONFKEY_LANGSET);
	if (lang) {
		setenv("LANG", lang, 1);
		setenv("LC_MESSAGES", lang, 1);
		char *r = setlocale(LC_ALL, "");
		if (r == NULL) {
			r = setlocale(LC_ALL, lang);
			if (r) {
				_DBG("*****appcore setlocale=%s\n", r);
			}
		}

		__load_lang_info_for_fallback_translated_msg(lang);

		free(lang);
	} else {
		_ERR("failed to get current language for set lang env");
	}
}

void update_region(void)
{
	char *region;
	char *r;

	region = vconf_get_str(VCONFKEY_REGIONFORMAT);
	if (region) {
		setenv("LC_CTYPE", region, 1);
		setenv("LC_NUMERIC", region, 1);
		setenv("LC_TIME", region, 1);
		setenv("LC_COLLATE", region, 1);
		setenv("LC_MONETARY", region, 1);
		setenv("LC_PAPER", region, 1);
		setenv("LC_NAME", region, 1);
		setenv("LC_ADDRESS", region, 1);
		setenv("LC_TELEPHONE", region, 1);
		setenv("LC_MEASUREMENT", region, 1);
		setenv("LC_IDENTIFICATION", region, 1);
		r = setlocale(LC_ALL, "");
		if (r != NULL) {
			_DBG("*****appcore setlocale=%s\n", r);
		}
		free(region);
	} else {
		_ERR("failed to get current region format for set region env");
	}
}

static int __set_i18n(const char *domain, const char *dir)
{
	char *r;

	if (domain == NULL) {
		errno = EINVAL;
		return -1;
	}

	r = setlocale(LC_ALL, "");
	/* if locale is not set properly, try again to set as language base */
	if (r == NULL) {
		char *lang = vconf_get_str(VCONFKEY_LANGSET);
		r = setlocale(LC_ALL, lang);
		if (r) {
			_DBG("*****appcore setlocale=%s\n", r);
		}
		if (lang) {
			free(lang);
		}
	}
	if (r == NULL) {
		_ERR("appcore: setlocale() error");
	}
	//_retvm_if(r == NULL, -1, "appcore: setlocale() error");

	r = bindtextdomain(domain, dir);
	if (r == NULL) {
		_ERR("appcore: bindtextdomain() error");
	}
	//_retvm_if(r == NULL, -1, "appcore: bindtextdomain() error");

	r = textdomain(domain);
	if (r == NULL) {
		_ERR("appcore: textdomain() error");
	}
	//_retvm_if(r == NULL, -1, "appcore: textdomain() error");

	return 0;
}


EXPORT_API int appcore_set_i18n(const char *domainname, const char *dirname)
{
	int r;

	update_lang();
	update_region();

	r = __set_i18n(domainname, dirname);
	if (r == 0)
		_set = 1;

	return r;
}

int set_i18n(const char *domainname, const char *dirname)
{
	_retv_if(_set, 0);

	update_lang();
	update_region();

	return __set_i18n(domainname, dirname);
}

EXPORT_API int appcore_get_timeformat(enum appcore_time_format *timeformat)
{
	int r;

	if (timeformat == NULL) {
		_ERR("timeformat is null");
		errno = EINVAL;
		return -1;
	}

	r = vconf_get_int(VCONFKEY_REGIONFORMAT_TIME1224, (int *)timeformat);

	if (r < 0) {
		*timeformat = APPCORE_TIME_FORMAT_UNKNOWN;
		return -1;
	} else
		return 0;
}

EXPORT_API char *appcore_get_i18n_text(const char *msgid)
{
	char *cur_env = NULL;
	char *get_env = NULL;
	char *translated_msg = NULL;

	if(!msgid) {
		_ERR("msgid is null");
		errno = EINVAL;
		return NULL;
	}

	// get msg based on current locale env
	translated_msg = gettext(msgid);
	if(strncmp(msgid, translated_msg, strlen(msgid)) != 0) {
		goto func_out;
	}

	// backup current LC_MESSAGES locale value
	get_env = setlocale(LC_MESSAGES, NULL);
	if(get_env) {
		cur_env = strdup(get_env);
	}

	// Fallback #1 - get msg based on current language setting value
	if(current_lang) {
		setlocale(LC_MESSAGES, current_lang);
		translated_msg = gettext(msgid);
		if(strncmp(msgid, translated_msg, strlen(msgid)) != 0) {
			goto func_out;
		}
	}

func_out :
	if(cur_env) {
		setlocale(LC_MESSAGES, cur_env);
		free(cur_env);
	}
	return translated_msg;
}
