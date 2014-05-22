<?php

/**
* Pivotal Cloudfoundry customizations 
*/
$services = getenv("VCAP_SERVICES");
$services_json = json_decode($services,true);
$mysql_config = $services_json["p-mysql"][0]["credentials"];
$databases['default']['default'] = array(
'driver' => 'mysql',
'database' => $mysql_config["name"],
'username' => $mysql_config["username"],
'password' => $mysql_config["password"],
'host' => $mysql_config["hostname"],
'port' =>  $mysql_config["port"],
'prefix' => 'main_',
'collation' => 'utf8_general_ci',
);



# Pull User Provided Services credentials out of VCAP_SERVICES env
if (isset($services_json["user-provided"]))	{
	$ups_config = $services_json["user-provided"];
	foreach ($ups_config as $ups) {
		$ups_name = $ups["name"];
		if ($ups_name == "drupal-varnish") {
			$varnish_creds = $ups["credentials"];
		} else if ($ups_name == "drupal-memcached") {
			$memcached_creds = $ups["credentials"];
		} else if ($ups_name == "drupal-s3") {
			$s3_creds = $ups["credentials"];
		}
	}
}

/**
* Varnish configuration
**/
if (isset($varnish_creds))	{
	$conf['cache_backends'][] = 'sites/all/modules/varnish/varnish.cache.inc';
	$conf['cache_class_cache_page'] = 'VarnishCache';
	$conf['reverse_proxy'] = TRUE;
	$conf['page_cache_invoke_hooks'] = FALSE;
	$conf['cache'] = 1;
	$conf['cache_lifetime'] = 0;
	$conf['page_cache_maximum_age'] = 21600;
	$conf['reverse_proxy_header'] = 'HTTP_X_FORWARDED_FOR';
	$conf['reverse_proxy_addresses'] = explode(",", $varnish_creds["cluster"]);
	$conf['omit_vary_cookie'] = TRUE;
}

/**
* Memcached configuration
**/
if (isset($memcached_creds))	{
	$conf['cache_backends'][] = 'sites/all/modules/memcache/memcache.inc';
	$conf['cache_default_class'] = 'MemCacheDrupal';
	$conf['cache_class_cache_form'] = 'DrupalDatabaseCache';

	$memcached_cluster = explode(",", $memcached_creds["cluster"]);
	$mem_servers = array();
	foreach ($memcached_cluster as $server) {
		$mem_servers[$server] = "cluster1";
	}

	$conf['memcache_servers'] = $mem_servers;
	$conf['memcache_bins'] = array(
	  'cache' => 'cluster1'
	);
}

/**
* S3 configuration
**/
if (isset($s3_creds))	{	
	$conf['awssdk2_access_key'] = $s3_creds["access_key"];
	$conf['awssdk2_secret_key'] = $s3_creds["secret_key"];
	$conf['s3fs_bucket'] = $s3_creds["bucket"];
	
	if (isset($s3_creds["hostname"])) {
		$conf['s3fs_hostname'] = $s3_creds["hostname"];
		$conf['s3fs_use_customhost'] = TRUE;
	}
	
	if (isset($s3_creds["domain"])) {
		$conf['s3fs_use_cname'] = TRUE;
		$conf['s3fs_domain'] = $s3_creds["domain"];
	}
	
	$conf['s3fs_use_https'] = $s3_creds["https"];
}


/**
 * Access control for update.php script.
 *
 * If you are updating your Drupal installation using the update.php script but
 * are not logged in using either an account with the "Administer software
 * updates" permission or the site maintenance account (the account that was
 * created during installation), you will need to modify the access check
 * statement below. Change the FALSE to a TRUE to disable the access check.
 * After finishing the upgrade, be sure to open this file again and change the
 * TRUE back to a FALSE!
 */
$update_free_access = FALSE;

/**
 * Salt for one-time login links and cancel links, form tokens, etc.
 *
 * This variable will be set to a random value by the installer. All one-time
 * login links will be invalidated if the value is changed. Note that if your
 * site is deployed on a cluster of web servers, you must ensure that this
 * variable has the same value on each server. If this variable is empty, a hash
 * of the serialized database credentials will be used as a fallback salt.
 *
 * For enhanced security, you may set this variable to a value using the
 * contents of a file outside your docroot that is never saved together
 * with any backups of your Drupal files and database.
 *
 * Example:
 *   $drupal_hash_salt = file_get_contents('/home/example/salt.txt');
 *
 */
$drupal_hash_salt = '';

/**
 * Base URL (optional).
 *
 * If Drupal is generating incorrect URLs on your site, which could
 * be in HTML headers (links to CSS and JS files) or visible links on pages
 * (such as in menus), uncomment the Base URL statement below (remove the
 * leading hash sign) and fill in the absolute URL to your Drupal installation.
 *
 * You might also want to force users to use a given domain.
 * See the .htaccess file for more information.
 *
 * Examples:
 *   $base_url = 'http://www.example.com';
 *   $base_url = 'http://www.example.com:8888';
 *   $base_url = 'http://www.example.com/drupal';
 *   $base_url = 'https://www.example.com:8888/drupal';
 *
 * It is not allowed to have a trailing slash; Drupal will add it
 * for you.
 */
# $base_url = 'http://www.example.com';  // NO trailing slash!

/**
 * PHP settings:
 *
 * To see what PHP settings are possible, including whether they can be set at
 * runtime (by using ini_set()), read the PHP documentation:
 * http://www.php.net/manual/ini.list.php
 * See drupal_environment_initialize() in includes/bootstrap.inc for required
 * runtime settings and the .htaccess file for non-runtime settings. Settings
 * defined there should not be duplicated here so as to avoid conflict issues.
 */

/**
 * Some distributions of Linux (most notably Debian) ship their PHP
 * installations with garbage collection (gc) disabled. Since Drupal depends on
 * PHP's garbage collection for clearing sessions, ensure that garbage
 * collection occurs by using the most common settings.
 */
ini_set('session.gc_probability', 1);
ini_set('session.gc_divisor', 100);

/**
 * Set session lifetime (in seconds), i.e. the time from the user's last visit
 * to the active session may be deleted by the session garbage collector. When
 * a session is deleted, authenticated users are logged out, and the contents
 * of the user's $_SESSION variable is discarded.
 */
ini_set('session.gc_maxlifetime', 200000);

/**
 * Set session cookie lifetime (in seconds), i.e. the time from the session is
 * created to the cookie expires, i.e. when the browser is expected to discard
 * the cookie. The value 0 means "until the browser is closed".
 */
ini_set('session.cookie_lifetime', 2000000);

/**
 * Fast 404 pages:
 *
 * Drupal can generate fully themed 404 pages. However, some of these responses
 * are for images or other resource files that are not displayed to the user.
 * This can waste bandwidth, and also generate server load.
 *
 * The options below return a simple, fast 404 page for URLs matching a
 * specific pattern:
 * - 404_fast_paths_exclude: A regular expression to match paths to exclude,
 *   such as images generated by image styles, or dynamically-resized images.
 *   If you need to add more paths, you can add '|path' to the expression.
 * - 404_fast_paths: A regular expression to match paths that should return a
 *   simple 404 page, rather than the fully themed 404 page. If you don't have
 *   any aliases ending in htm or html you can add '|s?html?' to the expression.
 * - 404_fast_html: The html to return for simple 404 pages.
 *
 * Add leading hash signs if you would like to disable this functionality.
 */
$conf['404_fast_paths_exclude'] = '/\/(?:styles)\//';
$conf['404_fast_paths'] = '/\.(?:txt|png|gif|jpe?g|css|js|ico|swf|flv|cgi|bat|pl|dll|exe|asp)$/i';
$conf['404_fast_html'] = '<!DOCTYPE html PUBLIC "-//W3C//DTD XHTML+RDFa 1.0//EN" "http://www.w3.org/MarkUp/DTD/xhtml-rdfa-1.dtd"><html xmlns="http://www.w3.org/1999/xhtml"><head><title>404 Not Found</title></head><body><h1>Not Found</h1><p>The requested URL "@path" was not found on this server.</p></body></html>';

