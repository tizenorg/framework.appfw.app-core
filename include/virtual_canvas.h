/*
 *  starter
 *
 * Copyright (c) 2000 - 2011 Samsung Electronics Co., Ltd. All rights reserved.
 *
 * Contact: Seungtaek Chung <seungtaek.chung@samsung.com>, Mi-Ju Lee <miju52.lee@samsung.com>, Xi Zhichan <zhichan.xi@samsung.com>
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

#ifndef __STARTER_VIRTUAL_CANVAS_H__
#define __STARTER_VIRTUAL_CANVAS_H__

#include <Evas.h>
#include <stdbool.h>

extern Evas *virtual_canvas_create(int w, int h);
extern bool virtual_canvas_flush_to_file(Evas *e, const char *filename, int w, int h);
extern bool virtual_canvas_destroy(Evas *e);

#endif //__STARTER_VIRTUAL_CANVAS_H__

// End of a file
