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

sub vcl_recv {
  if (req.request != "GET" &&
    req.request != "HEAD" &&
    req.request != "PUT" &&
    req.request != "POST" &&
    req.request != "TRACE" &&
    req.request != "OPTIONS" &&
    req.request != "DELETE") {
      /* Non-RFC2616 or CONNECT which is weird. */
      return (pipe);
  }

  if (req.request != "GET" && req.request != "HEAD") {
    /* We only deal with GET and HEAD by default */
    return (pass);
  }

  // Remove has_js and Google Analytics cookies.
  // set req.http.Cookie = regsuball(req.http.Cookie, "(^|;\s*)(__[a-z]+|has_js)=[^;]*", "");
  set req.http.Cookie = regsuball(req.http.Cookie, "(^|;\s*)(__[a-z]+|has_js|Drupal.toolbar.collapsed|Drupal.tableDrag.showWeight)=[^;]*", "");

  // To users: if you have additional cookies being set by your system (e.g.
  // from a javascript analytics file or similar) you will need to add VCL
  // at this point to strip these cookies from the req object, otherwise
  // Varnish will not cache the response. This is safe for cookies that your
  // backed (Drupal) doesn't process.
  //
  // Again, the common example is an analytics or other Javascript add-on.
  // You should do this here, before the other cookie stuff, or by adding
  // to the regular-expression above.


  // Remove a ";" prefix, if present.
  set req.http.Cookie = regsub(req.http.Cookie, "^;\s*", "");
  // Remove empty cookies.
  if (req.http.Cookie ~ "^\s*$") {
    unset req.http.Cookie;
  }

//  if (req.http.Authorization || req.http.Cookie) {
  if (req.http.Authorization) {
    /* Not cacheable by default */
    return (pass);
  }

  // Skip the Varnish cache for install, update, and cron
  if (req.url ~ "install\.php|update\.php|cron\.php") {
    return (pass);
  }

  // Normalize the Accept-Encoding header
  // as per: http://varnish-cache.org/wiki/FAQ/Compression
  if (req.http.Accept-Encoding) {
    if (req.url ~ "\.(jpg|png|gif|gz|tgz|bz2|tbz|mp3|ogg)$") {
      # No point in compressing these
      remove req.http.Accept-Encoding;
    }
    elsif (req.http.Accept-Encoding ~ "gzip") {
      set req.http.Accept-Encoding = "gzip";
    }
    else {
      # Unknown or deflate algorithm
      remove req.http.Accept-Encoding;
    }
  }

  // Let's have a little grace
  set req.grace = 30s;

  return (lookup);
}

 sub vcl_deliver {
   if (obj.hits > 0) {
     set resp.http.X-Cache = "HIT";
   } else {
     set resp.http.X-Cache = "MISS";
   }
 }

 // Strip any cookies before an image/js/css is inserted into cache.
 sub vcl_fetch {
   if (req.url ~ "\.(png|gif|jpg|swf|css|js)$") {
     unset beresp.http.set-cookie;
   }

  /* if (!beresp.cacheable) {
     set beresp.http.X-Cacheable = "NO:Not Cacheable";
   } elsif (req.http.Cookie ~ "(UserID|_session)") {
     set beresp.http.X-Cacheable = "NO:Got Session";
     return(pass);
   } elsif (beresp.http.Cache-Control ~ "private") {
     set beresp.http.X-Cacheable = "NO:Cache-Control=private";
     return(pass);
   } elsif (beresp.ttl < 1s) {
     set beresp.ttl   = 5s;
     set beresp.grace = 5s;
     set beresp.http.X-Cacheable = "YES:FORCED";
   } else {
     set beresp.http.X-Cacheable = "YES";
   }*/
 }
