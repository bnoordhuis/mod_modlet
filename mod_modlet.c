#include "httpd.h"
#include "http_log.h"
#include "http_config.h"
#include "http_protocol.h"
#include "ap_config.h"
#include "apr_strings.h"
#include "apr_hash.h"
#include "apr_dso.h"

typedef apr_status_t (*modlet_init_func_t)(void);
typedef void         (*modlet_shutdown_func_t)(void);
typedef apr_status_t (*modlet_handle_request_func_t)(request_rec *r);

typedef struct {
	modlet_handle_request_func_t handle_request;
	apr_dso_handle_t *handle;
	const char *image;
	long image_mtime;
} modlet_t;

typedef struct {
	apr_hash_t *modlets;
	int debug;
} modlet_cfg;

module AP_MODULE_DECLARE_DATA modlet_module;

static long modlet_file_mtime(apr_pool_t *p, const char *filename) {
	apr_finfo_t *fi = apr_pcalloc(p, sizeof *fi);

	if (APR_SUCCESS == apr_stat(fi, filename, 0, p)) {
		return fi->mtime;
	}
	else {
		return -1;
	}
}

static apr_dso_handle_sym_t modlet_find_symbol(server_rec *s, apr_pool_t *p, modlet_t *m, const char *symbol, int required) {
	apr_dso_handle_sym_t sym;
	apr_status_t status;
	char errbuf[1024];

	if (APR_SUCCESS != (status = apr_dso_sym(&sym, m->handle, symbol))) {
		ap_log_error(APLOG_MARK, (required ? APLOG_ERR : APLOG_INFO), status, s,
				"Symbol %s not found in modlet %s: %s", symbol, m->image, apr_dso_error(m->handle, errbuf, sizeof errbuf));
		return 0;
	}

	return sym;
}

static apr_status_t modlet_load_modlet(server_rec *s, apr_pool_t *p, modlet_t *m) {
	modlet_shutdown_func_t shutdown;
	modlet_init_func_t init;
	apr_status_t status;
	char errbuf[1024];

	// TODO wrap in a big mutex

	// unload current modlet first
	if (m->handle != NULL) {
		if ((shutdown = (modlet_shutdown_func_t) modlet_find_symbol(s, p, m, "shutdown", 0))) {
			shutdown();
		}

		if (m->handle == NULL || APR_SUCCESS == (status = apr_dso_unload(m->handle))) {
			m->handle = NULL;
		}
		else {
			ap_log_error(APLOG_MARK, APLOG_ERR, status, s,
					"Failed to unload modlet %s: %s", m->image, apr_dso_error(m->handle, errbuf, sizeof errbuf));
			return status;
		}
	}

	// load the modlet
	if (APR_SUCCESS != (status = apr_dso_load(&m->handle, m->image, p))) {
		// apr_dso_load() initializes c->handle even if the return status != APR_SUCCESS
		ap_log_error(APLOG_MARK, APLOG_ERR, status, s,
				"Failed to load modlet %s: %s", m->image, apr_dso_error(m->handle, errbuf, sizeof errbuf));
		return status;
	}

	// find the request handler
	if (!(m->handle_request = (modlet_handle_request_func_t) modlet_find_symbol(s, p, m, "handle_request", 1))) {
		status = APR_EGENERAL;
		goto unload;
	}

	// execute the init function
	if ((init = (modlet_init_func_t) modlet_find_symbol(s, p, m, "init", 0)) && APR_SUCCESS != (status = init())) {
		goto unload;
	}

	return APR_SUCCESS;

unload:
	apr_dso_unload(m->handle);
	m->handle = NULL;
	return status;
}

static void *modlet_create_server_config(apr_pool_t *p, server_rec *s) {
	modlet_cfg *c = apr_pcalloc(p, sizeof *c);

	c->modlets = apr_hash_make(p);

	return c;
}

static void *modlet_merge_server_config(apr_pool_t *p, void *basev, void *addv) {
	modlet_cfg *base = basev;
	modlet_cfg *add = addv;
	modlet_cfg *c = apr_pcalloc(p, sizeof c);

	c->modlets = apr_hash_overlay(p, add->modlets, base->modlets);
	c->debug = add->debug || base->debug;

	return c;
}

static int modlet_handler(request_rec *r) {
	modlet_cfg *c = ap_get_module_config(r->server->module_config, &modlet_module);
	modlet_t *m = apr_hash_get(c->modlets, r->handler, strlen(r->handler));
	return m ? m->handle_request(r) : DECLINED;
}

static int modlet_post_config(apr_pool_t *pconf, apr_pool_t *plog, apr_pool_t *ptemp, server_rec *s) {
	apr_hash_index_t *hi;
	modlet_cfg *c;
	modlet_t *m;

	// walk base and merged server configs
	do {
		c = ap_get_module_config(s->module_config, &modlet_module);

		for (hi = apr_hash_first(ptemp, c->modlets); hi != NULL; hi = apr_hash_next(hi)) {
			apr_hash_this(hi, NULL, NULL, (void **) &m);

			if (modlet_load_modlet(s, pconf, m) != APR_SUCCESS) {
				return HTTP_INTERNAL_SERVER_ERROR;
			}
		}
	}
	while ((s = s->next));

	return OK;
}

static void modlet_register_hooks(apr_pool_t *p) {
	ap_hook_post_config(modlet_post_config, NULL, NULL, APR_HOOK_MIDDLE);
	ap_hook_handler(modlet_handler, NULL, NULL, APR_HOOK_MIDDLE);
}

static const char *modlet_cmd_modlet(cmd_parms *cmd, void *dummy, const char *name, const char *filename) {
	modlet_cfg *c = ap_get_module_config(cmd->server->module_config, &modlet_module);
	modlet_t *m = apr_pcalloc(cmd->pool, sizeof *m);

	m->image = apr_pstrdup(cmd->pool, filename);
	apr_hash_set(c->modlets, name, strlen(name), m);

	return NULL;
}

static const command_rec modlet_cmds[] = {
	AP_INIT_TAKE2 ("Modlet", modlet_cmd_modlet, NULL, RSRC_CONF, "The modlet to load"),
	AP_INIT_TAKE1 (0, 0, 0, 0, 0)
};

module AP_MODULE_DECLARE_DATA modlet_module = {
	STANDARD20_MODULE_STUFF,
	NULL,                         /* create per-dir    config structures */
	NULL,                         /* merge  per-dir    config structures */
	modlet_create_server_config,  /* create per-server config structures */
	modlet_merge_server_config,   /* merge  per-server config structures */
	modlet_cmds,                  /* table of config file commands       */
	modlet_register_hooks         /* register hooks                      */
};
