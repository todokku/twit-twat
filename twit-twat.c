/*
 * Twit-Twat
 *
 * Copyright (C) 2017 Florian Zwoch <fzwoch@gmail.com>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#include <gtk/gtk.h>
#include <gst/gst.h>
#include <gst/video/videooverlay.h>
#include <libsoup/soup.h>
#include <json-glib/json-glib.h>
#include <gdk/gdkx.h>
#include <gdk/gdkwayland.h>

static GtkApplication* app;
static GtkWidget* window;
static GtkWidget* area;
static GstElement* playbin = NULL;
static GString* channel;
static const gchar* client_id = "7ikopbkspr7556owm9krqmalvr2w0i4";
static gint connection_speed = 0;

static GstBusSyncReply overlay(GstBus* bus, GstMessage* message, gpointer user_data)
{
  if (!gst_is_video_overlay_prepare_window_handle_message(message))
    return GST_BUS_PASS;

  if (GDK_IS_X11_DISPLAY(gdk_window_get_display(gtk_widget_get_window(area))))
    gst_video_overlay_set_window_handle(GST_VIDEO_OVERLAY(GST_MESSAGE_SRC(message)), GDK_WINDOW_XID(gtk_widget_get_window(area)));
  else if (GDK_IS_WAYLAND_DISPLAY(gdk_window_get_display(gtk_widget_get_window(area))))
    gst_video_overlay_set_window_handle(GST_VIDEO_OVERLAY(GST_MESSAGE_SRC(message)), (guintptr)gdk_wayland_window_get_wl_surface(gtk_widget_get_window(window)));

  return GST_BUS_DROP;
}

static gboolean bus_watch(GstBus* bus, GstMessage* message, gpointer user_data)
{
  switch (message->type) {
    case GST_MESSAGE_EOS:
    {
      gst_element_set_state(playbin, GST_STATE_NULL);
      GtkWidget* dialog = gtk_message_dialog_new(GTK_WINDOW(window), GTK_DIALOG_DESTROY_WITH_PARENT, GTK_MESSAGE_INFO, GTK_BUTTONS_CLOSE, "Broadcast finished");
      gtk_dialog_run(GTK_DIALOG(dialog));
      gtk_widget_destroy(dialog);
      break;
    }
    case GST_MESSAGE_ERROR:
    {
      GError* err;
      gst_message_parse_error(message, &err, NULL);
      GtkWidget* dialog = gtk_message_dialog_new(GTK_WINDOW(window), GTK_DIALOG_DESTROY_WITH_PARENT, GTK_MESSAGE_ERROR, GTK_BUTTONS_CLOSE, err->message);
      gtk_dialog_run(GTK_DIALOG(dialog));
      gtk_widget_destroy(dialog);
      g_error_free(err);
      break;
    }
    case GST_MESSAGE_BUFFERING:
    {
      gint percent;
      GstState state;
      gst_message_parse_buffering(message, &percent);
      gst_element_get_state(playbin, &state, NULL, GST_CLOCK_TIME_NONE);
      if (percent < 100 && state == GST_STATE_PLAYING)
        gst_element_set_state(playbin, GST_STATE_PAUSED);
      else if (percent == 100 && state != GST_STATE_PLAYING)
        gst_element_set_state(playbin, GST_STATE_PLAYING);
      break;
    }
    default:
      break;
  }
  return TRUE;
}

static void soup_callback_token(SoupSession* session, SoupMessage* msg, gpointer user_data)
{
  JsonParser* parser = json_parser_new();
  json_parser_load_from_data(parser, msg->response_body->data, -1, NULL);

  JsonReader* reader = json_reader_new(json_parser_get_root(parser));

  json_reader_read_member(reader, "sig");
  const gchar* sig = json_reader_get_string_value(reader);
  json_reader_end_member(reader);

  json_reader_read_member(reader, "token");
  const gchar* token = json_reader_get_string_value(reader);
  json_reader_end_member(reader);

  GRand* random = g_rand_new();

  gchar* uri = g_strdup_printf("http://usher.twitch.tv/api/channel/hls/%s.m3u8?player=twitchweb&token=%s&sig=%s&allow_audio_only=true&allow_source=true&type=any&p=%d", channel->str, token, sig, g_rand_int_range(random, 0, 1000000));

  g_object_unref(reader);
  g_object_unref(parser);
  g_object_unref(session);
  g_rand_free(random);

  if (playbin) {
    gst_element_set_state(playbin, GST_STATE_NULL);
    gst_object_unref(playbin);
  }

  playbin = gst_element_factory_make("playbin", NULL);

  g_object_set(playbin, "uri", uri, NULL);
  g_object_set(playbin, "latency", 2 * GST_SECOND, NULL);
  g_object_set(playbin, "connection_speed", connection_speed, NULL);

  g_free(uri);

  GstBus* bus = gst_element_get_bus(playbin);
  gst_bus_set_sync_handler(bus, overlay, NULL, NULL);
  gst_bus_add_watch(bus, bus_watch, NULL);
  gst_object_unref(bus);

  gst_element_set_state(playbin, GST_STATE_PAUSED);
}

static void soup_callback(SoupSession* session, SoupMessage* msg, gpointer user_data)
{
  JsonParser* parser = json_parser_new();
  json_parser_load_from_data(parser, msg->response_body->data, -1, NULL);

  JsonReader* reader = json_reader_new(json_parser_get_root(parser));

  json_reader_read_member(reader, "_total");
  int total = json_reader_get_int_value(reader);
  json_reader_end_member(reader);

  g_object_unref(reader);
  g_object_unref(parser);

  if (total != 1) {
    GtkWidget* dialog = gtk_message_dialog_new(GTK_WINDOW(window), GTK_DIALOG_DESTROY_WITH_PARENT, GTK_MESSAGE_INFO, GTK_BUTTONS_CLOSE, "Channel offline");
    gtk_dialog_run(GTK_DIALOG(dialog));
    gtk_widget_destroy(dialog);
    g_object_unref(session);
    return;
  }

  gchar* url = g_strdup_printf("https://api.twitch.tv/api/channels/%s/access_token", channel->str);
  SoupMessage* message = soup_message_new("GET", url);
  soup_message_headers_append(message->request_headers, "Client-ID", client_id);
  g_free(url);

  soup_session_queue_message(session, message, soup_callback_token, NULL);
}

static void activate_channel(GtkWidget* widget, gpointer user_data)
{
  if (g_strcmp0(gtk_entry_get_text(GTK_ENTRY(widget)), "") != 0) {
    g_string_assign(channel, g_ascii_strdown(gtk_entry_get_text(GTK_ENTRY(widget)), -1));
  }

  gchar* url = g_strdup_printf("https://api.twitch.tv/kraken/streams?channel=%s", channel->str);
  SoupMessage* message = soup_message_new("GET", url);
  soup_message_headers_append(message->request_headers, "Client-ID", client_id);
  g_free(url);

  SoupSession* session = soup_session_new_with_options(SOUP_SESSION_SSL_STRICT, FALSE, NULL);
  soup_session_queue_message(session, message, soup_callback, NULL);

  gtk_widget_destroy(GTK_WIDGET(user_data));
}

static void activate_connection_speed(GtkWidget* widget, gpointer user_data)
{
  connection_speed = 0;
  gtk_widget_destroy(GTK_WIDGET(user_data));
}

static gboolean key_press_event(GtkWidget* widget, GdkEventKey* event, gpointer user_data)
{
  switch (event->keyval) {
    case GDK_KEY_Escape:
      gtk_window_unmaximize(GTK_WINDOW(window));
      gtk_window_unfullscreen(GTK_WINDOW(window));
      break;
    case GDK_KEY_q:
      gtk_window_close(GTK_WINDOW(window));
      break;
    case GDK_KEY_plus:
      if (playbin) {
        gdouble volume;
        g_object_get(G_OBJECT(playbin), "volume", &volume, NULL);
        g_object_set(G_OBJECT(playbin), "volume", MIN(volume + 0.0125, 1.0), NULL);
      }
      break;
    case GDK_KEY_minus:
      if (playbin) {
        gdouble volume;
        g_object_get(G_OBJECT(playbin), "volume", &volume, NULL);
        g_object_set(G_OBJECT(playbin), "volume", MAX(volume - 0.0125, 0.0), NULL);
      }
      break;
    case GDK_KEY_g:
      {
        GtkWidget* entry = gtk_entry_new();
        GtkWidget* dialog = gtk_dialog_new_with_buttons("Enter channel", GTK_WINDOW(window), GTK_DIALOG_DESTROY_WITH_PARENT, NULL, NULL);
        g_signal_connect(G_OBJECT(entry), "activate", G_CALLBACK(activate_channel), dialog);
        gtk_entry_set_text(GTK_ENTRY(entry), channel->str);
        gtk_container_add(GTK_CONTAINER(gtk_dialog_get_content_area(GTK_DIALOG(dialog))), entry);
        gtk_window_set_resizable(GTK_WINDOW(dialog), FALSE);
        gtk_widget_show_all(dialog);
      }
      break;
    case GDK_KEY_s:
      {
        GtkWidget* entry = gtk_entry_new();
        GtkWidget* dialog = gtk_dialog_new_with_buttons("Max kbps", GTK_WINDOW(window), GTK_DIALOG_DESTROY_WITH_PARENT, NULL, NULL);
        g_signal_connect(G_OBJECT(entry), "activate", G_CALLBACK(activate_connection_speed), dialog);
        gtk_container_add(GTK_CONTAINER(gtk_dialog_get_content_area(GTK_DIALOG(dialog))), entry);
        gtk_window_set_resizable(GTK_WINDOW(dialog), FALSE);
        gtk_widget_show_all(dialog);
      }
      break;
    default:
      return FALSE;
  }
  return TRUE;
}

static gboolean button_press_event(GtkWidget* widget, GdkEventButton* event, gpointer user_data)
{
  if (event->type == GDK_DOUBLE_BUTTON_PRESS && event->button == GDK_BUTTON_PRIMARY) {
    if (gdk_window_get_state(gtk_widget_get_window(window)) & GDK_WINDOW_STATE_MAXIMIZED)
      gtk_window_unmaximize(GTK_WINDOW(window));
    else if (gdk_window_get_state(gtk_widget_get_window(window)) & GDK_WINDOW_STATE_FULLSCREEN)
      gtk_window_unfullscreen(GTK_WINDOW(window));
    else
      gtk_window_fullscreen(GTK_WINDOW(window));
    return FALSE;
  }
  return TRUE;
}

static gboolean delete_event(GtkWidget* widget, GdkEvent* event, gpointer user_data)
{
  if (playbin)
    gst_element_set_state(playbin, GST_STATE_NULL);
  return FALSE;
}

static void activate(void)
{
  window = gtk_application_window_new(app);
  area = gtk_drawing_area_new();
  channel = g_string_new(NULL);

  gtk_widget_add_events(area, GDK_BUTTON_PRESS_MASK);
  gtk_container_add(GTK_CONTAINER(window), area);

  gtk_window_set_title(GTK_WINDOW(window), "Twit-Twat");
  gtk_window_set_hide_titlebar_when_maximized(GTK_WINDOW(window), TRUE);
  gtk_window_set_default_size(GTK_WINDOW(window), 960, 540);

  gtk_widget_show_all(window);

  g_signal_connect(G_OBJECT(window), "key-press-event", G_CALLBACK(key_press_event), NULL);
  g_signal_connect(G_OBJECT(window), "button-press-event", G_CALLBACK(button_press_event), NULL);
  g_signal_connect(G_OBJECT(window), "delete-event", G_CALLBACK(delete_event), NULL);
}

int main(int argc, char** argv)
{
  gtk_init(&argc, &argv);
  gst_init(&argc, &argv);

  GstPluginFeature* nvdec = gst_registry_lookup_feature(gst_registry_get(), "nvdec");
  if (nvdec) {
    gst_plugin_feature_set_rank(nvdec, GST_RANK_PRIMARY << 1);
    g_object_unref(nvdec);
  }

  app = gtk_application_new(NULL, G_APPLICATION_FLAGS_NONE);

  g_signal_connect(G_OBJECT(app), "activate", G_CALLBACK(activate), NULL);

  return g_application_run(G_APPLICATION(app), argc, argv);
}
