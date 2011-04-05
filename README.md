## mod_modlet

Hassle-free module authoring for [Apache 2](http://httpd.apache.org/).

Example code:

	#include "http_protocol.h"

	int handle_request(request_rec *r) {
		apr_table_set(r->headers_out, "Content-Type", "text/html; charset=UTF-8");
		ap_rputs("Hello, modlet world!", r);
		return OK;
	}

Example config:

	LoadModule modlet_module /path/to/mod_modlet.so
	<VirtualHost *:80>
		ServerName 127.0.0.1
		Modlet hello /path/to/hello.so

		<Location /hello>
			SetHandler hello
		</Location>
	</VirtualHost>

Restart Apache and test it:

	$ curl http://127.0.0.1/hello
	Hello, modlet world!
