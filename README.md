## mod_modlet

Hassle-free module authoring for [Apache 2](http://httpd.apache.org/).

Example code:

	#include "http_protocol.h"

	int handle_request(request_rec *r) {
		ap_set_content_type(r, "text/html; charset=UTF-8");
		ap_rputs("Hello, modlet world!", r);
		return OK;
	}

Save as `hello.c` and compile it with:

	apxs2 -c hello.c

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
