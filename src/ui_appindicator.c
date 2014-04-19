/*
 * Copyright (C) 2010-2014 jeanfi@gmail.com
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301 USA
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <gtk/gtk.h>
#include <libappindicator/app-indicator.h>

#include "cfg.h"
#include "psensor.h"
#include "ui.h"
#include "ui_appindicator.h"
#include "ui_sensorpref.h"
#include "ui_status.h"
#include "ui_pref.h"

static const char *ICON = "psensor_normal";
static const char *ATTENTION_ICON = "psensor_hot";

static struct psensor **sensors;
static GtkMenuItem **menu_items;
static int appindicator_supported = 1;
static AppIndicator *indicator;
static struct ui_psensor *ui_psensor;

void cb_menu_show(GtkMenuItem *mi, gpointer data)
{
	ui_window_show((struct ui_psensor *)data);
}

void ui_appindicator_cb_preferences(GtkMenuItem *mi, gpointer data)
{
#ifdef HAVE_APPINDICATOR_029
	gdk_threads_enter();
#endif

	ui_pref_dialog_run((struct ui_psensor *)data);

#ifdef HAVE_APPINDICATOR_029
	gdk_threads_leave();
#endif
}

void ui_appindicator_cb_sensor_preferences(GtkMenuItem *mi, gpointer data)
{
	struct ui_psensor *ui = data;

#ifdef HAVE_APPINDICATOR_029
	gdk_threads_enter();
#endif

	if (ui->sensors && *ui->sensors)
		ui_sensorpref_dialog_run(*ui->sensors, ui);

#ifdef HAVE_APPINDICATOR_029
	gdk_threads_leave();
#endif
}

static void
update_menu_item(GtkMenuItem *item, struct psensor *s, int use_celcius)
{
	gchar *str;
	char *v;

	v = psensor_current_value_to_str(s, use_celcius);

	str = g_strdup_printf("%s: %s", s->name, v);

	gtk_menu_item_set_label(item, str);

	free(v);
	g_free(str);
}

static void update_menu_items(int use_celcius)
{
	struct psensor **s;
	GtkMenuItem **m;

	if (!sensors)
		return ;

	for (s = sensors, m = menu_items; *s; s++, m++)
		update_menu_item(*m, *s, use_celcius);
}

static void
build_sensor_menu_items(const struct ui_psensor *ui,
			GtkMenu *menu)
{
	int i, j, n, celcius;
	const char *name;
	struct psensor **sorted_sensors;

	free(menu_items);

	celcius  = ui->config->temperature_unit == CELCIUS;

	sorted_sensors = ui_get_sensors_ordered_by_position(ui);
	n = psensor_list_size(sorted_sensors);
	menu_items = malloc(n * sizeof(GtkWidget *));
	sensors = malloc((n + 1) * sizeof(struct psensor *));
	for (i = 0, j = 0; i < n; i++) {
		if (config_is_appindicator_enabled(sorted_sensors[i]->id)) {
			sensors[j] = sorted_sensors[i];
			name = sensors[j]->name;

			menu_items[j] = GTK_MENU_ITEM
				(gtk_menu_item_new_with_label(name));

			gtk_menu_shell_insert(GTK_MENU_SHELL(menu),
					      GTK_WIDGET(menu_items[j]),
					      j+2);

			update_menu_item(menu_items[j], sensors[j], celcius);

			j++;
		}
	}

	sensors[j] = NULL;

	free(sorted_sensors);
}

static GtkWidget *get_menu(struct ui_psensor *ui)
{
	GError *error;
	GtkMenu *menu;
	guint ok;
	GtkBuilder *builder;

	builder = gtk_builder_new();

	error = NULL;
	ok = gtk_builder_add_from_file
		(builder,
		 PACKAGE_DATA_DIR G_DIR_SEPARATOR_S "psensor.glade",
		 &error);

	if (!ok) {
		log_printf(LOG_ERR, error->message);
		g_error_free(error);
		return NULL;
	}

	menu = GTK_MENU(gtk_builder_get_object(builder, "appindicator_menu"));
	build_sensor_menu_items(ui, menu);
	gtk_builder_connect_signals(builder, ui);

	g_object_ref(G_OBJECT(menu));
	g_object_unref(G_OBJECT(builder));

	return GTK_WIDGET(menu);
}

void ui_appindicator_update(struct ui_psensor *ui, unsigned int attention)
{
	AppIndicatorStatus status;
	char *label, *str, *tmp;
	struct psensor **p;

	if (!indicator)
		return;

	status = app_indicator_get_status(indicator);

	p =  ui_get_sensors_ordered_by_position(ui);
	label = NULL;
	while (*p) {
		if (config_is_appindicator_label_enabled((*p)->id)) {
			str = psensor_current_value_to_str
				(*p, ui->config->temperature_unit == CELCIUS);
			if (label == NULL) {
				label = str;
			} else {
				tmp = malloc(strlen(label)
					     + 1
					     + strlen(str)
					     + 1);
				sprintf(tmp, "%s %s", label, str);
				free(label);
				free(str);
				label = tmp;
			}
		}
		p++;
	}

	app_indicator_set_label(indicator, label, NULL);

	if (!attention && status == APP_INDICATOR_STATUS_ATTENTION)
		app_indicator_set_status(indicator,
					 APP_INDICATOR_STATUS_ACTIVE);

	if (attention && status == APP_INDICATOR_STATUS_ACTIVE)
		app_indicator_set_status(indicator,
					 APP_INDICATOR_STATUS_ATTENTION);

	update_menu_items(ui->config->temperature_unit == CELCIUS);
}

static GtkStatusIcon *unity_fallback(AppIndicator *indicator)
{
	GtkStatusIcon *ico;

	log_debug("ui_appindicator.unity_fallback()");

	appindicator_supported = 0;

	ico = ui_status_get_icon(ui_psensor);

	ui_status_set_visible(1);

	return ico;
}

static void
unity_unfallback(AppIndicator *indicator, GtkStatusIcon *status_icon)
{
	log_debug("ui_appindicator.unity_unfallback()");

	ui_status_set_visible(0);

	appindicator_supported = 1;
}

void ui_appindicator_update_menu(struct ui_psensor *ui)
{
	GtkWidget *menu;

	menu = get_menu(ui);
	app_indicator_set_menu(indicator, GTK_MENU(menu));

	gtk_widget_show_all(menu);
}

void ui_appindicator_init(struct ui_psensor *ui)
{
	ui_psensor = ui;

	indicator = app_indicator_new
		("psensor",
		 ICON,
		 APP_INDICATOR_CATEGORY_APPLICATION_STATUS);

	APP_INDICATOR_GET_CLASS(indicator)->fallback = unity_fallback;
	APP_INDICATOR_GET_CLASS(indicator)->unfallback = unity_unfallback;

	app_indicator_set_status(indicator, APP_INDICATOR_STATUS_ACTIVE);
	app_indicator_set_attention_icon(indicator, ATTENTION_ICON);

	ui_appindicator_update_menu(ui);
}

int is_appindicator_supported()
{
	return appindicator_supported;
}

void ui_appindicator_cleanup()
{
	free(sensors);
}
