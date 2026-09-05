/*
 * TikuGUI -- a board's interface on the desktop.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * panel_demo.c - the sample form: a gauge a timer moves, a toggle on the
 * first LED, a field, and a button that counts.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_gui.h"
#include <interfaces/led/tiku_led.h>
#include <kernel/timers/tiku_timer.h>

#include <stdio.h>

TIKU_PROCESS(gui_demo_process, "gui-demo");

static tiku_gui_form_t demo;
static struct tiku_timer demo_timer;
static tiku_gui_row_t *temp_row;
static int temp, lamp;
static char name[32] = "workshop";
static char status[32] = "lamp off";

/**
 * @brief The toggle drives the LED from the process's own thread, and
 *        says so in the status row: proof the event reached the process.
 */
static void
lamp_changed(tiku_gui_row_t *row)
{
    if (*row->value) {
        tiku_led_on(0);
    } else {
        tiku_led_off(0);
    }
    snprintf(status, sizeof status, "lamp %s", *row->value ? "on" : "off");
    tiku_gui_changed(row);
}

TIKU_PROCESS_THREAD(gui_demo_process, ev, data)
{
    TIKU_PROCESS_BEGIN();

    /* Autostart runs the thread; a pid is what ps and kill go by. */
    (void)tiku_process_register("gui-demo", &gui_demo_process);
    tiku_led_init(0);
    tiku_gui_form_init(&demo, "demo", "Board one", &gui_demo_process);
    (void)tiku_gui_text(&demo, "note", "Sensors and switches");
    temp_row = tiku_gui_gauge(&demo, "temp", "Temperature", &temp, 0, 100);
    (void)tiku_gui_toggle(&demo, "lamp", "Lamp", &lamp, lamp_changed);
    (void)tiku_gui_field(&demo, "name", "Name", name, sizeof name, NULL);
    (void)tiku_gui_button(&demo, "count", "Count", NULL);
    (void)tiku_gui_text(&demo, "status", status);
    (void)tiku_gui_add(&demo);
    tiku_timer_set_event(&demo_timer, TIKU_CLOCK_SECOND);

    while (1) {
        TIKU_PROCESS_WAIT_EVENT();
        if (ev == TIKU_EVENT_TIMER) {
            /* A sawtooth, so the gauge is seen to move. */
            temp = (temp + 7) % 101;
            tiku_gui_changed(temp_row);
            tiku_timer_reset(&demo_timer);
        } else {
            (void)tiku_gui_event(ev, data);
        }
    }

    TIKU_PROCESS_END();
}

TIKU_AUTOSTART_PROCESSES(&gui_demo_process);
