#!/usr/bin/env python3
#
# Twit-Twat
#
# Copyright (C) 2017-2018 Florian Zwoch <fzwoch@gmail.com>
#
# This program is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program. If not, see <http://www.gnu.org/licenses/>.
#

import sys
import random
import gi

gi.require_version('Gtk', '3.0')
gi.require_version('Gst', '1.0')
gi.require_version('GstVideo', '1.0')
gi.require_version('Soup', '2.4')
gi.require_version('Json', '1.0')

from gi.repository import GLib, Gtk, Gdk, Gst, Soup, Json, GstVideo, GdkX11

class TwitTwatApp(Gtk.Application):
	channel = ''
	client_id = '7ikopbkspr7556owm9krqmalvr2w0i4'
	connection_speed = 0
	playbin = None
	window = None
	area = None

	def do_activate(self):
		self.window = Gtk.Window()
		self.area = Gtk.DrawingArea()

		self.area.add_events(Gdk.EventMask.BUTTON_PRESS_MASK)

		self.add_window(self.window)
		self.window.add(self.area)

		self.window.set_title('Twit-Twat')
		self.window.set_hide_titlebar_when_maximized(True)
		self.window.set_default_size(960, 540)
		self.window.show_all()

		self.window.connect('button-press-event', self.do_button_press_event)
		self.window.connect('key-press-event', self.do_key_press_event)

	def do_button_press_event(self, widget, event):
		if not event.type == Gdk.EventType.DOUBLE_BUTTON_PRESS and event.button == Gdk.BUTTON_PRIMARY:
			return False

		if self.window.get_window().get_state() & Gdk.WindowState.MAXIMIZED:
			self.window.unmaximize()
		elif self.window.get_window().get_state() & Gdk.WindowState.FULLSCREEN:
			self.window.unfullscreen()
		else:
			self.window.fullscreen()
		return True

	def do_key_press_event(self, widget, event):
		if event.keyval == Gdk.KEY_Escape:
			self.window.unmaximize()
			self.window.unfullscreen()
		elif event.keyval == Gdk.KEY_plus or event.keyval == Gdk.KEY_KP_Add:
			if self.playbin:
				volume = self.playbin.get_property('volume')
				self.playbin.set_property('volume', min(volume + 0.0125, 1.0))
		elif event.keyval == Gdk.KEY_minus or event.keyval == Gdk.KEY_KP_Subtract:
			if self.playbin:
				volume = self.playbin.get_property('volume')
				self.playbin.set_property('volume', max(volume - 0.0125, 0.0))
		elif event.keyval == Gdk.KEY_q:
			if self.playbin:
				self.playbin.set_state(Gst.State.NULL)
			self.window.close()
		elif event.keyval == Gdk.KEY_g:
			entry = Gtk.Entry()
			dialog = Gtk.Dialog('Enter Channel', self.window, Gtk.DialogFlags.MODAL | Gtk.DialogFlags.DESTROY_WITH_PARENT)
			entry.set_text(self.channel)
			entry.connect('activate', self.on_channel_activate, dialog)
			dialog.get_content_area().add(entry)
			dialog.set_resizable(False)
			dialog.show_all()
		elif event.keyval == Gdk.KEY_s:
			entry = Gtk.Entry()
			dialog = Gtk.Dialog('Max kbps', self.window, Gtk.DialogFlags.MODAL | Gtk.DialogFlags.DESTROY_WITH_PARENT)
			entry.set_text(str(self.connection_speed))
			entry.connect('activate', self.on_speed_activate, dialog)
			dialog.get_content_area().add(entry)
			dialog.set_resizable(False)
			dialog.show_all()
		elif event.keyval == Gdk.KEY_h:
			dialog = Gtk.MessageDialog(self.window, Gtk.DialogFlags.DESTROY_WITH_PARENT, Gtk.MessageType.INFO, Gtk.ButtonsType.CLOSE)
			dialog.set_title('Controls')
			dialog.set_markup('<b>G</b>\t\tGo to channel\n<b>+/-</b>\t\tChange volume\n<b>D-Click</b>\tToggle full screen\n<b>Esc</b>\t\tExit full screen\n<b>S</b>\t\tSet bandwidth limit\n<b>H</b>\t\tControls info\n<b>Q</b>\t\tQuit')
			dialog.run()
			dialog.destroy()
		else:
			return False
		return True

	def on_channel_activate(self, entry, dialog):
		if entry.get_text() is not '':
			self.channel = entry.get_text().strip().lower()

			session = Soup.Session()
			message = Soup.Message.new('GET', 'https://api.twitch.tv/kraken/streams?channel=' + self.channel)

			message.request_headers.append('Client-ID', self.client_id)
			session.props.ssl_strict = False
			session.queue_message(message, self.on_get_access_token)
		dialog.destroy()

	def on_speed_activate(self, entry, dialog):
		self.connection_speed = int(entry.get_text())
		dialog.destroy()

	def on_get_access_token(self, session, msg):
		parser = Json.Parser()
		parser.load_from_data(msg.response_body.data, -1)
		reader = Json.Reader()
		reader.set_root(parser.get_root())

		reader.read_member('_total')
		total = reader.get_int_value()
		reader.end_member()

		if total != 1:
			dialog = Gtk.MessageDialog(self.window, Gtk.DialogFlags.DESTROY_WITH_PARENT, Gtk.MessageType.INFO, Gtk.ButtonsType.CLOSE, 'Channel offline')
			dialog.run()
			dialog.destroy()
			return

		message = Soup.Message.new('GET', 'https://api.twitch.tv/api/channels/' + self.channel + '/access_token')
		message.request_headers.append('Client-ID', self.client_id)
		session.queue_message(message, self.on_play_stream)

	def on_play_stream(self, session, msg):
		parser = Json.Parser()
		parser.load_from_data(msg.response_body.data, -1)
		reader = Json.Reader()
		reader.set_root(parser.get_root())

		reader.read_member('sig')
		sig = reader.get_string_value()
		reader.end_member()

		reader.read_member('token')
		token = reader.get_string_value()
		reader.end_member()

		if self.playbin:
			self.playbin.set_state(Gst.State.NULL)

		self.playbin = Gst.ElementFactory.make('playbin')

		if isinstance(self.area.get_window(), GdkX11.X11Window):
			self.playbin.set_window_handle(self.area.get_window().get_xid())

		self.playbin.connect('element-setup', self.on_element_setup)
		self.playbin.get_bus().add_watch(GLib.PRIORITY_DEFAULT, self.on_bus_message)
		self.playbin.set_property('uri', 'http://usher.twitch.tv/api/channel/hls/' + self.channel + '.m3u8?' + 'player=twitchweb&' + 'token=' + token + '&' + 'sig=' + sig + '&' + 'allow_audio_only=true&allow_source=true&type=any&p=' + str(random.randrange(0, 1000000)))
		self.playbin.set_property('latency', 2 * Gst.SECOND)
		self.playbin.set_property('connection_speed', self.connection_speed)
		self.playbin.set_state(Gst.State.PAUSED)

	def on_element_setup(self, playbin, element):
		if element.get_factory() == Gst.ElementFactory.find('glimagesink'):
			element.set_property('handle_events', False)

	def on_bus_message(self, bus, message):
		if message.type == Gst.MessageType.EOS:
			self.playbin.set_state(Gst.State.NULL)
			dialog = Gtk.MessageDialog(self.window, Gtk.DialogFlags.DESTROY_WITH_PARENT, Gtk.MessageType.INFO, Gtk.ButtonsType.CLOSE, "Broadcast finished")
			dialog.run()
			dialog.destroy()
		elif message.type == Gst.MessageType.ERROR:
			self.playbin.set_state(Gst.State.NULL)
			err = message.parse_error()
			dialog = Gtk.MessageDialog(self.window, Gtk.DialogFlags.DESTROY_WITH_PARENT, Gtk.MessageType.ERROR, Gtk.ButtonsType.CLOSE, err.message)
			dialog.run()
			dialog.destroy()
		elif message.type == Gst.MessageType.BUFFERING:
			percent = message.parse_buffering()
			state = self.playbin.get_state(Gst.CLOCK_TIME_NONE)
			if percent < 100 and state == Gst.State.PLAYING:
				self.playbin.set_state(Gst.State.PAUSED)
			elif percent == 100 and state != Gst.State.PLAYING:
				self.playbin.set_state(Gst.State.PLAYING)
		return True

Gtk.init(sys.argv)
Gst.init(sys.argv)

nvdec = Gst.Registry.get().lookup_feature('nvdec')
if nvdec:
	nvdec.set_rank(Gst.Rank.PRIMARY << 1)

TwitTwatApp().run(sys.argv)
