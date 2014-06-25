# This is a basic VCL configuration file for varnish.  See the vcl(7)
# man page for details on VCL syntax and semantics.
# 
# Default backend definition.  Set this to point to your content
# server.
# 
#backend default {
#    .host = "99.0.112.16";
#    .port = "80";
#}
backend default {
     .host = "127.0.0.1";
     .port = "8888";
     .connect_timeout = 600s;
     .first_byte_timeout = 600s;
     .between_bytes_timeout = 600s;
 }
