<?php

/**
 * @file
 * Defines a drush command that refreshes the S3 metadata cache.
 */

/**
 * Implements hook_drush_command().
 */
function s3fs_drush_command() {
  $items = array();
  $items['s3fs-refresh-cache'] = array(
    'description' => t("Refreshes the S3 File System metadata cache. Your Amazon Web Services credentials, and the name of your site's bucket, must be configured first."),
    'aliases' => array('s3fs-rc'),
    'callback' => 'drush_s3fs_refresh_cache',
  );
  return $items;
}

/**
 * Refreshes the file metadata cache.
 */
function drush_s3fs_refresh_cache() {
  $config = _s3fs_get_config();
  if (!empty($config['bucket'])) {
    _s3fs_refresh_cache($config);
  }
  else {
    drupal_set_message(t('Unable to determine Amazon S3 bucket name for cache refresh. Please set the bucket name in the S3 File System configuration settings and try again.'), 'error');
  }
}
