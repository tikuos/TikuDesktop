/* tiku_docview_app.c -- TikuDoc: Word documents and OpenDocument text
 * read as the pages they lay out to, and exported as PDFs, by the office
 * viewer's descriptor in apps/office.
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * SPDX-License-Identifier: Apache-2.0 */
#include "tiku_officeview.h"

#define OV_APP_ID     "doc"
#define OV_APP_NAME   "TikuDoc"
#define OV_APP_SLIDES 0
#define OV_APP_HELP   "apps/doc.html"
#include "tiku_officeview_app.inl"
