#include "http_protocol.h"

int handle_request(request_rec *r) {
	apr_table_set(r->headers_out, "Content-Type", "text/html; charset=UTF-8");
	ap_rputs("Hello, modlet world!", r);
	return OK;
}
