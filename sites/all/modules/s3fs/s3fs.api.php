<?php

/**
 * @file
 * This file contains no working PHP code; it exists to provide additional
 * documentation for doxygen as well as to document hooks in the standard
 * Drupal manner.
 */

/**
 * @defgroup s3fs_hooks S3 File System hooks
 * Hooks that can be implemented by other modules to extend S3 File System.
 */

/**
 * Alters the format and options used when creating an external URL.
 *
 * For example the URL can be a URL directly to the file, or can be a URL to a
 * torrent. In addition, it can be authenticated (time limited), and in that
 * case a save-as can be forced.
 *
 * @param string $local_path
 *   The local filesystem path.
 * @param array $settings
 *   Associative array of URL settings:
 *     - 'torrent': (boolean) Should the file should be sent via BitTorrent?
 *     - 'presigned_url': (boolean) Triggers use of an authenticated URL.
 *     - 'timeout': (int) Time in seconds before a pre-signed URL times out.
 *     - 'api_args': array of additional arguments to the getObject() function:
 *       http://docs.aws.amazon.com/aws-sdk-php/latest/class-Aws.S3.S3Client.html#_getObject
 *
 * @return array
 *   The modified array of configuration items.
 */
function hook_s3fs_url_info($local_path, &$url_settings) {
  // An example of what you might want to do with this hook.
  if ($local_path == 'myfile.jpg') {
    $url_settings['presigned_url'] = TRUE;
    $url_settings['timeout'] = 10;
  }
}
