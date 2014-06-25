<?php

/**
 * @file
 * Drupal stream wrapper implementation for S3 File System.
 *
 * Implements DrupalStreamWrapperInterface to provide an Amazon S3 wrapper
 * using the "s3://" scheme.
 */

/**
 * The stream wrapper class.
 */
class S3fsStreamWrapper implements DrupalStreamWrapperInterface {
  
  /**
   * Stream context (this is set by PHP when a context is used).
   *
   * @var resource
   */
  public $context = NULL;
  
  /**
   * Instance URI referenced as "s3://key".
   *
   * @var string
   */
  protected $uri = NULL;
  
  /**
   * The AWS SDK S3Client object.
   *
   * @var Aws\S3\S3Client
   */
  protected $s3 = NULL;
  
  /**
   * Domain we use to access files over http.
   *
   * @var string
   */
  protected $domain = NULL;
  
  /**
   * Directory listing used by the dir_* methods.
   *
   * @var array
   */
  protected $dir = NULL;
  
  /**
   * Map for files that should be delivered with a torrent URL.
   *
   * @var array
   */
  protected $torrents = array();
  
  /**
   * Files that the user has said must be downloaded, rather than viewed.
   *
   * @var array
   */
  protected $saveas = array();
  
  /**
   * Files which should be created with URLs that eventually time out.
   *
   * @var array
   */
  protected $presignedURLs = array();
  
  /**
   * The constructor sets this to TRUE once it's finished.
   *
   * See the comment on _assert_constructor_called() for why this exists.
   *
   * @var bool
   */
  protected $constructed = FALSE;
  
  /**
   * Default map for determining file mime types.
   *
   * @var array
   */
  protected static $mimeTypeMapping = NULL;
  
  
  /**
   * Stream wrapper constructor.
   *
   * Creates the Aws\S3\S3Client client object and activates the options
   * specified on the S3 File System Settings page.
   */
  public function __construct() {
    // Since S3fsStreamWrapper is always constructed with the same inputs (the
    // file URI is not part of construction), store the constructed settings
    // statically. This is important for performance because Drupal
    // re-constructs stream wrappers very often.
    $settings = &drupal_static('S3fsStreamWrapper_constructed_settings');
    if ($settings !== NULL) {
      $this->config = $settings['config'];
      $this->s3 = _s3fs_get_amazons3_client($this->config);
      $this->domain = $settings['domain'];
      $this->torrents = $settings['torrents'];
      $this->presignedURLs = $settings['presignedURLs'];
      $this->saveas = $settings['saveas'];
      $this->constructed = TRUE;
      return;
    }
    $this->config = _s3fs_get_config();
    
    if (empty($this->config['bucket'])) {
      $msg = t('Your AmazonS3 bucket name is not configured. Please visit the !settings_page.',
        array('!settings_page' => l(t('configuration page'), '/admin/config/media/s3fs/settings')));
      watchdog('S3 File System', $msg, array(), WATCHDOG_ERROR);
      throw new Exception($msg);
    }
    
    // Get the S3 client object.
    $this->s3 = _s3fs_get_amazons3_client($this->config);
    
    // Always use HTTPS when the page is being served via HTTPS, to avoid
    // complaints from the browser about insecure content.
    global $is_https;
    if ($is_https) {
      // We change the config itself, rather than simply using $is_https in
      // the following if condition, because $this->config['use_https'] gets
      // used again later.
      $this->config['use_https'] = TRUE;
    }
    
    if (!empty($this->config['use_https'])) {
      $scheme = 'https';
      $this->_debug('Using HTTPS.');
    }
    else {
      $scheme = 'http';
      $this->_debug('Using HTTP.');
    }
    
    // CNAME support for customizing S3 URLs.
    // If use_cname is not enabled, file URLs do not use $this->domain.
    if (!empty($this->config['use_cname']) && !empty($this->config['domain'])) {
      $domain = check_url($this->config['domain']);
      if ($domain) {
        // If domain is set to a root-relative path, add the hostname back in.
        if (strpos($domain, '/') === 0) {
          $domain = $_SERVER['HTTP_HOST'] . $domain;
        }
        $this->domain = "$scheme://$domain";
      }
      else {
        // Due to the config form's validation, this shouldn't ever happen.
        throw new Exception(t('The "Use CNAME" option is enabled, but no Domain Name has been set. S3fsStreamWrapper construction cannot continue.'));
      }
    }
    
    // Convert the torrents string to an array.
    if (!empty($this->config['torrents'])) {
      foreach (explode("\n", $this->config['torrents']) as $line) {
        $blob = trim($line);
        if ($blob) {
          $this->torrents[] = $blob;
        }
      }
    }
    
    // Convert the presigned URLs string to an associative array like
    // array(blob => timeout).
    if (!empty($this->config['presigned_urls'])) {
      foreach (explode("\n", $this->config['presigned_urls']) as $line) {
        $blob = trim($line);
        if ($blob) {
          if (preg_match('/(.*)\|(.*)/', $blob, $matches)) {
            $blob = $matches[2];
            $timeout = $matches[1];
            $this->presignedURLs[$blob] = $timeout;
          }
          else {
            $this->presignedURLs[$blob] = 60;
          }
        }
      }
    }
    
    // Convert the forced save-as string to an array.
    if (!empty($this->config['saveas'])) {
      foreach (explode("\n", $this->config['saveas']) as $line) {
        $blob = trim($line);
        if ($blob) {
          $this->saveas[] = $blob;
        }
      }
    }
    
    // Save all the work we just did, so that subsequent S3fsStreamWrapper
    // constructions don't have to repeat it.
    $settings['config'] = $this->config;
    $settings['domain'] = $this->domain;
    $settings['torrents'] = $this->torrents;
    $settings['presignedURLs'] = $this->presignedURLs;
    $settings['saveas'] = $this->saveas;
    
    $this->constructed = TRUE;
    $this->_debug('S3fsStreamWrapper constructed.');
  }
  
  /***************************************************************************
  DrupalStreamWrapperInterface Implementations
  ***************************************************************************/
  
  /**
   * Static function to determine a file's media type.
   *
   * Uses Drupal's mimetype mapping, unless a different mapping is specified.
   *
   * @return string
   *   Returns a string representing the file's MIME type, or
   *   'application/octet-stream' if no type cna be determined.
   */
  public static function getMimeType($uri, $mapping = NULL) {
    self::_debug("getMimeType($uri, $mapping) called.");
    
    // Load the default mime type map.
    if (!isset(self::$mimeTypeMapping)) {
      include_once DRUPAL_ROOT . '/includes/file.mimetypes.inc';
      self::$mimeTypeMapping = file_mimetype_mapping();
    }
    
    // If a mapping wasn't specified, use the default map.
    if ($mapping == NULL) {
      $mapping = self::$mimeTypeMapping;
    }
    
    $extension = '';
    $file_parts = explode('.', basename($uri));
    
    // Remove the first part: a full filename should not match an extension.
    array_shift($file_parts);
    
    // Iterate over the file parts, trying to find a match.
    // For my.awesome.image.jpeg, we try:
    // - jpeg
    // - image.jpeg
    // - awesome.image.jpeg
    while ($additional_part = array_pop($file_parts)) {
      $extension = strtolower($additional_part . ($extension ? '.' . $extension : ''));
      if (isset($mapping['extensions'][$extension])) {
        return $mapping['mimetypes'][$mapping['extensions'][$extension]];
      }
    }
    
    // No mime types matches, so return the default.
    return 'application/octet-stream';
  }
  
  /**
   * Sets the stream resource URI. URIs are formatted as "s3://key".
   *
   * @param string $uri
   *   A string containing the URI that should be used for this instance.
   */
  public function setUri($uri) {
    $this->_debug("setUri($uri) called.");
    
    $this->uri = $uri;
  }
  
  /**
   * Returns the stream resource URI. URIs are formatted as "s3://key".
   *
   * @return string
   *   Returns the current URI of the instance.
   */
  public function getUri() {
    $this->_debug("getUri() called for {$this->uri}.");
    
    return $this->uri;
  }
  
  /**
   * Returns a web accessible URL for the resource.
   *
   * The format of the returned URL will be different depending on how the S3
   * integration has been configured on the S3 File System admin page.
   *
   * @return string
   *   Returns a string containing a web accessible URL for the resource.
   */
  public function getExternalUrl() {
    $this->_debug("getExternalUri() called for {$this->uri}.");
    
    $s3_filename = $this->_uri_to_s3_filename($this->uri);
    
    // Image styles support:
    // If an image derivative URL (e.g. styles/thumbnail/blah.jpg) is requested
    // and the file doesn't exist, provide a URL to s3fs's special version of
    // image_style_deliver(), which will create the derivative when that URL
    // gets requested.
    $path_parts = explode('/', $s3_filename);
    if ($path_parts[0] == 'styles') {
      if (!$this->_s3fs_get_object($this->uri)) {
        // The style delivery path looks like: s3/files/styles/<style>/...
        // And $path_parts looks like array('styles', '<style>', ...), so
        // just add the extra parts to the beginning.
        array_unshift($path_parts, 's3', 'files');
        return url(implode('/', $path_parts), array('absolute' => TRUE));
      }
    }
    
    // Set up the URL settings from the Settings page.
    $url_settings = array(
      'torrent' => FALSE,
      'presigned_url' => FALSE,
      'timeout' => 60,
      'forced_saveas' => FALSE,
      'api_args' => array('Scheme' => !empty($this->config['use_https']) ? 'https' : 'http'),
    );
    
    // Presigned URLs.
    foreach ($this->presignedURLs as $blob => $timeout) {
      // ^ is used as the delimeter because it's an illegal character in URLs.
      if (preg_match("^$blob^", $s3_filename)) {
        $url_settings['presigned_url'] = TRUE;
        $url_settings['timeout'] = $timeout;
        break;
      }
    }
    // Forced Save As.
    foreach ($this->saveas as $blob) {
      if (preg_match("^$blob^", $s3_filename)) {
        $filename = basename($s3_filename);
        $url_settings['api_args']['ResponseContentDisposition'] = "attachment; filename=\"$filename\"";
        $url_settings['forced_saveas'] = TRUE;
        break;
      }
    }
    
    // Allow other modules to change the URL settings.
    module_invoke_all('s3fs_url_info', $s3_filename, $url_settings);
    
    if (empty($this->config['use_cname'])) {
      $expires = NULL;
      if ($url_settings['presigned_url']) {
        $expires = "+{$url_settings['timeout']} seconds";
      }
      else {
        // Due to Amazon's security policies (see Request Parameters section @
        // http://docs.aws.amazon.com/AmazonS3/latest/API/RESTObjectGET.html),
        // only signed requests can use request parameters.
        // Thus, we must provide an expiry time for any URLs which specify
        // Response* API args. Currently, this only includes "Forced Save As".
        foreach ($url_settings['api_args'] as $key => $arg) {
          if (strpos($key, 'Response') === 0) {
            $expires = "+10 years";
            break;
          }
        }
      }
      $url = $this->s3->getObjectUrl($this->config['bucket'], $s3_filename, $expires, $url_settings['api_args']);
    }
    else {
      $url = "{$this->domain}/$s3_filename";
    }
    
    // Torrents can only be created for publicly-accessible files:
    // https://forums.aws.amazon.com/thread.jspa?threadID=140949
    // So Forced SaveAs and Presigned URLs cannot be served as torrents.
    if (!$url_settings['forced_saveas'] && !$url_settings['presigned_url']) {
      foreach ($this->torrents as $blob) {
        if (preg_match("^$blob^", $s3_filename)) {
          // A torrent URL is just a plain URL with "?torrent" on the end.
          $url .= '?torrent';
          break;
        }
      }
    }
    
    return $url;
  }
  
  /**
   * Returns the local writable target of the resource within the stream.
   *
   * This function should be used in place of calls to realpath() or similar
   * functions when attempting to determine the location of a file. While
   * functions like realpath() may return the location of a read-only file, this
   * method may return a URI or path suitable for writing that is completely
   * separate from the URI used for reading.
   *
   * @param string $uri
   *   Optional URI.
   *
   * @return array
   *   Returns a string representing a location suitable for writing of a file,
   *   or FALSE if unable to write to the file such as with read-only streams.
   */
  protected function getTarget($uri = NULL) {
    $this->_debug("getTarget($uri) called.");
    
    if (!isset($uri)) {
      $uri = $this->uri;
    }
    $data = explode('://', $uri, 2);
    // Remove erroneous leading or trailing forward-slashes and backslashes.
    return isset($data[1]) ? trim($data[1], '\/') : FALSE;
  }
  
  /**
   * Gets the path that the wrapper is responsible for.
   *
   * @return string
   *   The empty string. Since this is a remote stream wrapper,
   *   it has no directory path.
   */
  public function getDirectoryPath() {
    $this->_debug("getDirectoryPath() called.");
    
    return '';
  }
  
  /***************************************************************************
  PHP Stream Wrapper Implementations
  ***************************************************************************/
  
  /**
   * Changes permissions of the resource.
   *
   * This wrapper doesn't support the concept of filesystem permissions.
   *
   * @param int $mode
   *   Integer value for the permissions. Consult PHP chmod() documentation
   *   for more information.
   *
   * @return bool
   *   Returns TRUE.
   */
  public function chmod($mode) {
    $this->_assert_constructor_called();
    $octal_mode = decoct($mode);
    $this->_debug("chmod($octal_mode) called. S3fsStreamWrapper does not support this function.");
    
    return TRUE;
  }
  
  /**
   * This wrapper does not support realpath().
   *
   * @return bool
   *   Returns FALSE.
   */
  public function realpath() {
    $this->_debug("realpath() called for {$this->uri}. S3fsStreamWrapper does not support this function.");
    
    return FALSE;
  }
  
  /**
   * Gets the name of the directory from a given path.
   *
   * This method is usually accessed through drupal_dirname(), which wraps
   * around the normal PHP dirname() function, since it doesn't support stream
   * wrappers.
   *
   * @param string $uri
   *   An optional URI.
   *
   * @return string
   *   A string containing the directory name, or FALSE if not applicable.
   *
   * @see drupal_dirname()
   */
  public function dirname($uri = NULL) {
    $this->_debug("dirname($uri) called.");
    
    if (!isset($uri)) {
      $uri = $this->uri;
    }
    $target = $this->getTarget($uri);
    $dirname = dirname($target);
    
    // When the dirname() call above is given 's3://', it returns '.'.
    // But 's3://.' is invalid, so we convert it to '' to get "s3://".
    if ($dirname == '.') {
      $dirname = '';
    }
    return "s3://$dirname";
  }
  
  /**
   * Support for fopen(), file_get_contents(), file_put_contents() etc.
   *
   * @param string $uri
   *   A string containing the URI of the file to open.
   * @param string $mode
   *   The file mode. Only "r", "w", "a", and "x" are supported.
   * @param int $options
   *   A bit mask of STREAM_USE_PATH and STREAM_REPORT_ERRORS.
   * @param string $opened_path
   *   A string containing the path actually opened.
   *
   * @return bool
   *   Returns TRUE if file was opened successfully.
   *
   * @see http://php.net/manual/en/streamwrapper.stream-open.php
   */
  public function stream_open($uri, $mode, $options, &$opened_path) {
    $this->_debug("stream_open($uri, $mode, $options, $opened_path) called.");
    
    $this->uri = $uri;
    // We don't care about the binary flag, so strip it out.
    $this->mode = $mode = rtrim($mode, 'bt');
    $this->params = $this->_get_params($uri);
    $errors = array();
    
    if (strpos($mode, '+')) {
      $errors[] = t('The S3 File System stream wrapper does not allow simultaneous reading and writing.');
    }
    if (!in_array($mode, array('r', 'w', 'a', 'x'))) {
      $errors[] = t("Mode not supported: %mode. Use one 'r', 'w', 'a', or 'x'.", array('%mode' => $mode));
    }
    // When using mode "x", validate if the file exists first.
    if ($mode == 'x' && $this->_read_cache('s3://' . $this->params['Key'])) {
      $errors[] = t("%uri already exists in your S3 bucket, so it cannot be opened with mode 'x'.", array('%uri' => $uri));
    }
    
    if (!$errors) {
      if ($mode == 'r') {
        $this->_open_read_stream($this->params, $errors);
      }
      elseif ($mode == 'a') {
        $this->_open_append_stream($this->params, $errors);
      }
      else {
        $this->_open_write_stream($this->params, $errors);
      }
    }
    
    return $errors ? $this->_trigger_error($errors) : TRUE;
  }
  
  /**
   * Support for fclose().
   *
   * Clears the object buffer.
   *
   * @return bool
   *   TRUE
   *
   * @see http://php.net/manual/en/streamwrapper.stream-close.php
   */
  public function stream_close() {
    $this->_debug("stream_close() called for {$this->params['Key']}.");
    
    $this->body = NULL;
    $this->params = NULL;
    return TRUE;
  }
  
  /**
   * This wrapper does not support flock().
   *
   * @return bool
   *   returns FALSE.
   *
   * @see http://php.net/manual/en/streamwrapper.stream-lock.php
   */
  public function stream_lock($operation) {
    $this->_debug("stream_lock($operation) called. S3fsStreamWrapper doesn't support this function.");
    
    return FALSE;
  }
  
  /**
   * Support for fread(), file_get_contents() etc.
   *
   * @param int $count
   *   Maximum number of bytes to be read.
   *
   * @return string
   *   The string that was read, or FALSE in case of an error.
   *
   * @see http://php.net/manual/en/streamwrapper.stream-read.php
   */
  public function stream_read($count) {
    $this->_debug("stream_read($count) called for {$this->uri}.");
    
    return $this->body->read($count);
  }
  
  /**
   * Support for fwrite(), file_put_contents() etc.
   *
   * @param string $data
   *   The string to be written.
   *
   * @return int
   *   The number of bytes written.
   *
   * @see http://php.net/manual/en/streamwrapper.stream-write.php
   */
  public function stream_write($data) {
    $bytes = strlen($data);
    $this->_debug("stream_write() called with $bytes bytes of data for {$this->uri}.");
    
    return $this->body->write($data);
  }
  
  /**
   * Support for feof().
   *
   * @return bool
   *   TRUE if end-of-file has been reached.
   *
   * @see http://php.net/manual/en/streamwrapper.stream-eof.php
   */
  public function stream_eof() {
    $this->_debug("stream_eof() called for {$this->params['Key']}.");
    
    return $this->body->feof();
  }
  
  /**
   * Support for fseek().
   *
   * @param int $offset
   *   The byte offset to got to.
   * @param int $whence
   *   SEEK_SET, SEEK_CUR, or SEEK_END.
   *
   * @return bool
   *   TRUE on success.
   *
   * @see http://php.net/manual/en/streamwrapper.stream-seek.php
   */
  public function stream_seek($offset, $whence) {
    $this->_debug("stream_seek($offset, $whence) called.");
    
    return $this->body->seek($offset, $whence);
  }
  
  /**
   * Support for fflush(). Flush current cached stream data to a file in S3.
   *
   * @return bool
   *   TRUE if data was successfully stored (or there was no data to store).
   *
   * @see http://php.net/manual/en/streamwrapper.stream-flush.php
   */
  public function stream_flush() {
    $this->_debug("stream_flush() called for {$this->params['Key']}.");
    
    if ($this->mode == 'r') {
      return FALSE;
    }
    
    // Prep the upload parameters.
    $this->body->rewind();
    $upload_params = $this->params;
    $upload_params['Body'] = $this->body;
    // All files uploaded to S3 must be set to public-read, or users' browsers
    // will get PermissionDenied errors, and torrent URLs won't work.
    $upload_params['ACL'] = 'public-read';
    $upload_params['ContentType'] = S3fsStreamWrapper::getMimeType($this->uri);
    
    try {
      $this->s3->putObject($upload_params);
      $this->s3->waitUntilObjectExists($this->params);
      $metadata = $this->_get_metadata_from_s3($this->uri);
      if ($metadata === FALSE) {
        // This should never happen, but just in case...
        throw new Exception(t('Uploading the file %file to S3 failed in an unexpected way.', array('%file' => $this->uri)));
      }
      $this->_write_cache($metadata);
      clearstatcache(TRUE, $this->uri);
      return TRUE;
    }
    catch (\Exception $e) {
      $this->_debug($e->getMessage());
      return $this->_trigger_error($e->getMessage());
    }
  }
  
  /**
   * Support for ftell().
   *
   * @return int
   *   The current offset in bytes from the beginning of file.
   *
   * @see http://php.net/manual/en/streamwrapper.stream-tell.php
   */
  public function stream_tell() {
    $this->_debug("stream_tell() called.");
    
    return $this->body->ftell();
  }
  
  /**
   * Support for fstat().
   *
   * @return array
   *   An array with file status, or FALSE in case of an error - see fstat()
   *   for a description of this array.
   *
   * @see http://php.net/manual/en/streamwrapper.stream-stat.php
   */
  public function stream_stat() {
    $this->_debug("stream_stat() called for {$this->params['Key']}.");
    
    $stat = fstat($this->body->getStream());
    // Add the size of the underlying stream if it is known.
    if ($this->mode == 'r' && $this->body->getSize()) {
      $stat[7] = $stat['size'] = $this->body->getSize();
    }
    
    return $stat;
  }
  
  /**
   * Cast the stream to return the underlying file resource
   *
   * @param int $cast_as
   *   STREAM_CAST_FOR_SELECT or STREAM_CAST_AS_STREAM
   *
   * @return resource
   */
  public function stream_cast($cast_as) {
    $this->_debug("stream_cast($cast_as) called.");
    
    return $this->body->getStream();
  }
  
  /**
   * Support for unlink().
   *
   * @param string $uri
   *   A string containing the uri to the resource to delete.
   *
   * @return bool
   *   TRUE if resource was successfully deleted, regardless of whether or not
   *   the file actually existed.
   *   FALSE if the call to S3 failed, in which case the file will not be
   *   removed from the cache.
   *
   * @see http://php.net/manual/en/streamwrapper.unlink.php
   */
  public function unlink($uri) {
    $this->_assert_constructor_called();
    $this->_debug("unlink($uri) called.");
    
    try {
      $this->s3->deleteObject($this->_get_params($uri));
      $this->_delete_cache($uri);
      clearstatcache(TRUE, $uri);
      return TRUE;
    }
    catch (\Exception $e) {
      $this->_debug($e->getMessage());
      return $this->_trigger_error($e->getMessage());
    }
  }
  
  /**
   * Support for rename().
   *
   * If $to_uri exists, this file will be overwritten. This behavior is
   * identical to the PHP rename() function.
   *
   * @param string $from_uri
   *   The uri to the file to rename.
   * @param string $to_uri
   *   The new uri for file.
   *
   * @return bool
   *   TRUE if file was successfully renamed.
   *
   * @see http://php.net/manual/en/streamwrapper.rename.php
   */
  public function rename($from_uri, $to_uri) {
    $this->_assert_constructor_called();
    $this->_debug("rename($from_uri, $to_uri) called.");
    
    $from_params = $this->_get_params($from_uri);
    $to_params = $this->_get_params($to_uri);
    clearstatcache(TRUE, $from_uri);
    clearstatcache(TRUE, $to_uri);
    
    // Add the copyObject() parameters.
    $to_params['CopySource'] = "/{$from_params['Bucket']}/" . rawurlencode($from_params['Key']);
    $to_params['MetadataDirective'] = 'COPY';
    $to_params['ACL'] = 'public-read';
    
    try {
      // Copy the original object to the specified destination.
      $this->s3->copyObject($to_params);
      // Copy the original object's metadata.
      $metadata = $this->_read_cache($from_uri);
      $metadata['uri'] = $to_uri;
      $this->_write_cache($metadata);
      // Delete the original object.
      return $this->unlink($from_uri);
    }
    catch (\Exception $e) {
      $this->_debug($e->getMessage());
      return $this->_trigger_error($e->getMessage());
    }
  }
  
  /**
   * Support for mkdir().
   *
   * @param string $uri
   *   A string containing the URI to the directory to create.
   * @param int $mode
   *   Permission flags - see mkdir().
   * @param int $options
   *   A bit mask of STREAM_REPORT_ERRORS and STREAM_MKDIR_RECURSIVE.
   *
   * @return bool
   *   TRUE if directory was successfully created.
   *
   * @see http://php.net/manual/en/streamwrapper.mkdir.php
   */
  public function mkdir($uri, $mode, $options) {
    $this->_assert_constructor_called();
    $this->_debug("mkdir($uri, $mode, $options) called.");
    
    clearstatcache(TRUE, $uri);
    // If this URI already exists in the cache, return TRUE if it's a folder
    // (so that recursive calls won't improperly report failure when they
    // reach an existing ancestor), or FALSE if it's a file (failure).
    $test_metadata = $this->_read_cache($uri);
    if ($test_metadata) {
      return (bool) $test_metadata['dir'];
    }
    
    // S3 is a flat file system, with no concept of directories (just files
    // with slashes in their names). We store folders in the metadata cache,
    // but don't create anything in S3.
    $metadata = _s3fs_convert_metadata($uri, array());
    $metadata['timestamp'] = date('U', time());
    $this->_write_cache($metadata);
    
    // If the STREAM_MKDIR_RECURSIVE option was specified, also create all the
    // ancestor folders of this uri.
    $parent_dir = drupal_dirname($uri);
    if (($options & STREAM_MKDIR_RECURSIVE) && $parent_dir != 's3://') {
      return $this->mkdir($parent_dir, $mode, $options);
    }
    return TRUE;
  }
  
  /**
   * Support for rmdir().
   *
   * @param string $uri
   *   A string containing the URI to the folder to delete.
   * @param int $options
   *   A bit mask of STREAM_REPORT_ERRORS.
   *
   * @return bool
   *   TRUE if folder is successfully removed.
   *   FALSE if $uri isn't a folder, or the folder is not empty.
   *
   * @see http://php.net/manual/en/streamwrapper.rmdir.php
   */
  public function rmdir($uri, $options) {
    $this->_assert_constructor_called();
    $this->_debug("rmdir($uri, $options) called.");
    
    if (!$this->_uri_is_dir($uri)) {
      return FALSE;
    }
    
    // We need a version of the URI with no / (folders are cached with no /),
    // and a version with the /, in case it's an object in S3, and to
    // differentiate it from files with this folder's name as a substring.
    // e.g. rmdir('s3://foo/bar') should ignore s3://foo/barbell.jpg.
    $bare_uri = rtrim($uri, '/');
    $slash_uri = $bare_uri . '/';
    
    // Check if the folder is empty.
    $files = db_select('s3fs_file', 's')
      ->fields('s')
      ->condition('uri', db_like($slash_uri) . '%', 'LIKE')
      ->execute()
      ->fetchAll(PDO::FETCH_ASSOC);
    
    // If the folder is empty, it's eligible for deletion.
    if (empty($files)) {
      $result = $this->_delete_cache($bare_uri);
      clearstatcache(TRUE, $bare_uri);
      
      // Also delete the object from S3, if it's there.
      $params = $this->_get_params($slash_uri);
      try {
        if ($this->s3->doesObjectExist($params['Bucket'], $params['Key'])) {
          $this->s3->deleteObject($params);
        }
      }
      catch (\Exception $e) {
        $this->_debug($e->getMessage());
        return $this->_trigger_error($e->getMessage());
      }
      
      return (bool) $result;
    }
    
    // The folder is non-empty.
    return FALSE;
  }
  
  /**
   * Support for stat().
   *
   * @param string $uri
   *   A string containing the URI to get information about.
   * @param int $flags
   *   A bit mask of STREAM_URL_STAT_LINK and STREAM_URL_STAT_QUIET.
   *   S3fsStreamWrapper ignores this value.
   *
   * @return array
   *   An array with file status, or FALSE in case of an error - see fstat()
   *   for a description of this array.
   *
   * @see http://php.net/manual/en/streamwrapper.url-stat.php
   */
  public function url_stat($uri, $flags) {
    $this->_assert_constructor_called();
    $this->_debug("url_stat($uri, $flags) called.");
    
    return $this->_stat($uri);
  }
  
  /**
   * Support for opendir().
   *
   * @param string $uri
   *   A string containing the URI to the directory to open.
   * @param int $options
   *   A flag used to enable safe_mode.
   *   This parameter is ignored: this wrapper doesn't support safe_mode.
   *
   * @return bool
   *   TRUE on success.
   *
   * @see http://php.net/manual/en/streamwrapper.dir-opendir.php
   */
  public function dir_opendir($uri, $options = NULL) {
    $this->_assert_constructor_called();
    $this->_debug("dir_opendir($uri, $options) called.");
    
    if (!$this->_uri_is_dir($uri)) {
      return FALSE;
    }
    
    $bare_uri = rtrim($uri, '/');
    $slash_uri = $bare_uri . '/';
    
    // If this URI was originally s3://, the above code removed *both* slashes
    // but only added one back. So we need to add back the second slash.
    if ($slash_uri == 's3:/') {
      $slash_uri = 's3://';
    }
    
    // Get the list of uris for files and folders which are children of the
    // specified folder, but not grandchildren.
    $and = db_and();
    $and->condition('uri', db_like($slash_uri) . '%', 'LIKE');
    $and->condition('uri', db_like($slash_uri) . '%/%', 'NOT LIKE');
    $child_uris = db_select('s3fs_file', 's')
      ->fields('s', array('uri'))
      ->condition($and)
      ->execute()
      ->fetchCol(0);
    
    $this->dir = array();
    foreach ($child_uris as $child_uri) {
      $this->dir[] = basename($child_uri);
    }
    return TRUE;
  }
  
  /**
   * Support for readdir().
   *
   * @return string
   *   The next filename, or FALSE if there are no more files in the directory.
   *
   * @see http://php.net/manual/en/streamwrapper.dir-readdir.php
   */
  public function dir_readdir() {
    $this->_debug("dir_readdir() called.");
    
    $entry = each($this->dir);
    return $entry ? $entry['value'] : FALSE;
  }
  
  /**
   * Support for rewinddir().
   *
   * @return bool
   *   TRUE on success.
   *
   * @see http://php.net/manual/en/streamwrapper.dir-rewinddir.php
   */
  public function dir_rewinddir() {
    $this->_debug("dir_rewinddir() called.");
    
    reset($this->dir);
    return TRUE;
  }
  
  /**
   * Support for closedir().
   *
   * @return bool
   *   TRUE on success.
   *
   * @see http://php.net/manual/en/streamwrapper.dir-closedir.php
   */
  public function dir_closedir() {
    $this->_debug("dir_closedir() called.");
    
    unset($this->dir);
    return TRUE;
  }
  
  /***************************************************************************
  Internal Functions
  ***************************************************************************/
  
  /**
   * Convert a URI into a valid S3 filename.
   */
  protected function _uri_to_s3_filename($uri) {
    $filename = str_replace('s3://', '', $uri);
    // Remove both leading and trailing /s. S3 filenames never start with /,
    // and a $uri for a folder might be specified with a trailing /, which
    // we'd need to remove to be able to retrieve it from the cache.
    return trim($filename, '/');
  }
  
  /**
   * Get the status of the file with the specified URI.
   *
   * @return array
   *   An array with file status, or FALSE if the file doesn't exist.
   *   See fstat() for a description of this array.
   *
   * @see http://php.net/manual/en/streamwrapper.stream-stat.php
   */
  protected function _stat($uri) {
    $this->_debug("_stat($uri) called.", TRUE);
    
    $metadata = $this->_s3fs_get_object($uri);
    if ($metadata) {
      $stat = array();
      $stat[0]  = $stat['dev'] = 0;
      $stat[1]  = $stat['ino'] = 0;
      $stat[2]  = $stat['mode'] = $metadata['mode'];
      $stat[3]  = $stat['nlink'] = 0;
      $stat[4]  = $stat['uid'] = 0;
      $stat[5]  = $stat['gid'] = 0;
      $stat[6]  = $stat['rdev'] = 0;
      $stat[7]  = $stat['size'] = 0;
      $stat[8]  = $stat['atime'] = 0;
      $stat[9]  = $stat['mtime'] = 0;
      $stat[10] = $stat['ctime'] = 0;
      $stat[11] = $stat['blksize'] = 0;
      $stat[12] = $stat['blocks'] = 0;
      
      if (!$metadata['dir']) {
        $stat[4]  = $stat['uid'] = $metadata['uid'];
        $stat[7]  = $stat['size'] = $metadata['filesize'];
        $stat[8]  = $stat['atime'] = $metadata['timestamp'];
        $stat[9]  = $stat['mtime'] = $metadata['timestamp'];
        $stat[10] = $stat['ctime'] = $metadata['timestamp'];
      }
      return $stat;
    }
    return FALSE;
  }
  
  /**
   * Determine whether the $uri is a directory.
   *
   * @param string $uri
   *   A string containing the uri to the resource to check. If none is given
   *   defaults to $this->uri
   *
   * @return bool
   *   TRUE if the resource is a directory
   */
  protected function _uri_is_dir($uri) {
    if ($uri == 's3://' || $uri == 's3:') {
      return TRUE;
    }
    
    // Folders only exist in the cache, so we don't need to query S3.
    // Since they're stored with no ending slash, so we need to trim it.
    $uri = rtrim($uri, '/');
    $metadata = $this->_read_cache($uri);
    return $metadata ? $metadata['dir'] : FALSE;
  }
  
  /**
   * Try to fetch an object from the metadata cache.
   *
   * If that file isn't in the cache, we assume it doesn't exist.
   *
   * @param string $uri
   *   A string containing the uri of the resource to check.
   *
   * @return bool
   *   An array if the $uri exists, otherwise FALSE.
   */
  protected function _s3fs_get_object($uri) {
    $this->_debug("_s3fs_get_object($uri) called.", TRUE);
    
    // For the root directory, just return metadata for a generic folder.
    if ($uri == 's3://' || $uri == 's3:') {
      return _s3fs_convert_metadata('/', array());
    }
    
    // Trim any trailing '/', in case this is a folder request.
    $uri = rtrim($uri, '/');
    
    // Check if this URI is in the cache.
    $metadata = $this->_read_cache($uri);
    
    // If cache ignore is enabled, query S3 for all URIs which aren't in the
    // cache, and non-folder URIs which are.
    if (!empty($this->config['ignore_cache']) && !$metadata['dir']) {
      try {
        // If _get_metadata_from_s3() returns FALSE, the file doesn't exist.
        $metadata = $this->_get_metadata_from_s3($uri);
      }
      catch (\Exception $e) {
        $this->_debug($e->getMessage());
        return $this->_trigger_error($e->getMessage());
      }
    }
    
    return $metadata;
  }
  
  /**
   * Fetch an object from the file metadata cache table.
   *
   * @param string $uri
   *   A string containing the uri of the resource to check.
   *
   * @return array
   *   An array of metadata if the $uri is in the cache, otherwise FALSE.
   */
  protected function _read_cache($uri) {
    $this->_debug("_read_cache($uri) called.", TRUE);
    
    $record = db_select('s3fs_file', 's')
      ->fields('s')
      ->condition('uri', $uri, '=')
      ->execute()
      ->fetchAssoc();
    return $record ? $record : FALSE;
  }
  
  /**
   * Write an object's (and its ancestor folders') metadata to the cache.
   *
   * @param array $metadata
   *   An associative array of file metadata, in this format:
   *     'uri' => The full URI of the file, including 's3://'.
   *     'filesize' => The size of the file, in bytes.
   *     'timestamp' => The file's create/update timestamp.
   *     'dir' => A boolean indicating whether the object is a directory.
   *     'mode' => The octal mode of the file.
   *     'uid' => The uid of the owner of the S3 object.
   *
   * @throws
   *   Exceptions which occur in the database call will percolate.
   */
  protected function _write_cache($metadata) {
    $this->_debug("_write_cache({$metadata['uri']}) called.", TRUE);
    
    db_merge('s3fs_file')
      ->key(array('uri' => $metadata['uri']))
      ->fields($metadata)
      ->execute();
    
    $dirname = $this->dirname($metadata['uri']);
    if ($dirname != 's3://') {
      $this->mkdir($dirname, NULL, STREAM_MKDIR_RECURSIVE);
    }
  }
  
  /**
   * Delete an object's metadata from the cache.
   *
   * @param mixed $uri
   *   A string (or array of strings) containing the URI(s) of the object(s)
   *   to be deleted.
   *
   * @throws
   *   Exceptions which occur in the database call will percolate.
   */
  protected function _delete_cache($uri) {
    $this->_debug("_delete_cache($uri) called.", TRUE);
    
    $delete_query = db_delete('s3fs_file');
    if (is_array($uri)) {
      // Build an OR condition to delete all the URIs in one query.
      $or = db_or();
      foreach ($uri as $u) {
        $or->condition('uri', $u, '=');
      }
      $delete_query->condition($or);
    }
    else {
      $delete_query->condition('uri', $uri, '=');
    }
    return $delete_query->execute();
  }
  
  /**
   * Get the stream context options available to the current stream.
   *
   * @return array
   */
  protected function _get_options() {
    $context = isset($this->context) ? $this->context : stream_context_get_default();
    $options = stream_context_get_options($context);
    return isset($options['s3']) ? $options['s3'] : array();
  }
  
  /**
   * Get a specific stream context option
   *
   * @param string $name Name of the option to retrieve
   *
   * @return mixed|null
   */
  protected function _get_option($name) {
    $options = $this->_get_options();
    return isset($options[$name]) ? $options[$name] : NULL;
  }
  
  /**
   * Get the Command parameters for the specified URI.
   *
   * @param string $uri
   *   The URI of the file.
   *
   * @return array
   *   A Command parameters array, including 'Bucket', 'Key', and
   *   context params.
   */
  protected function _get_params($uri) {
    $params = $this->_get_options();
    unset($params['seekable']);
    unset($params['throw_exceptions']);
    
    $params['Bucket'] = $this->config['bucket'];
    // Strip s3:// from the URI to get the S3 Key.
    $params['Key'] = $this->_uri_to_s3_filename($uri);
    return $params;
  }
  
  /**
   * Initialize the stream wrapper for a read only stream.
   *
   * @param array $params
   *   A Command parameters array.
   * @param array $errors
   *   Array to which encountered errors should be appended.
   *
   * @return bool
   */
  protected function _open_read_stream($params, &$errors) {
    $this->_debug("_open_read_stream({$params['Key']}) called.", TRUE);
    
    // Create the command and serialize the request.
    $request = $this->_get_signed_request($this->s3->getCommand('GetObject', $params));
    // Create a stream that uses the EntityBody object.
    $factory = $this->_get_option('stream_factory');
    if (empty($factory)) {
      $factory = new Guzzle\Stream\PhpStreamRequestFactory();
    }
    $this->body = $factory->fromRequest($request, array(), array('stream_class' => 'Guzzle\Http\EntityBody'));
    
    // Wrap the body in an S3fsSeekableCachingEntityBody, so that seeks can
    // go to not-yet-read sections of the file.
    if (class_exists('S3fsSeekableCachingEntityBody')) {
      $this->body = new S3fsSeekableCachingEntityBody($this->body);
    }
    
    return TRUE;
  }
  
  /**
   * Initialize the stream wrapper for an append stream.
   *
   * @param array $params
   *   A Command parameters array.
   * @param array $errors
   *   Array to which encountered errors should be appended.
   *
   * @return bool
   */
  protected function _open_append_stream($params, &$errors) {
    $this->_debug("_open_append_stream({$params['Key']}) called.", TRUE);
    
    try {
      // Get the body of the object
      $this->body = $this->s3->getObject($params)->get('Body');
      $this->body->seek(0, SEEK_END);
    }
    catch (Aws\S3\Exception\S3Exception $e) {
      // The object does not exist, so use a simple write stream.
      $this->_open_write_stream($params, $errors);
    }
    
    return true;
  }
  
  /**
   * Initialize the stream wrapper for a write only stream.
   *
   * @param array $params
   *   A Command parameters array.
   * @param array $errors
   *   Array to which encountered errors should be appended.
   *
   * @return bool
   */
  protected function _open_write_stream($params, &$errors) {
    $this->_debug("_open_write_stream({$params['Key']}) called.", TRUE);
    
    $this->body = new Guzzle\Http\EntityBody(fopen('php://temp', 'r+'));
  }
  
  /**
   * Serialize and sign a command, returning a request object
   *
   * @param CommandInterface $command Command to sign
   *
   * @return RequestInterface
   */
  protected function _get_signed_request($command) {
    $this->_debug("_get_signed_request() called.", TRUE);
    
    $request = $command->prepare();
    $request->dispatch('request.before_send', array('request' => $request));
    return $request;
  }
  
  /**
   * Returns the converted metadata for an object in S3.
   *
   * @param string $uri
   *   The URI for the object in S3.
   *
   * @return array
   *   An array of DB-compatible file metadata.
   *
   * @throws \Exception
   *   Any exception raised by the listObjects() S3 command will percolate
   *   out of this function.
   */
  function _get_metadata_from_s3($uri) {
    $this->_debug("_get_metadata_from_s3($uri) called.", TRUE);
    
    $params = $this->_get_params($uri);
    // I wish we could call headObject(), rather than listObjects(), but
    // headObject() doesn't return the object's owner ID.
    $result = $this->s3->listObjects(array(
      'Bucket' => $params['Bucket'],
      'Prefix' => $params['Key'],
      'MaxKeys' => 1,
    ));
    
    // $result['Contents'][0] is the s3 metadata. If it's unset, there is no
    // file in S3 matching this URI.
    if (isset($result['Contents'][0])) {
      return _s3fs_convert_metadata($uri, $result['Contents'][0]);
    }
    return FALSE;
  }
  
  /**
   * Triggers one or more errors.
   *
   * @param string|array $errors
   *   Errors to trigger.
   * @param mixed $flags
   *   If set to STREAM_URL_STAT_QUIET, then no error or exception occurs.
   *
   * @return bool
   *   Returns FALSE.
   * @throws RuntimeException
   *   If the 'throw_exceptions' option is TRUE.
   */
  protected function _trigger_error($errors, $flags = NULL) {
    if ($flags != STREAM_URL_STAT_QUIET) {
      if ($this->_get_option('throw_exceptions')) {
        throw new RuntimeException(implode("\n", (array) $errors));
      }
      else {
        trigger_error(implode("\n", (array) $errors), E_USER_WARNING);
      }
    }
    return FALSE;
  }
  
  /**
   * Call the constructor it it hasn't been has been called yet.
   *
   * Due to PHP bug #40459, the constructor of this class isn't always called
   * for some of the methods.
   *
   * @see https://bugs.php.net/bug.php?id=40459
   */
  protected function _assert_constructor_called() {
    if ($this->constructed === FALSE) {
      $this->__construct();
    }
  }
  
  /**
   * Logging function used for debugging.
   *
   * This function only writes anything if the global variable $_s3fs_debug
   * is TRUE.
   *
   * @param string $msg
   *   The message to debug log.
   * @param bool $internal
   *   If this is TRUE, don't log $msg unless $_s3fs_debug_internal is TRUE.
   */
  protected static function _debug($msg, $internal = FALSE) {
    global $_s3fs_debug, $_s3fs_debug_internal;
    if ($_s3fs_debug && (!$internal || $_s3fs_debug_internal)) {
      debug($msg);
    }
  }
}

// Guzzle\Http\CachingEntityBody is only defined once the SDK has been loaded,
// so we need to load it before we can inherit from it.
if (function_exists('libraries_load')) {
  $library = libraries_load('awssdk2');
  if ($library['loaded']) {
    /**
     * A replacement class for CachingEntityBody that serves better for s3fs.
     *
     * Any instantiation of this class must be wrapped in a check for its
     * existence, since it may not be defined under certain circumstances.
     */
    class S3fsSeekableCachingEntityBody extends Guzzle\Http\CachingEntityBody {
      
      /**
       * This version of seek() allows seeking past the end of the cache.
       *
       * If the caller attempts to seek more than 50 megs into the file,
       * though, an exception will be thrown, because that would take up too
       * much memory.
       */
      public function seek($offset, $whence = SEEK_SET) {
        if ($whence == SEEK_SET) {
          $byte = $offset;
        }
        else if ($whence == SEEK_CUR) {
          $byte = $offset + $this->ftell();
        }
        else {
          throw new RuntimeException(__CLASS__ . ' supports only SEEK_SET and SEEK_CUR seek operations');
        }
        
        if ($byte > 52428800) {
          throw new RuntimeException(
            "Seeking more than 50 megabytes into a remote file is not supported, due to memory constraints.
              If you need to bypass this error, please contact the maintainers of S3 File System."
          );
        }
        
        // If the caller tries to seek past the end of the currently cached
        // data, read in enough of the remote stream to let the seek occur.
        while ($byte > $this->body->getSize() && !$this->isConsumed()) {
          $this->read(16384);
        }
        
        return $this->body->seek($byte);
      }
    }
  }
}
