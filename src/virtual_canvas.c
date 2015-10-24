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

#include <Elementary.h>
#include <Ecore_Evas.h>
#include <Ecore_X.h>

#include "appcore-internal.h"
#include "virtual_canvas.h"


#define QUALITY_N_COMPRESS "quality=100 compress=1"



Evas *virtual_canvas_create(int w, int h)
{
	Ecore_Evas *internal_ee;
	Evas *internal_e;

	// Create virtual canvas
	internal_ee = ecore_evas_buffer_new(w, h);
	if (!internal_ee) {
		_DBG("Failed to create a new canvas buffer\n");
		return NULL;
	}

	ecore_evas_alpha_set(internal_ee, EINA_TRUE);
	ecore_evas_manual_render_set(internal_ee, EINA_TRUE);

	// Get the "Evas" object from a virtual canvas
	internal_e = ecore_evas_get(internal_ee);
	if (!internal_e) {
		ecore_evas_free(internal_ee);
		_DBG("Faield to get Evas object\n");
		return NULL;
	}

	return internal_e;
}



static bool _flush_data_to_file(Evas *e, char *data, const char *filename, int w, int h)
{
	Evas_Object *output;

	output = evas_object_image_add(e);
	if (!output) {
		_DBG("Failed to create an image object\n");
		return false;
	}

	evas_object_image_data_set(output, NULL);
	evas_object_image_colorspace_set(output, EVAS_COLORSPACE_ARGB8888);
	evas_object_image_alpha_set(output, EINA_TRUE);
	evas_object_image_size_set(output, w, h);
	evas_object_image_smooth_scale_set(output, EINA_TRUE);
	evas_object_image_data_set(output, data);
	evas_object_image_data_update_add(output, 0, 0, w, h);

	if (evas_object_image_save(output, filename, NULL, QUALITY_N_COMPRESS) == EINA_FALSE) {
		evas_object_del(output);
		SECURE_LOGD("Faild to save a captured image (%s)\n", filename);
		return false;
	}

	evas_object_del(output);

	if (access(filename, F_OK) != 0) {
		SECURE_LOGD("File %s is not found\n", filename);
		return false;
	}

	return true;
}



bool virtual_canvas_flush_to_file(Evas *e, const char *filename, int w, int h)
{
	void *data;
	Ecore_Evas *internal_ee;

	internal_ee = ecore_evas_ecore_evas_get(e);
	if (!internal_ee) {
		_DBG("Failed to get ecore evas\n");
		return false;
	}

	ecore_evas_manual_render(internal_ee);

	// Get a pointer of a buffer of the virtual canvas
	data = (void *) ecore_evas_buffer_pixels_get(internal_ee);
	if (!data) {
		_DBG("Failed to get pixel data\n");
		return false;
	}

	return _flush_data_to_file(e, data, filename, w, h);
}



bool virtual_canvas_destroy(Evas *e)
{
	Ecore_Evas *ee;

	ee = ecore_evas_ecore_evas_get(e);
	if (!ee) {
		_DBG("Failed to ecore evas object\n");
		return false;
	}

	ecore_evas_free(ee);
	return true;
}



// End of a file
