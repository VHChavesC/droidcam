/* DroidCam & DroidCamX (C) 2010-
 * https://github.com/aramg
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 */

#include <string.h>
#include <sys/types.h>
#include <gtk/gtk.h>
#include <X11/Xlib.h>

#include "common.h"
#include "settings.h"
#include "connection.h"
#include "decoder.h"
#include "icon.h"

/* Globals */
GtkWidget *menu;
GtkWidget *infoText;
GtkWidget *audioCheckbox;
GtkEntry * ipEntry;
GtkEntry * portEntry;
GtkButton *start_button;
GThread* hVideoThread;
GThread* hAudioThread;

int a_running = 0;
int v_running = 0;
int thread_cmd = 0;
struct settings g_settings = {0};

extern char snd_device[32];
extern char v4l2_device[32];

void * AudioThreadProc(void * args);
void * VideoThreadProc(void * args);

/* Helper Functions */
void ShowError(const char * title, const char * msg)
{
	GtkWidget *dialog = gtk_message_dialog_new(NULL, GTK_DIALOG_DESTROY_WITH_PARENT, GTK_MESSAGE_ERROR, GTK_BUTTONS_OK, "%s", msg);
	gtk_window_set_title(GTK_WINDOW(dialog), title);
	gtk_dialog_run(GTK_DIALOG(dialog));
	gtk_widget_destroy(dialog);
}

static void Stop(void)
{
	a_running = 0;
	v_running = 0;
	dbgprint("join\n");
	if (hVideoThread) {
		g_thread_join(hVideoThread);
		hVideoThread = NULL;
	}
	if (hAudioThread) {
		g_thread_join(hAudioThread);
		hAudioThread = NULL;
	}
}

static void Start(void)
{
	char * ip = NULL;
	SOCKET s = INVALID_SOCKET;
	int port = atoi(gtk_entry_get_text(portEntry));

	if (g_settings.connection == CB_RADIO_ADB) {
		if (CheckAdbDevices(port) != 8) return;
		ip = "127.0.0.1";
	} else if (g_settings.connection == CB_RADIO_WIFI) {
		ip = (char*)gtk_entry_get_text(ipEntry);
	}

	// wifi or USB
	if (ip != NULL) {
		if (strlen(ip) < 7 || port <= 0 || port > 65535) {
			MSG_ERROR("You must enter the correct IP address (and port) to connect to.");
			return;
		}

		gtk_button_set_label(start_button, "Please wait");
		s = connect_droidcam(ip, port);
		if (s == INVALID_SOCKET) {
			gtk_button_set_label(start_button, "Connect");
			return;
		}
		strncpy(g_settings.ip, ip, sizeof(g_settings.ip));
	}
	g_settings.port = port;

	hVideoThread = g_thread_new(NULL, VideoThreadProc, (void*) (SOCKET_PTR) s);
	if (s != INVALID_SOCKET && g_settings.audio) {
		a_running = 1;
		hAudioThread = g_thread_new(NULL, AudioThreadProc, NULL);
	}
	gtk_button_set_label(start_button, "Stop");
	gtk_widget_set_sensitive(GTK_WIDGET(ipEntry), FALSE);
	gtk_widget_set_sensitive(GTK_WIDGET(portEntry), FALSE);
	gtk_widget_set_sensitive(GTK_WIDGET(audioCheckbox), FALSE);
	SaveSettings(&g_settings);
}

/* Messages */
static gboolean
accel_callback( GtkAccelGroup  *group,
		  GObject		*obj,
		  guint		   keyval,
		  GdkModifierType mod,
		  gpointer		user_data)
{
	if(v_running == 1 && thread_cmd ==0){
		thread_cmd = (uintptr_t) user_data;
	}
	return TRUE;
}

static void the_callback(GtkWidget* widget, gpointer extra)
{
	int cb = (uintptr_t) extra;
	gboolean ipEdit = TRUE;
	gboolean portEdit = TRUE;
	gboolean audioBox = TRUE;
	char * text = NULL;

_up:
	dbgprint("the_cb=%d\n", cb);
	switch (cb) {
		case CB_BUTTON:
			if (v_running || a_running) {
				Stop();
				cb = (int)g_settings.connection;
				goto _up;
			}
#if 1
			Start();
#else
			decoder_show_test_image();
#endif
		break;
		case CB_WIFI_SRVR:
			g_settings.connection = CB_WIFI_SRVR;
			text = "Prepare";
			ipEdit = FALSE;
			audioBox = FALSE;
		break;
		case CB_RADIO_WIFI:
			g_settings.connection = CB_RADIO_WIFI;
			text = "Connect";
		break;
		case CB_RADIO_ADB:
			g_settings.connection = CB_RADIO_ADB;
			text = "Connect";
			ipEdit = FALSE;
		break;
		case CB_BTN_OTR:
			gtk_menu_popup_at_pointer(GTK_MENU(menu), NULL);
		break;
		case CB_CONTROL_ZIN  :
		case CB_CONTROL_ZOUT :
		case CB_CONTROL_AF   :
		case CB_CONTROL_LED  :
		if(v_running == 1 && thread_cmd ==0){
			thread_cmd =  cb - 10;
		}
		break;
		case CB_AUDIO:
			g_settings.audio = (int) gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(audioCheckbox));
			dbgprint("audio=%d\n", g_settings.audio);
		break;
	}

	if (text != NULL && v_running == 0){
		gtk_button_set_label(start_button, text);
		gtk_widget_set_sensitive(GTK_WIDGET(ipEntry), ipEdit);
		gtk_widget_set_sensitive(GTK_WIDGET(portEntry), portEdit);
		gtk_widget_set_sensitive(GTK_WIDGET(audioCheckbox), audioBox);
	}
}

int main(int argc, char *argv[])
{
	char info[128];
	char port[16];
	GtkWidget *window;
	GtkWidget *grid;
	GtkWidget *radioGroup;
	GtkWidget *menuGrid;
	GtkWidget *radio1, *radio2;
	GtkWidget *radios[CB_RADIO_COUNT];
	GtkWidget *widget; // generic stuff

	// init threads
	XInitThreads();
	gtk_init(&argc, &argv);

	window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
	gtk_window_set_title(GTK_WINDOW(window), "DroidCam Client");
	gtk_container_set_border_width(GTK_CONTAINER(window), 1);
	gtk_window_set_resizable(GTK_WINDOW(window), FALSE);
	gtk_window_set_position(GTK_WINDOW(window), GTK_WIN_POS_NONE);
	gtk_container_set_border_width(GTK_CONTAINER(window), 10);
//	gtk_widget_set_size_request(window, 250, 120);
	gtk_window_set_icon(GTK_WINDOW(window), gdk_pixbuf_new_from_resource("/com/dev47apps/droidcam/icon.png", NULL));

 {
	GtkAccelGroup *gtk_accel = gtk_accel_group_new ();
	GClosure *closure = g_cclosure_new(G_CALLBACK(accel_callback), (gpointer)(CB_CONTROL_AF-10), NULL);
	gtk_accel_group_connect(gtk_accel, gdk_keyval_from_name("a"), GDK_CONTROL_MASK, GTK_ACCEL_VISIBLE, closure);

	closure = g_cclosure_new(G_CALLBACK(accel_callback), (gpointer)(CB_CONTROL_LED-10), NULL);
	gtk_accel_group_connect(gtk_accel, gdk_keyval_from_name("l"), GDK_CONTROL_MASK, GTK_ACCEL_VISIBLE, closure);

	closure = g_cclosure_new(G_CALLBACK(accel_callback), (gpointer)(CB_CONTROL_ZOUT-10), NULL);
	gtk_accel_group_connect(gtk_accel, gdk_keyval_from_name("minus"), 0, GTK_ACCEL_VISIBLE, closure);

	closure = g_cclosure_new(G_CALLBACK(accel_callback), (gpointer)(CB_CONTROL_ZIN-10), NULL);
	gtk_accel_group_connect(gtk_accel, gdk_keyval_from_name("plus"), 0, GTK_ACCEL_VISIBLE, closure);

	closure = g_cclosure_new(G_CALLBACK(accel_callback), (gpointer)(CB_CONTROL_ZIN-10), NULL);
	gtk_accel_group_connect(gtk_accel, gdk_keyval_from_name("equal"), 0, GTK_ACCEL_VISIBLE, closure);

	gtk_window_add_accel_group(GTK_WINDOW(window), gtk_accel);
 }
	menu = gtk_menu_new();

	widget = gtk_menu_item_new_with_label("DroidCamX Commands:");
	gtk_menu_shell_append (GTK_MENU_SHELL(menu), widget);
	gtk_widget_show (widget);
	gtk_widget_set_sensitive(widget, 0);

	widget = gtk_menu_item_new_with_label("Auto-Focus (Ctrl+A)");
	gtk_menu_shell_append (GTK_MENU_SHELL(menu), widget);
	gtk_widget_show (widget);
	g_signal_connect(widget, "activate", G_CALLBACK(the_callback), (gpointer)CB_CONTROL_AF);

	widget = gtk_menu_item_new_with_label("Toggle LED Flash (Ctrl+L)");
	gtk_menu_shell_append (GTK_MENU_SHELL(menu), widget);
	gtk_widget_show (widget);
	g_signal_connect(widget, "activate", G_CALLBACK(the_callback), (gpointer)CB_CONTROL_LED);

	widget = gtk_menu_item_new_with_label("Zoom In (+)");
	gtk_menu_shell_append (GTK_MENU_SHELL(menu), widget);
	gtk_widget_show (widget);
	g_signal_connect(widget, "activate", G_CALLBACK(the_callback), (gpointer)CB_CONTROL_ZIN);

	widget = gtk_menu_item_new_with_label("Zoom Out (-)");
	gtk_menu_shell_append (GTK_MENU_SHELL(menu), widget);
	gtk_widget_show (widget);
	g_signal_connect(widget, "activate", G_CALLBACK(the_callback), (gpointer)CB_CONTROL_ZOUT);

	// Create main grid to create left and right column of the UI.
	// +-----------------------------------+
	// |---------------+    +--------------|
	// ||RadioGroup    |    |Input field  ||
	// ||              |    |Input field  ||
	// ||Toggle Audio  |    |      Connect||
	// ||Menu          |    |             ||
	// ||              |    |             ||
	// ||Info          |    |             ||
	// |---------------+    +--------------|
	// +-----------------------------------+
	grid = gtk_grid_new();

	// Add created grid to main window.
	gtk_container_add(GTK_CONTAINER(window), grid);

	// Columns and rows should be separated a bit.
	gtk_grid_set_column_spacing(GTK_GRID(grid), 25);
	gtk_grid_set_row_spacing(GTK_GRID(grid), 5);

	// Create grid for radio buttons, so they are easy to distinguish from the rest
	// the elements.
	radioGroup = gtk_grid_new();

	// Put radio group as first element of left column.
	gtk_grid_attach(GTK_GRID(grid), radioGroup, 0, 0, 1, 3);

	// Create radio options.
	radio1 = gtk_radio_button_new_with_label(NULL, "WiFi / LAN");
	g_signal_connect(radio1, "toggled", G_CALLBACK(the_callback), (gpointer)CB_RADIO_WIFI);
	gtk_grid_attach(GTK_GRID(radioGroup), radio1, 0, 0, 1, 1);
	radios[CB_RADIO_WIFI] = radio1;

	radio2 = gtk_radio_button_new_with_label_from_widget(GTK_RADIO_BUTTON(radio1), "Wifi Server Mode");
	g_signal_connect(radio2, "toggled", G_CALLBACK(the_callback), (gpointer)CB_WIFI_SRVR);
	gtk_grid_attach_next_to(GTK_GRID(radioGroup), radio2, radio1, GTK_POS_BOTTOM, 1, 1);
	radios[CB_WIFI_SRVR] = radio2;

	radio1 = gtk_radio_button_new_with_label_from_widget(GTK_RADIO_BUTTON(radio2), "USB (over adb)");
	g_signal_connect(radio1, "toggled", G_CALLBACK(the_callback), (gpointer)CB_RADIO_ADB);
	gtk_grid_attach_next_to(GTK_GRID(radioGroup), radio1, radio2, GTK_POS_BOTTOM, 1, 1);
	radios[CB_RADIO_ADB] = radio1;

	// Add toggle button to enable audio as 2nd element of left column.
	widget = gtk_check_button_new_with_label("Enable Audio");
	g_signal_connect(widget, "toggled", G_CALLBACK(the_callback), (gpointer)CB_AUDIO);
	gtk_grid_attach(GTK_GRID(grid), widget, 0, 3, 1, 1);
	audioCheckbox = widget;

	// Menu button goes third to the left column.
	widget = gtk_button_new_with_label("...");
	g_signal_connect(widget, "clicked", G_CALLBACK(the_callback), (gpointer)CB_BTN_OTR);

	// Put menu button in the grid, so it's not full column width, but smaller.
	menuGrid = gtk_grid_new();
	gtk_grid_attach(GTK_GRID(menuGrid), widget, 0, 0, 1, 1);
	gtk_grid_attach(GTK_GRID(grid), menuGrid, 0, 4, 1, 1);

	// Info text goes as last element of left column.
	infoText = gtk_label_new(NULL);
	gtk_grid_attach(GTK_GRID(grid), infoText, 0, 5, 1, 1);

	// Phone IP label.
	widget = gtk_label_new("Phone IP:");
	gtk_label_set_xalign(GTK_LABEL(widget), 0.0);
	gtk_grid_attach(GTK_GRID(grid), widget, 1, 0, 1, 1);

	// And input field for phone IP.
	widget = gtk_entry_new();
	gtk_entry_set_max_length(GTK_ENTRY(widget), 16);
	ipEntry = (GtkEntry*)widget;
	gtk_grid_attach(GTK_GRID(grid), widget, 2, 0, 1, 1);

	// Port label.
	widget = gtk_label_new("DroidCam Port:");
	gtk_label_set_xalign (GTK_LABEL(widget), 0.0);
	gtk_grid_attach(GTK_GRID(grid), widget, 1, 1, 1, 1);

	// Port input field.
	widget = gtk_entry_new();
	gtk_entry_set_max_length(GTK_ENTRY(widget), 5);
	portEntry = (GtkEntry*)widget;
	gtk_grid_attach(GTK_GRID(grid), widget, 2, 1, 1, 1);

	// And finally connect button.
	widget = gtk_button_new_with_label("Connect");
	g_signal_connect(widget, "clicked", G_CALLBACK(the_callback), (gpointer) CB_BUTTON);
	start_button = (GtkButton*)widget;
	gtk_grid_attach(GTK_GRID(grid), widget, 2, 2, 1, 1);

	g_signal_connect(window, "destroy", G_CALLBACK (gtk_main_quit), NULL);
	gtk_widget_show_all(window);

	LoadSettings(&g_settings);
	snprintf(port, sizeof(port), "%d", g_settings.port);
	gtk_entry_set_text(ipEntry, g_settings.ip);
	gtk_entry_set_text(portEntry, port);

	if (g_settings.connection < CB_RADIO_COUNT)
		gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(radios[g_settings.connection]), TRUE);

	if (g_settings.audio)
		gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(audioCheckbox), TRUE);

	if ( decoder_init() )
	{
		// add info about devices
		snprintf(info, sizeof(info), "Client v" APP_VER_STR ", Video: %s, Audio: %s",
			v4l2_device, snd_device);
		gtk_label_set_text(GTK_LABEL(infoText), info);

		// set the font size
		PangoAttrList *attrlist = pango_attr_list_new();
		PangoAttribute *attr = pango_attr_size_new_absolute(12 * PANGO_SCALE);
		pango_attr_list_insert(attrlist, attr);
		gtk_label_set_attributes(GTK_LABEL(infoText), attrlist);
		pango_attr_list_unref(attrlist);

		// main loop
		gtk_main();
		Stop();
		decoder_fini();
		connection_cleanup();
	}

	return 0;
}
