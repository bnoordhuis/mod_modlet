#include "http_protocol.h"

int handle_request(request_rec *r) {
	ap_set_content_type(r, "text/html; charset=UTF-8");
	ap_rputs("Hello, modlet world!", r);
	return OK;
}
