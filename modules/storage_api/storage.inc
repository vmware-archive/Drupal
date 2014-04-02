<?php


class Storage {

  /**
   * Loads a storage, either from the database or from an array.
   *
   * @param $storage
   *   Either a storage_id or an array.
   *
   * @throws StorageException
   *   When the storage is not found.
   */
  public function __construct($storage) {

    if (!is_array($storage)) {
      // Avoid querying the database if the storage_id is NULL.
      if (!$storage) {
        throw new StorageException();
      }

      // Either the file is being deleted and no longer has a selector,
      // or we grab the selector with migrating = 0.
      $query = db_select('storage');
      $query->leftJoin('storage_file', NULL, 'storage.file_id = storage_file.file_id');
      $query->leftJoin('storage_selector', NULL, 'storage.selector_id = storage_selector.selector_id');
      $query->leftJoin('storage_class', NULL, 'storage_selector.class_id = storage_class.class_id');

      $storage = $query->fields('storage')
        ->fields('storage_file')
        ->fields('storage_selector')
        ->fields('storage_class')
        ->condition('storage.storage_id', $storage)
        ->condition(db_or()
          ->isNull('storage.selector_id')
          ->condition('storage_selector.migrating', 0))
        ->execute()
        ->fetchAssoc();

      if (!$storage) {
        throw new StorageException();
      }
    }

    if (isset($storage['data'])) {
      $storage['data'] = unserialize($storage['data']);
    }
    
    // Even if the storage doesn't yet have a file_id, it may have a filename.
    if (isset($storage['data']['filename'])) {
      $storage['filename'] = $storage['data']['filename'];
    }

    if (isset($storage['options'])) {
      storage_unserialize_class_options($storage['options']);
    }

    if (isset($storage['serving_container'])) {
      $storage['serving_container'] = unserialize($storage['serving_container']);
    }

    foreach ($storage as $key => $value) {
      $this->$key = $value;
    }
  }

  public function update() {
    drupal_write_record('storage', $this, 'storage_id');
  }

  public function duplicate() {
    $storage = clone $this;
    unset($storage->storage_id);
    drupal_write_record('storage', $storage);

    $message = 'Storage duplicated:<br />' . $storage->logInfo();
    watchdog('storage', $message);

    return $storage;
  }

  public function selector() {

    return storage_selector($this->selector_id);
  }

  public function module() {

    return $this->selector()->module();
  }

  public function parent() {

    return storage_load($this->parent_id);
  }

  public function addChild(StorageSelector $selector, array $options) {

    $options['parent_id'] = $this->storage_id;
    return $selector->storageAdd($options);
  }

  public function children() {
    $children = array();

    $storage_ids = db_select('storage')
      ->fields('storage', array('storage_id'))
      ->condition('parent_id', $this->storage_id)
      ->isNotNull('selector_id')
      ->execute()
      ->fetchCol();

    foreach ($storage_ids as $storage_id) {
      $children[] = storage_load($storage_id);
    }

    return $children;
  }

  /**
   * Determine whether the user has access to a file.
   *
   * @return
   *   Boolean TRUE if the current user has access to the file.
   */
  public function access() {

    $results = module_invoke_all('storage_access_alter', $this);
    
    foreach ($results as $result) {
      if (isset($result)) {
        return $result;
      }
    }
    
    $result = module_invoke($this->module(), 'storage_access', $this);
    
    if (isset($result)) {
      return $result;
    }

    try {
      // Fall back to the access control of the parent storage.
      return $this->parent()->access();
    }
    catch (StorageException $e) {
      return TRUE;
    }
  }


  /**
   * Selects a container instance.
   *
   * @param $serving
   *   Set to TRUE if the instance is to be served.
   *
   * @return
   *   The container instance.
   *
   * @throws StorageException
   *   When there is no container instance.
   */
  public function selectContainerInstance($serving = FALSE) {
    // Avoid querying the database altogether if there is no file_id. This is
    // also currently necessary due to a core bug:
    // http://drupal.org/node/813540
    if (!$this->file_id) {
      throw new StorageException();
    }

    $query = db_select('storage_instance');
    $query->join('storage_class_container', NULL, 'storage_instance.container_id = storage_class_container.container_id');
    $query->join('storage_selector', NULL, 'storage_class_container.class_id = storage_selector.class_id');
    $query->join('storage_container', NULL, 'storage_instance.container_id = storage_container.container_id');

    if ($serving) {
      $query->condition('storage_class_container.serving', 1);
    }

    $row = $query->condition('storage_instance.file_id', $this->file_id)
      ->condition('storage_selector.selector_id', $this->selector_id)
      ->condition('storage_container.status', STORAGE_CONTAINER_STATUS_ACTIVE)
      ->fields('storage_container')
      ->fields('storage_instance', array('reference'))
      ->orderBy('storage_selector.migrating')
      ->orderBy('storage_class_container.weight')
      ->orderBy('storage_class_container.container_id')
      ->range(0, 1)
      ->execute()
      ->fetchAssoc();

    if ($row) {
      return storage_container_new($row);
    }
    
    // Let's see if the file is in the storage's initial container.
    $container = storage_container_load($this->initial_container_id);

    $container->reference = db_select('storage_instance')
      ->fields('storage_instance', array('reference'))
      ->condition('storage_instance.container_id', $container->container_id)
      ->condition('storage_instance.file_id', $this->file_id)
      ->range(0, 1)
      ->execute()
      ->fetchField();

    if (!$container->reference) {
      throw new StorageException();
    }

    return $container;
  }


  /**
   * Generates the storage.
   *
   * @return
   *   The initial container instance.
   *
   * @throws StorageException
   *   When it isn't possible to generate the storage.
   */
  private function generate() {
    // Get the lock.
    $lock = new StorageLock('generate:' . $this->storage_id);

    // Check if the storage is still ungenerated.
    try {
      return $this->selectContainerInstance(TRUE);
    }
    catch (StorageException $e) {}

    $container = storage_container_load($this->initial_container_id);
    $uri = $this->getUri();

    // If the storage's file is already in the system, then the instance might
    // already exist.
    if (empty($new_file)) {
      $reference = $container->instanceExists($this);
    }

    if (empty($reference)) {
      try {
        $reference = $container->instanceCreate($this, $uri);
      }
      catch (StorageException $e) {
        $message = 'Failed to create instance of file ' . ($this->filename ? ($this->filename . ' ') : '');
        $message .= 'in storage container <i>' . check_plain($container->name()) . '</i>.';

        if (user_access('administer storage')) {
          drupal_set_message($message);
        }

        $message .= ' <br />' . $this->logInfo();
        watchdog('storage', $message, NULL, WATCHDOG_WARNING);
        
        throw new StorageException();
      }
    }

    $this->update();

    $message = 'Generated file' . ($this->filename ? (' ' . $this->filename) : '');
    $message .= '<br />' . $this->logInfo();
    watchdog('storage', $message);
    
    $container->reference = $reference;
    
    return $container;
  }

  /**
   * Serve the specified file, either directly or by an HTTP 302 redirect.
   */
  public function serve() {
    $container = NULL;

    if ($this->serving_container) {
      $container = $this->serving_container;
    }
    else {
      try {
        try {
          $container = $this->selectContainerInstance(TRUE);
        }
        catch (StorageException $e) {
          $container = $this->generate();
        }
        $this->cacheServingContainer($container);
      }
      catch (StorageException $e) {
        $this->cacheNoServingContainer();
      }
    }

    // Allow other modules to react to this file being served.
    module_invoke_all('storage_serving', $this, $container);

    if ($container) {
      if (!$container->service()->serve ||
        ($container->access_control && !$container->service()->serve_secure))
      {
        // This will not return.
        $this->serveSecure($container);
      }
      else {
        // The URL redirected to should have the same scheme as the current
        // request.
        $https = storage_is_request_https();
        $url = $container->serviceInstanceServe($container->reference, $https);
      }
    }
    elseif ($this->options['serve_source_uri'] && $this->source_uri) {

      $url = file_create_url($this->source_uri);
    }
    
    if (!isset($url)) {
      _storage_service_unavailable();
      return;
    }
    
    // This will not return.
    drupal_goto($url);    // 302 redirect (don't cache)
  }

  private function serveSecure(StorageContainerInterface $container) {
    $this->setHTTPHeaders();
    $container->serviceInstanceOutput($container->reference);
    drupal_exit();
  }

  private function dataUri(StorageContainerInterface $container) {
    $uri = 'data:' . str_replace(' ', '', $this->mimetype);
    $uri .= ';base64,';
    $uri .= base64_encode($container->serviceInstanceGetContents($container->reference));
    return $uri;
  }

  /**
   * Generates a URL that will serve the supplied file.
   *
   * If the files's class is set to serve by redirection, then the URL will be
   * on your site. Otherwise, Storage API will determine which container to
   * serve from and generate a direct URL.
   *
   * @param bool $absolute
   *   Whether the generated URL should be absolute. Defaults to FALSE.
   * @param bool|null $https
   *   Whether the URL should use the HTTPS scheme. If NULL it is inherited from
   *   the request currently being handled. Defaults to NULL.
   *
   * @return
   *   The generated URL.
   */
  public function serveURL($absolute = FALSE, $https = NULL) {
    if (is_null($https)) {
      $https = storage_is_request_https();
    }

    if ($this->options['redirect'] || ($this->serving_container === FALSE)  // FALSE means there is no container.
      || !$this->access())
    {
      return url($this->path(), array('absolute' => $absolute));
    }

    if ($this->serving_container) {
      $container = $this->serving_container;
    }
    else {
      try {
        $container = $this->selectContainerInstance(TRUE);
        $this->cacheServingContainer($container);
      }
      catch (StorageException $e) {
        $this->cacheNoServingContainer();
      }
    }

    if (isset($container)) {
      if ($container->service()->serve &&
        (!$container->access_control || $container->service()->serve_secure))
      {
        if ($this->options['data_uri']) {
          $url = $this->dataUri($container);
        }
        else {
          $url = $container->serviceInstanceServe($container->reference, $https);
        }
      }
    }
    elseif ($this->options['serve_source_uri'] && $this->source_uri) {

      $url = file_create_url($this->source_uri);
    }

    if (!isset($url)) {
      $url = $this->path();
    }

    return url($url, array(
      'absolute' => $absolute,
      'https' => $https,
    ));
  }


  public function path() {
    return 'system/storage/serve/' . $this->storage_id . ($this->filename ? ('/' . basename($this->filename)) : '');
  }


  public function logInfo() {

    $info = 'class: ' . $this->name . ', <br />';
    
    $info .= 'storage_id: ' . $this->storage_id;
    $info .= $this->file_id ? (', file_id: ' . $this->file_id) : '';
    $info .= $this->size ? (', size: ' . storage_format_byte_count($this->size)) : '';

    return $info;
  }


  public function fileLogInfo() {

    $info = 'file_id: ' . $this->file_id . ', ';
    $info .= 'size: ' . $this->size();

    return $info;
  }
  
  
  public function size() {
    return storage_format_byte_count($this->size);
  }


  /**
   * Schedule a storage for complete removal from the system.
   *
   * All instances of the storage will be removed during cron.
   */
  public function remove($recursive = TRUE) {

    db_update('storage')
      ->fields(array(
        'status' => STORAGE_STATUS_PROCESS_CRON,
        'serving_container' => serialize(FALSE),
      ))
      ->expression('selector_id', 'NULL')
      ->condition('storage_id', $this->storage_id)
      ->execute();
    
    // Allow other modules to react to this storage being removed.
    module_invoke_all('storage_remove', $this);

    $message = 'File removed' . ($this->filename ? (': ' . $this->filename . ' ') : '') . ' <br />';
    $message .= $this->logInfo();
    
    watchdog('storage', $message, NULL);

    if ($recursive) {
      foreach($this->children() as $child) {
        $child->remove();
      }
    }
  }


  /**
   * Generate HTTP headers for serving this storage.
   *
   * @return
   *   An array of HTTP headers.
   */
  public function HTTPHeaders() {
    $headers[] = 'Content-Type: ' . $this->mimetype;
    $headers[] = 'Content-Length: ' . $this->size;
    $headers[] = 'Content-MD5: ' . base64_encode($this->md5);
    $headers[] = 'Last-Modified: ' . date(DATE_RFC1123, $this->created);
    $headers[] = 'Content-Disposition: attachment; filename="' . basename($this->filename) . '"';

    return $headers;
  }


  /**
   * Output directly the correct HTTP headers for a specific file.
   */
  public function setHTTPHeaders() {

    // Unset the headers we don't want (they still get sent unfortunately).
    header('Set-Cookie:');
    header('Expires:');
    
    foreach ($this->HTTPHeaders() as $header) {
      header($header);
    }
  }


  private function acquireFromSourceURL() {
    $time = time();

    if ($time < $this->data['next_acquire_time'])
      return;
      
    $uri = _storage_acquire_from_url($this->source_uri);
    
    if ($uri) {
      $file_id = _storage_file_id($uri, NULL, $new_file);

      if (!$this->file_id) {
        $this->file_id = $file_id;
      }
      
      if ($this->file_id != $file_id) {    // the file has changed

        _storage_delete_file_if_unrequired($file_id);
        drupal_unlink($uri);
        $uri = FALSE;
      }
      else {
        unset($this->data['next_acquire_time']);
        unset($this->data['next_acquire_delay']);
        $this->update();
        
        // we need to reload $storage to get the filesize, etc
//          $this = storage_load($this->storage_id);
        $this->new_file = $new_file;    // _storage_ensure_instance_exists() will make use of this

        $message = 'Acquired file from url: ' . $this->source_uri . ' . <br />';
        $message .= $this->logInfo();

        watchdog('storage', $message);

        return $uri;
      }
    }
    
    if ($uri === FALSE) {   // hard failure

      unset($this->data['next_acquire_time']);
      unset($this->data['next_acquire_delay']);
      unset($this->source_uri);
      
      $this->update();

      $message = 'Failed acquiring file from url: ' . $this->source_uri . ' . <br />';
      $message .= $this->logInfo();

      watchdog('storage', $message, NULL, WATCHDOG_WARNING);

      return FALSE;
    }
    
    
    // soft failure
    
    if ($this->data['next_acquire_time']) {
    
      $this->data['next_acquire_time'] += $this->data['next_acquire_delay'];
      $this->data['next_acquire_delay'] *= 2;
    }
    else {
      $this->data['next_acquire_time'] = $time + 60;
      $this->data['next_acquire_delay'] = 120;
    }
    
    $this->update();
  }


  /**
   * Gets a URI that can be used to access a file locally.
   *
   * The file will be made available locally (this may involve downloading it or generating it).
   *
   * @return
   *   A URI where the file can be accessed.
   *
   * @throws StorageException
   *   When it is not possible to get a URI.
   */
  public function getUri() {

    try {
      $container = $this->selectContainerInstance();
      return $container->serviceInstanceGetUri($container->reference);
    }
    catch (StorageException $e) {}

    if ($this->source_uri) {

      try {
        $uri = $this->acquireFromSourceURL();
      }
      catch (StorageException $e) {}

      if (isset($uri)) {
        return $uri;
      }
    }

    if ($uri = module_invoke($this->module(), 'storage_generate', $this)) {

      try {
        $file_id = _storage_file_id($uri, $this->filename, $new_file);
      }
      catch (StorageException $e) {
        throw new StorageException();
      }
      
      if ($this->file_id && ($this->file_id != $file_id)) {
      
        // The storage's file_id is changing. If there are any instances of the
        // old file, create a dummy storage so these instances can be destroyed
        // if necessary.
        $count = db_select('storage_instance')
          ->condition('file_id', $this->file_id)
          ->countQuery()
          ->execute()
          ->fetchField();
          
        if ($count > 0) {
          
          db_insert('storage')
            ->fields(array(
              'file_id' => $this->file_id,
              'created' => $this->created,
              'data' => '',
            ))
            ->execute();
        }
      }

      if ($this->file_id != $file_id) {
        $this->file_id = $file_id;
        unset($this->data['filename']);
        $this->update();

        // we need to reload $storage to get the filesize, etc
        $storage = storage_load($this->storage_id);
        
        foreach ($storage as $key => $value) {
          $this->$key = $value;
        }
        
        $this->new_file = $new_file;    // StorageContainer::ensureInstanceExists() will make use of this
      }
            
      return $uri;
    }

    throw new StorageException();
  }


  public function cacheServingContainer(StorageContainerInterface $container) {
    $this->serving_container = serialize($container);

    db_update('storage')
      ->fields(array('serving_container' => $this->serving_container))
      ->condition('storage_id', $this->storage_id)
      ->execute();
  }


  public function cacheNoServingContainer() {
    $this->serving_container = FALSE;

    db_update('storage')
      ->fields(array('serving_container' => $this->serving_container))
      ->condition('storage_id', $this->storage_id)
      ->execute();
  }


  public function flushServingContainer() {
    unset($this->serving_container);

    db_update('storage')
      ->expression('serving_container', 'NULL')
      ->condition('storage_id', $this->storage_id)
      ->execute();
  }
  
  /**
   * @throws StorageException
   *   When the storage is unable to be correctly propagated.
   */
  public function check() {

    // If the storage has a single instance in a container that is not
    // propagating, then we are done.
    if ($this->file_id) {
      $query = db_select('storage_instance');
      $query->join('storage_class_container', NULL, 'storage_instance.container_id = storage_class_container.container_id');
      $query->join('storage_selector', NULL, 'storage_class_container.class_id = storage_selector.class_id');

      $count = $query->condition('storage_instance.file_id', $this->file_id)
        ->condition('storage_class_container.propagate', 0)
        ->condition('storage_selector.selector_id', $this->selector_id)
        ->countQuery()
        ->execute()
        ->fetchField();

      if ($count) {
        return;
      }
    }

    // Get all containers this storage is supposed to have an instance in.
    $query = db_select('storage_selector', NULL, array('fetch' => PDO::FETCH_ASSOC));
    $query->join('storage_class', NULL, 'storage_selector.class_id = storage_class.class_id');
    $query->join('storage_class_container', NULL, 'storage_class.class_id = storage_class_container.class_id');
    $query->join('storage_container', NULL, 'storage_class_container.container_id = storage_container.container_id');
    
    $result = $query->fields('storage_container')
      ->fields('storage_class_container')
      ->condition('storage_selector.selector_id', $this->selector_id)
      ->condition('storage_selector.migrating', 0)
      ->orderBy('storage_class_container.weight')
      ->orderBy('storage_container.container_id')
      ->execute();
    
    $container_ids = array();
    
    $failure = FALSE;
    
    foreach ($result as $fields) {
      $container = storage_container_new($fields);
      $container_ids[] = $container->container_id;
      
      // Don't attempt to propagate to an external container, a container that
      // isn't propagating or a container that is being removed. But don't
      // destroy the instance in the container either.
      if ($container->external || !$container->propagate || $container->remove) {
        continue;
      }

      try {
        $container->ensureInstanceExists($this);
      }
      catch (StorageException $e) {
        $failure = TRUE;
      }
    }
    
    if ($failure) {
      throw new StorageException();
    }

    // The storage is fully propagated, now we can destroy unrequired instances.
    $this->destroyUnrequiredInstances($container_ids);
    
    if ($this->selector_id) {
      $this->initial_container_id = NULL;
      $this->status = STORAGE_STATUS_IDLE;
      $this->update();
    }
    else {
      // This storage can now be deleted.
      db_delete('storage')
        ->condition('storage_id', $this->storage_id)
        ->execute();

      _storage_delete_file_if_unrequired($this->file_id);
    }
  }

  
  /**
   * @throws StorageException
   *   When not all unrequired instances could be destroyed.
   */
  public function destroyUnrequiredInstances(array $container_ids) {

    // Find any instances of the storage's file that are in containers the
    // storage is not supposed to be in.
    $query = db_select('storage_instance', NULL, array('fetch' => PDO::FETCH_ASSOC));
    $query->join('storage_container', NULL, 'storage_instance.container_id = storage_container.container_id');

    $query->fields('storage_container')
      ->fields('storage_instance', array('reference'))
      ->condition('storage_instance.file_id', $this->file_id)
      ->orderBy('storage_container.container_id');

    if (count($container_ids)) {
      $query->condition('storage_instance.container_id', $container_ids, 'NOT IN');
    }

    $failure = FALSE;

    foreach ($query->execute() as $row) {
      $reference = $row['reference'];
      $container = storage_container_new($row);

      try {
        $container->instanceDestroyIfUnrequired($this, $reference);
      }
      catch (StorageException $e) {
        $failure = TRUE;
      }
    }
    
    if ($failure) {
      throw new StorageException();
    }
  }
}

