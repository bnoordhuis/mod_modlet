mod_modlet.la: mod_modlet.slo
	$(SH_LINK) -rpath $(libexecdir) -module -avoid-version  mod_modlet.lo
DISTCLEAN_TARGETS = modules.mk
shared =  mod_modlet.la
