# Makefile.am
# Copyright (C) 2002, 2012, 2015 g10 Code GmbH
#
# This file is part of PINENTRY.
#
# PINENTRY is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 2 of the License, or
# (at your option) any later version.
#
# PINENTRY is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program; if not, write to the Free Software
# Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA

## Process this file with automake to produce Makefile.in

ACLOCAL_AMFLAGS = -I m4
DISTCHECK_CONFIGURE_FLAGS = --disable-pinentry-qt4

GITLOG_TO_CHANGELOG=gitlog-to-changelog

EXTRA_DIST = autogen.sh autogen.rc README.GIT ChangeLog-old \
	     build-aux/gitlog-to-changelog \
	     build-aux/git-log-fix build-aux/git-log-footer

if BUILD_PINENTRY_CURSES
pinentry_curses = curses
else
pinentry_curses =
endif

if BUILD_PINENTRY_TTY
pinentry_tty = tty
else
pinentry_tty =
endif

if BUILD_PINENTRY_EMACS
pinentry_emacs = emacs
else
pinentry_emacs =
endif

if BUILD_PINENTRY_GTK_2
pinentry_gtk_2 = gtk+-2
else
pinentry_gtk_2 =
endif

if BUILD_PINENTRY_GNOME_3
pinentry_gnome_3 = gnome3
else
pinentry_gnome_3 =
endif

if BUILD_PINENTRY_QT4
pinentry_qt4 = qt4
else
pinentry_qt4 =
endif

if BUILD_PINENTRY_W32
pinentry_w32 = w32
else
pinentry_w32 =
endif

SUBDIRS = m4 secmem pinentry ${pinentry_curses} ${pinentry_tty} \
	${pinentry_emacs} ${pinentry_gtk_2} ${pinentry_gnome_3} \
	${pinentry_qt4} ${pinentry_w32} doc


install-exec-local:
	@list='$(bin_PROGRAMS)'; for p in $$list; do \
	  echo "  $(SETCAP) cap_ipc_lock+p $(DESTDIR)$(bindir)/`echo $$p|sed 's/$(EXEEXT)$$//'|sed '$(transform)'|sed 's/$$/$(EXEEXT)/'`"; \
	  $(SETCAP) cap_ipc_lock+p $(DESTDIR)$(bindir)/`echo $$p|sed 's/$(EXEEXT)$$//'|sed '$(transform)'|sed 's/$$/$(EXEEXT)/'` || true; \
	done

	(cd $(DESTDIR)$(bindir); \
	rm -f pinentry; \
	$(LN_S) $(PINENTRY_DEFAULT)$(EXEEXT) pinentry)

dist-hook: gen-ChangeLog
	echo "$(VERSION)" > $(distdir)/VERSION

gen_start_date = 2012-08-08T00:00:00
.PHONY: gen-ChangeLog
gen-ChangeLog:
	if test -d $(top_srcdir)/.git; then				\
	  (cd $(top_srcdir) &&                      			\
	    $(GITLOG_TO_CHANGELOG) --append-dot --tear-off		\
	    --amend=build-aux/git-log-fix				\
	    --since=$(gen_start_date) ) > $(distdir)/cl-t;		\
          cat $(top_srcdir)/build-aux/git-log-footer >> $(distdir)/cl-t;\
	  rm -f $(distdir)/ChangeLog;					\
	  mv $(distdir)/cl-t $(distdir)/ChangeLog;			\
	fi
