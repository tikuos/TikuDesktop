/* tiku_slidesview_app.c -- TikuSlides: PowerPoint presentations shown a
 * slide at a time, and presented, by the office viewer's descriptor in
 * apps/office.
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * SPDX-License-Identifier: Apache-2.0 */
#include "tiku_officeview.h"

#define OV_APP_ID     "slides"
#define OV_APP_NAME   "TikuSlides"
#define OV_APP_SLIDES 1
#define OV_APP_HELP   "apps/slides.html"
#include "tiku_officeview_app.inl"
