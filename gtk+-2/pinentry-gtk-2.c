/* pinentry-gtk-2.c
   Copyright (C) 1999 Robert Bihlmeyer <robbe@orcus.priv.at>
   Copyright (C) 2001, 2002, 2007, 2015 g10 Code GmbH
   Copyright (C) 2004 by Albrecht Dre� <albrecht.dress@arcor.de>

   pinentry-gtk-2 is a pinentry application for the Gtk+-2 widget set.
   It tries to follow the Gnome Human Interface Guide as close as
   possible.

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.  */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include <gdk/gdkkeysyms.h>
#if __GNUC__ > 4 || (__GNUC__ == 4 && __GNUC_MINOR__ >= 7 )
# pragma GCC diagnostic push
# pragma GCC diagnostic ignored "-Wstrict-prototypes"
#endif
#include <gtk/gtk.h>
#if __GNUC__ > 4 || (__GNUC__ == 4 && __GNUC_MINOR__ >= 7 )
# pragma GCC diagnostic pop
#endif
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef HAVE_GETOPT_H
#include <getopt.h>
#else
#include "getopt.h"
#endif				/* HAVE_GETOPT_H */

#include "gtksecentry.h"
#include "pinentry.h"

#ifdef FALLBACK_CURSES
#include "pinentry-curses.h"
#endif


#define PGMNAME "pinentry-gtk2"

#ifndef VERSION
#  define VERSION
#endif

static pinentry_t pinentry;
static int grab_failed;
static int passphrase_ok;
typedef enum { CONFIRM_CANCEL, CONFIRM_OK, CONFIRM_NOTOK } confirm_value_t;
static confirm_value_t confirm_value;

static GtkWidget *entry;
static GtkWidget *repeat_entry;
static GtkWidget *error_label;
static GtkWidget *qualitybar;
static GtkTooltips *tooltips;
static gboolean got_input;
static guint timeout_source;

/* Gnome hig small and large space in pixels.  */
#define HIG_SMALL      6
#define HIG_LARGE     12

/* The text shown in the quality bar when no text is shown.  This is not
 * the empty string, because with an empty string the height of
 * the quality bar is less than with a non-empty string.  This results
 * in ugly layout changes when the text changes from non-empty to empty
 * and vice versa.  */
#define QUALITYBAR_EMPTY_TEXT " "


/* Constrain size of the window the window should not shrink beyond
   the requisition, and should not grow vertically.  */
static void
constrain_size (GtkWidget *win, GtkRequisition *req, gpointer data)
{
  static gint width, height;
  GdkGeometry geo;

  (void)data;

  if (req->width == width && req->height == height)
    return;
  width = req->width;
  height = req->height;
  geo.min_width = width;
  /* This limit is arbitrary, but INT_MAX breaks other things */
  geo.max_width = 10000;
  geo.min_height = geo.max_height = height;
  gtk_window_set_geometry_hints (GTK_WINDOW (win), NULL, &geo,
				 GDK_HINT_MIN_SIZE | GDK_HINT_MAX_SIZE);
}


/* Realize the window as transient if we grab the keyboard.  This
   makes the window a modal dialog to the root window, which helps the
   window manager.  See the following quote from:
   http://standards.freedesktop.org/wm-spec/wm-spec-1.4.html#id2512420

   Implementing enhanced support for application transient windows

   If the WM_TRANSIENT_FOR property is set to None or Root window, the
   window should be treated as a transient for all other windows in
   the same group. It has been noted that this is a slight ICCCM
   violation, but as this behavior is pretty standard for many
   toolkits and window managers, and is extremely unlikely to break
   anything, it seems reasonable to document it as standard.  */

static void
make_transient (GtkWidget *win, GdkEvent *event, gpointer data)
{
  GdkScreen *screen;
  GdkWindow *root;

  (void)event;
  (void)data;

  if (! pinentry->grab)
    return;

  /* Make window transient for the root window.  */
  screen = gdk_screen_get_default ();
  root = gdk_screen_get_root_window (screen);
  gdk_window_set_transient_for (win->window, root);
}


/* Grab the keyboard for maximum security */
static int
grab_keyboard (GtkWidget *win, GdkEvent *event, gpointer data)
{
  (void)data;

  if (! pinentry->grab)
    return FALSE;

  if (gdk_keyboard_grab (win->window, FALSE, gdk_event_get_time (event)))
    {
      g_critical ("could not grab keyboard");
      grab_failed = 1;
      gtk_main_quit ();
    }
  return FALSE;
}


/* Remove grab.  */
static int
ungrab_keyboard (GtkWidget *win, GdkEvent *event, gpointer data)
{
  (void)data;

  gdk_keyboard_ungrab (gdk_event_get_time (event));
  /* Unmake window transient for the root window.  */
  /* gdk_window_set_transient_for cannot be used with parent = NULL to
     unset transient hint (unlike gtk_ version which can).  Replacement
     code is taken from gtk_window_transient_parent_unrealized.  */
  gdk_property_delete (win->window,
                       gdk_atom_intern_static_string ("WM_TRANSIENT_FOR"));
  return FALSE;
}


static int
delete_event (GtkWidget *widget, GdkEvent *event, gpointer data)
{
  (void)widget;
  (void)event;
  (void)data;

  pinentry->close_button = 1;
  gtk_main_quit ();
  return TRUE;
}


static void
button_clicked (GtkWidget *widget, gpointer data)
{
  (void)widget;

  if (data)
    {
      const char *s, *s2;

      /* Okay button or enter used in text field.  */
      s = gtk_secure_entry_get_text (GTK_SECURE_ENTRY (entry));
      if (!s)
	s = "";

      if (pinentry->repeat_passphrase && repeat_entry)
        {
          s2 = gtk_secure_entry_get_text (GTK_SECURE_ENTRY (repeat_entry));
          if (!s2)
            s2 = "";
          if (strcmp (s, s2))
            {
              gtk_label_set_text (GTK_LABEL (error_label),
                                  pinentry->repeat_error_string?
                                  pinentry->repeat_error_string:
                                  "not correctly repeated");
              gtk_widget_grab_focus (entry);
              return; /* again */
            }
          pinentry->repeat_okay = 1;
        }

      passphrase_ok = 1;
      pinentry_setbufferlen (pinentry, strlen (s) + 1);
      if (pinentry->pin)
	strcpy (pinentry->pin, s);
    }
  gtk_main_quit ();
}


static void
enter_callback (GtkWidget *widget, GtkWidget *anentry)
{
  (void)anentry;

  button_clicked (widget, (gpointer) CONFIRM_OK);
}


static void
confirm_button_clicked (GtkWidget *widget, gpointer data)
{
  (void)widget;

  confirm_value = (confirm_value_t) data;
  gtk_main_quit ();
}


static void
cancel_callback (GtkAccelGroup *acc, GObject *accelerable,
                 guint keyval, GdkModifierType modifier, gpointer data)
{
  int confirm_mode = !!data;

  (void)acc;
  (void)keyval;
  (void)modifier;

  if (confirm_mode)
    confirm_button_clicked (GTK_WIDGET (accelerable),
                            (gpointer)CONFIRM_CANCEL);
  else
    button_clicked (GTK_WIDGET (accelerable),
                    (gpointer)CONFIRM_CANCEL);
}



static gchar *
pinentry_utf8_validate (gchar *text)
{
  gchar *result;

  if (!text)
    return NULL;

  if (g_utf8_validate (text, -1, NULL))
    return g_strdup (text);

  /* Failure: Assume that it was encoded in the current locale and
     convert it to utf-8.  */
  result = g_locale_to_utf8 (text, -1, NULL, NULL, NULL);
  if (!result)
    {
      gchar *p;

      result = p = g_strdup (text);
      while (!g_utf8_validate (p, -1, (const gchar **) &p))
	*p = '?';
    }
  return result;
}


/* Handler called for "changed".   We use it to update the quality
   indicator.  */
static void
changed_text_handler (GtkWidget *widget)
{
  char textbuf[50];
  const char *s;
  int length;
  int percent;
  GdkColor color = { 0, 0, 0, 0};

  got_input = TRUE;

  if (pinentry->repeat_passphrase && repeat_entry)
    {
      gtk_secure_entry_set_text (GTK_SECURE_ENTRY (repeat_entry), "");
      gtk_label_set_text (GTK_LABEL (error_label), "");
    }

  if (!qualitybar || !pinentry->quality_bar)
    return;

  s = gtk_secure_entry_get_text (GTK_SECURE_ENTRY (widget));
  if (!s)
    s = "";
  length = strlen (s);
  percent = length? pinentry_inq_quality (pinentry, s, length) : 0;
  if (!length)
    {
      strcpy(textbuf, QUALITYBAR_EMPTY_TEXT);
      color.red = 0xffff;
    }
  else if (percent < 0)
    {
      snprintf (textbuf, sizeof textbuf, "(%d%%)", -percent);
      color.red = 0xffff;
      percent = -percent;
    }
  else
    {
      snprintf (textbuf, sizeof textbuf, "%d%%", percent);
      color.green = 0xffff;
    }
  gtk_progress_bar_set_fraction (GTK_PROGRESS_BAR (qualitybar),
                                 (double)percent/100.0);
  gtk_progress_bar_set_text (GTK_PROGRESS_BAR (qualitybar), textbuf);
  gtk_widget_modify_bg (qualitybar, GTK_STATE_PRELIGHT, &color);
}


#ifdef HAVE_LIBSECRET
static void
may_save_passphrase_toggled (GtkWidget *widget, gpointer data)
{
  GtkToggleButton *button = GTK_TOGGLE_BUTTON (widget);
  pinentry_t ctx = (pinentry_t) data;

  ctx->may_cache_password = gtk_toggle_button_get_active (button);
}
#endif


static gboolean
timeout_cb (gpointer data)
{
  (void)data;
  if (!got_input)
    gtk_main_quit ();

  /* Don't run again.  */
  timeout_source = 0;
  return FALSE;
}


static GtkWidget *
create_window (pinentry_t ctx, int confirm_mode)
{
  GtkWidget *w;
  GtkWidget *win, *box;
  GtkWidget *wvbox, *chbox, *bbox;
  GtkAccelGroup *acc;
  GClosure *acc_cl;
  gchar *msg;

  tooltips = gtk_tooltips_new ();

  /* FIXME: check the grabbing code against the one we used with the
     old gpg-agent */
  win = gtk_window_new (GTK_WINDOW_TOPLEVEL);
  acc = gtk_accel_group_new ();

  g_signal_connect (G_OBJECT (win), "delete_event",
		    G_CALLBACK (delete_event), NULL);

#if 0
  g_signal_connect (G_OBJECT (win), "destroy", G_CALLBACK (gtk_main_quit),
		    NULL);
#endif
  g_signal_connect (G_OBJECT (win), "size-request",
		    G_CALLBACK (constrain_size), NULL);
  if (!confirm_mode)
    {
      if (pinentry->grab)
	g_signal_connect (G_OBJECT (win),
			  "realize", G_CALLBACK (make_transient), NULL);

      /* We need to grab the keyboard when its visible! not when its
         mapped (there is a difference)  */
      g_object_set (G_OBJECT(win), "events",
                    GDK_VISIBILITY_NOTIFY_MASK | GDK_STRUCTURE_MASK, NULL);

      g_signal_connect (G_OBJECT (win),
			pinentry->grab
                        ? "visibility-notify-event"
                        : "focus-in-event",
			G_CALLBACK (grab_keyboard), NULL);
      g_signal_connect (G_OBJECT (win),
			pinentry->grab ? "unmap-event" : "focus-out-event",
			G_CALLBACK (ungrab_keyboard), NULL);
    }
  gtk_window_add_accel_group (GTK_WINDOW (win), acc);

  wvbox = gtk_vbox_new (FALSE, HIG_LARGE * 2);
  gtk_container_add (GTK_CONTAINER (win), wvbox);
  gtk_container_set_border_width (GTK_CONTAINER (wvbox), HIG_LARGE);

  chbox = gtk_hbox_new (FALSE, HIG_LARGE);
  gtk_box_pack_start (GTK_BOX (wvbox), chbox, FALSE, FALSE, 0);

  w = gtk_image_new_from_stock (GTK_STOCK_DIALOG_AUTHENTICATION,
					       GTK_ICON_SIZE_DIALOG);
  gtk_misc_set_alignment (GTK_MISC (w), 0.0, 0.0);
  gtk_box_pack_start (GTK_BOX (chbox), w, FALSE, FALSE, 0);

  box = gtk_vbox_new (FALSE, HIG_SMALL);
  gtk_box_pack_start (GTK_BOX (chbox), box, TRUE, TRUE, 0);

  if (pinentry->title)
    {
      msg = pinentry_utf8_validate (pinentry->title);
      gtk_window_set_title (GTK_WINDOW(win), msg);
    }
  if (pinentry->description)
    {
      msg = pinentry_utf8_validate (pinentry->description);
      w = gtk_label_new (msg);
      g_free (msg);
      gtk_misc_set_alignment (GTK_MISC (w), 0.0, 0.5);
      gtk_label_set_line_wrap (GTK_LABEL (w), TRUE);
      gtk_box_pack_start (GTK_BOX (box), w, TRUE, FALSE, 0);
    }
  if (!confirm_mode && (pinentry->error || pinentry->repeat_passphrase))
    {
      /* With the repeat passphrase option we need to create the label
         in any case so that it may later be updated by the error
         message.  */
      GdkColor color = { 0, 0xffff, 0, 0 };

      if (pinentry->error)
        msg = pinentry_utf8_validate (pinentry->error);
      else
        msg = "";
      error_label = gtk_label_new (msg);
      if (pinentry->error)
        g_free (msg);
      gtk_misc_set_alignment (GTK_MISC (error_label), 0.0, 0.5);
      gtk_label_set_line_wrap (GTK_LABEL (error_label), TRUE);
      gtk_box_pack_start (GTK_BOX (box), error_label, TRUE, FALSE, 0);
      gtk_widget_modify_fg (error_label, GTK_STATE_NORMAL, &color);
    }

  qualitybar = NULL;

  if (!confirm_mode)
    {
      int nrow;
      GtkWidget* table;

      nrow = 1;
      if (pinentry->quality_bar)
        nrow++;
      if (pinentry->repeat_passphrase)
        nrow++;

      table = gtk_table_new (nrow, 2, FALSE);
      nrow = 0;
      gtk_box_pack_start (GTK_BOX (box), table, FALSE, FALSE, 0);

      if (pinentry->prompt)
	{
	  msg = pinentry_utf8_validate (pinentry->prompt);
	  w = gtk_label_new_with_mnemonic (msg);
	  g_free (msg);
	  gtk_misc_set_alignment (GTK_MISC (w), 1.0, 0.5);
	  gtk_table_attach (GTK_TABLE (table), w, 0, 1, nrow, nrow+1,
			    GTK_FILL, GTK_FILL, 4, 0);
	}

      entry = gtk_secure_entry_new ();
      gtk_widget_set_size_request (entry, 200, -1);
      g_signal_connect (G_OBJECT (entry), "activate",
			G_CALLBACK (enter_callback), entry);
      g_signal_connect (G_OBJECT (entry), "changed",
                        G_CALLBACK (changed_text_handler), entry);
      gtk_table_attach (GTK_TABLE (table), entry, 1, 2, nrow, nrow+1,
                        GTK_EXPAND|GTK_FILL, GTK_EXPAND|GTK_FILL, 0, 0);
      gtk_widget_grab_focus (entry);
      gtk_widget_show (entry);
      nrow++;

      if (pinentry->quality_bar)
	{
          msg = pinentry_utf8_validate (pinentry->quality_bar);
	  w = gtk_label_new (msg);
          g_free (msg);
	  gtk_misc_set_alignment (GTK_MISC (w), 1.0, 0.5);
	  gtk_table_attach (GTK_TABLE (table), w, 0, 1, nrow, nrow+1,
			    GTK_FILL, GTK_FILL, 4, 0);
	  qualitybar = gtk_progress_bar_new();
	  gtk_widget_add_events (qualitybar,
				 GDK_ENTER_NOTIFY_MASK | GDK_LEAVE_NOTIFY_MASK);
	  gtk_progress_bar_set_text (GTK_PROGRESS_BAR (qualitybar),
				     QUALITYBAR_EMPTY_TEXT);
	  gtk_progress_bar_set_fraction (GTK_PROGRESS_BAR (qualitybar), 0.0);
          if (pinentry->quality_bar_tt)
            gtk_tooltips_set_tip (GTK_TOOLTIPS (tooltips), qualitybar,
                                  pinentry->quality_bar_tt, "");
	  gtk_table_attach (GTK_TABLE (table), qualitybar, 1, 2, nrow, nrow+1,
	  		    GTK_EXPAND|GTK_FILL, GTK_EXPAND|GTK_FILL, 0, 0);
          nrow++;
	}


      if (pinentry->repeat_passphrase)
        {
	  msg = pinentry_utf8_validate (pinentry->repeat_passphrase);
	  w = gtk_label_new (msg);
	  g_free (msg);
	  gtk_misc_set_alignment (GTK_MISC (w), 1.0, 0.5);
	  gtk_table_attach (GTK_TABLE (table), w, 0, 1, nrow, nrow+1,
			    GTK_FILL, GTK_FILL, 4, 0);

          repeat_entry = gtk_secure_entry_new ();
          gtk_widget_set_size_request (repeat_entry, 200, -1);
          g_signal_connect (G_OBJECT (entry), "activate",
                            G_CALLBACK (enter_callback), repeat_entry);
          gtk_table_attach (GTK_TABLE (table), repeat_entry, 1, 2, nrow, nrow+1,
                            GTK_EXPAND|GTK_FILL, GTK_EXPAND|GTK_FILL, 0, 0);
          gtk_widget_grab_focus (entry);
          gtk_widget_show (entry);
          nrow++;
        }
    }

  bbox = gtk_hbutton_box_new ();
  gtk_button_box_set_layout (GTK_BUTTON_BOX (bbox), GTK_BUTTONBOX_END);
  gtk_box_set_spacing (GTK_BOX (bbox), 6);
  gtk_box_pack_start (GTK_BOX (wvbox), bbox, TRUE, FALSE, 0);

#ifdef HAVE_LIBSECRET
  if (ctx->allow_external_password_cache && ctx->keyinfo)
    /* Only show this if we can cache passwords and we have a stable
       key identifier.  */
    {
      if (pinentry->default_pwmngr)
        {
          msg = pinentry_utf8_validate (pinentry->default_pwmngr);
          w = gtk_check_button_new_with_mnemonic (msg);
          g_free (msg);
        }
      else
        w = gtk_check_button_new_with_label ("Save passphrase using libsecret");

      /* Make sure it is off by default.  */
      gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (w), FALSE);

      gtk_box_pack_start (GTK_BOX (box), w, TRUE, FALSE, 0);
      gtk_widget_show (w);

      g_signal_connect (G_OBJECT (w), "toggled",
                        G_CALLBACK (may_save_passphrase_toggled),
			(gpointer) ctx);
    }
#endif

  if (!pinentry->one_button)
    {
      if (pinentry->cancel)
        {
          msg = pinentry_utf8_validate (pinentry->cancel);
          w = gtk_button_new_with_mnemonic (msg);
          g_free (msg);
        }
      else if (pinentry->default_cancel)
        {
          GtkWidget *image;

          msg = pinentry_utf8_validate (pinentry->default_cancel);
          w = gtk_button_new_with_mnemonic (msg);
          g_free (msg);
          image = gtk_image_new_from_stock (GTK_STOCK_CANCEL,
                                            GTK_ICON_SIZE_BUTTON);
          if (image)
            gtk_button_set_image (GTK_BUTTON (w), image);
        }
      else
          w = gtk_button_new_from_stock (GTK_STOCK_CANCEL);
      gtk_container_add (GTK_CONTAINER (bbox), w);
      g_signal_connect (G_OBJECT (w), "clicked",
                        G_CALLBACK (confirm_mode ? confirm_button_clicked
                                    : button_clicked),
			(gpointer) CONFIRM_CANCEL);

      acc_cl = g_cclosure_new (G_CALLBACK (cancel_callback),
			       (confirm_mode? "":NULL), NULL);
      gtk_accel_group_connect (acc, GDK_KEY_Escape, 0, 0, acc_cl);

      GTK_WIDGET_SET_FLAGS (w, GTK_CAN_DEFAULT);
    }

  if (confirm_mode && !pinentry->one_button && pinentry->notok)
    {
      msg = pinentry_utf8_validate (pinentry->notok);
      w = gtk_button_new_with_mnemonic (msg);
      g_free (msg);

      gtk_container_add (GTK_CONTAINER (bbox), w);
      g_signal_connect (G_OBJECT (w), "clicked",
                        G_CALLBACK (confirm_button_clicked),
			(gpointer) CONFIRM_NOTOK);
      GTK_WIDGET_SET_FLAGS (w, GTK_CAN_DEFAULT);
    }

  if (pinentry->ok)
    {
      msg = pinentry_utf8_validate (pinentry->ok);
      w = gtk_button_new_with_mnemonic (msg);
      g_free (msg);
    }
  else if (pinentry->default_ok)
    {
      GtkWidget *image;

      msg = pinentry_utf8_validate (pinentry->default_ok);
      w = gtk_button_new_with_mnemonic (msg);
      g_free (msg);
      image = gtk_image_new_from_stock (GTK_STOCK_OK,
                                        GTK_ICON_SIZE_BUTTON);
      if (image)
        gtk_button_set_image (GTK_BUTTON (w), image);
    }
  else
    w = gtk_button_new_from_stock (GTK_STOCK_OK);
  gtk_container_add (GTK_CONTAINER(bbox), w);
  if (!confirm_mode)
    {
      g_signal_connect (G_OBJECT (w), "clicked",
			G_CALLBACK (button_clicked), "ok");
      GTK_WIDGET_SET_FLAGS (w, GTK_CAN_DEFAULT);
      gtk_widget_grab_default (w);
      g_signal_connect_object (G_OBJECT (entry), "focus_in_event",
				G_CALLBACK (gtk_widget_grab_default),
			       G_OBJECT (w), 0);
    }
  else
    {
      g_signal_connect (G_OBJECT (w), "clicked",
			G_CALLBACK(confirm_button_clicked),
			(gpointer) CONFIRM_OK);
      GTK_WIDGET_SET_FLAGS (w, GTK_CAN_DEFAULT);
    }

  gtk_window_set_position (GTK_WINDOW (win), GTK_WIN_POS_CENTER);
  gtk_window_set_keep_above (GTK_WINDOW (win), TRUE);
  gtk_widget_show_all (win);
  gtk_window_present (GTK_WINDOW (win));  /* Make sure it has the focus.  */

  if (pinentry->timeout > 0)
    timeout_source = g_timeout_add (pinentry->timeout*1000, timeout_cb, NULL);

  return win;
}


static int
gtk_cmd_handler (pinentry_t pe)
{
  GtkWidget *w;
  int want_pass = !!pe->pin;

  got_input = FALSE;
  pinentry = pe;
  confirm_value = CONFIRM_CANCEL;
  passphrase_ok = 0;
  w = create_window (pe, want_pass ? 0 : 1);
  gtk_main ();
  gtk_widget_destroy (w);
  while (gtk_events_pending ())
    gtk_main_iteration ();

  if (timeout_source)
    /* There is a timer running.  Cancel it.  */
    {
      g_source_remove (timeout_source);
      timeout_source = 0;
    }

  if (confirm_value == CONFIRM_CANCEL || grab_failed)
    pe->canceled = 1;

  pinentry = NULL;
  if (want_pass)
    {
      if (passphrase_ok && pe->pin)
	return strlen (pe->pin);
      else
	return -1;
    }
  else
    return (confirm_value == CONFIRM_OK) ? 1 : 0;
}


pinentry_cmd_handler_t pinentry_cmd_handler = gtk_cmd_handler;


int
main (int argc, char *argv[])
{
  static GMemVTable secure_mem =
    {
      secentry_malloc,
      secentry_realloc,
      secentry_free,
      NULL,
      NULL,
      NULL
    };

  g_mem_set_vtable (&secure_mem);

  pinentry_init (PGMNAME);

#ifdef FALLBACK_CURSES
  if (pinentry_have_display (argc, argv))
    {
      if (! gtk_init_check (&argc, &argv))
	pinentry_cmd_handler = curses_cmd_handler;
    }
  else
    pinentry_cmd_handler = curses_cmd_handler;
#else
  gtk_init (&argc, &argv);
#endif

  pinentry_parse_opts (argc, argv);

  if (pinentry_loop ())
    return 1;

  return 0;
}
