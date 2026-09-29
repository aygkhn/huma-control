# SPDX-License-Identifier: GPL-2.0-or-later
# SPDX-FileCopyrightText: 2026 Gokhan AY <aygkhn@gmail.com>
# Translations of the Huma Control Center app (gettext domain: huma-control)
#
#   make -C app/po          compile the .po files: app/locale/<lang>/LC_MESSAGES/huma-control.mo
#   make -C app/po update   re-extract the source strings (.pot) and update the .po files
#   make -C app/po check    check the .po files
#   make -C app/po clean    delete the compiled .mo files
#
# Run from the repo, the app looks in app/locale; installed, in ~/.local/share/locale.

DOMAIN   := huma-control
SRCDIR   := ..
LOCALEDIR := $(SRCDIR)/locale
LINGUAS  := $(shell grep -v '^[#]' LINGUAS)
# the launcher's Name, GenericName, Comment and Keywords (install.sh: msgfmt --desktop)
DESKTOP  := io.github.aygkhn.HumaControl.desktop
MOFILES  := $(foreach l,$(LINGUAS),$(LOCALEDIR)/$(l)/LC_MESSAGES/$(DOMAIN).mo)

XGETTEXT_FLAGS := --language=Python --from-code=UTF-8 \
	--keyword=_ --keyword=ngettext:1,2 --keyword=pgettext:1c,2 \
	--add-comments=Translators --package-name=huma-control \
	--copyright-holder='Gokhan AY' --msgid-bugs-address=aygkhn@gmail.com

.PHONY: all update check clean

all: $(MOFILES)

$(LOCALEDIR)/%/LC_MESSAGES/$(DOMAIN).mo: %.po
	@mkdir -p $(dir $@)
	msgfmt --check -o $@ $<

# REUSE-IgnoreStart
$(DOMAIN).pot: POTFILES $(addprefix $(SRCDIR)/,$(shell grep -v '^[#]' POTFILES)) $(SRCDIR)/$(DESKTOP)
	cd $(SRCDIR) && xgettext $(XGETTEXT_FLAGS) --files-from=po/POTFILES -o po/$(DOMAIN).pot
	cd $(SRCDIR) && xgettext --join-existing --language=Desktop --from-code=UTF-8 --package-name=huma-control --msgid-bugs-address=aygkhn@gmail.com --copyright-holder='Gokhan AY' -o po/$(DOMAIN).pot $(DESKTOP)
	sed -i -e '1s/.*/# Translation template for the Huma Control Center app./' \
	    -e 's/^# Copyright (C) YEAR Gokhan AY/# Copyright (C) 2026 Gokhan AY/' \
	    -e 's/^# FIRST AUTHOR <EMAIL@ADDRESS>, YEAR./# Gokhan AY <aygkhn@gmail.com>, 2026./' \
	    -e '4a # SPDX-License-Identifier: GPL-2.0-or-later' \
	    -e '4a # SPDX-FileCopyrightText: 2026 Gokhan AY <aygkhn@gmail.com>' $(DOMAIN).pot
# REUSE-IgnoreEnd

update: $(DOMAIN).pot
	for l in $(LINGUAS); do msgmerge --update --backup=none --no-fuzzy-matching $$l.po $(DOMAIN).pot && \
	    msgattrib --no-obsolete -o $$l.po $$l.po; done

check:
	for l in $(LINGUAS); do msgfmt --check --statistics -o /dev/null $$l.po || exit 1; done

clean:
	rm -f $(MOFILES)
