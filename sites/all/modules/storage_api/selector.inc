<?php


class StorageSelector {

  public $selector_id;

  public function __construct($arg0, $arg1 = NULL) {
    
    if (!is_null($arg1)) {
      $this->selector_id = $arg0 . ':' . $arg1;
    }
    else {
      $this->selector_id = $arg0;
    }
  }
  
  /**
   * Creates a new class selector.
   */
  public function create($class_id = NULL) {

    if (!$class_id) {
      $class_id = db_select('storage_class')
        ->fields('storage_class', array('class_id'))
        ->orderBy('name')
        ->orderBy('class_id')
        ->range(0, 1)
        ->execute()
        ->fetchField();
    }

    db_insert('storage_selector')
      ->fields(array(
        'selector_id' => $this->selector_id,
        'class_id' => $class_id,
      ))
      ->execute();
  }
  
  /**
   * Deletes a class selector.
   */
  public function destroy() {

    db_update('storage')
      ->fields(array(
        'status' => STORAGE_STATUS_PROCESS_CRON,
        'serving_container' => serialize(FALSE),
      ))
      ->expression('selector_id', 'NULL')
      ->condition('selector_id', $this->selector_id)
      ->execute();

    db_delete('storage_selector')
      ->condition('selector_id', $this->selector_id)
      ->execute();
  }

  public function module() {
    return strtok($this->selector_id, ':');
  }

  /**
   * Generates a FAPI select element for a class selector.
   *
   * After form submission, pass the form value for the element to
   * storage_selector_submit().
   *
   * @param $title
   *   The title of the select element.
   * @param $description
   *   The description of the select element (optional).
   * @return
   *   The select element.
   */
  public function formItem($title = NULL, $description = NULL) {
    $selector = array(
      '#tree' => TRUE
    );

    $result = db_select('storage_class', NULL, array('fetch' => PDO::FETCH_ASSOC))
      ->fields('storage_class')
      ->orderBy('name')
      ->orderBy('class_id')
      ->execute();
    
    $classes = array();

    foreach ($result as $class) {
      $classes[$class['class_id']] = $class['name'];
    }

    if (!$description) {
      $text = "This can be safely changed at any time.<br />";
      $text .= "Configure storage classes <a href=\"!path\">here</a>.";

      $description = t($text, array('!path' => url('admin/structure/storage')));
    }

    $selector['class_id'] = array(
      '#type' => 'select',
      '#title' => $title ? $title : t("Storage class"),
      '#description' => $description,
      '#options' => $classes,
    );

    $current_class_id = db_select('storage_selector')
      ->fields('storage_selector', array('class_id'))
      ->condition('selector_id', $this->selector_id)
      ->condition('migrating', 0)
      ->execute()
      ->fetchField();

    $selector['class_id']['#default_value'] = $current_class_id;

    $selector['current_class_id'] = array(
      '#type' => 'value',
      '#value' => $current_class_id
    );

    return $selector;
  }
  
  /**
   * Processes a class selector FAPI element.
   *
   * The element's class selector is updated to point at the class selected.
   * Migration of files to the new class is initiated.
   *
   * @param $form_item
   *   The form value of a submitted class selector FAPI element.
   */
  public function submit(array $form_item) {
    // Has the class been changed?
    if ($form_item['current_class_id'] == $form_item['class_id']) {
      return;
    }

    db_merge('storage_selector')
      ->key(array(
        'selector_id' => $this->selector_id,
        'class_id' => $form_item['class_id'],
      ))
      ->fields(array('migrating' => 0))
      ->execute();
    
    // Are there any files to migrate?
    $num_files = db_select('storage')
      ->condition('selector_id', $this->selector_id)
      ->countQuery()
      ->execute()
      ->fetchField();

    if ($num_files) {
      db_merge('storage_selector')
        ->key(array(
          'selector_id' => $this->selector_id,
          'class_id' => $form_item['current_class_id'],
        ))
        ->fields(array('migrating' => 1))
        ->execute();

      db_update('storage')
        ->fields(array(
          'status' => STORAGE_STATUS_PROCESS_CRON,
        ))
        ->expression('serving_container', 'NULL')
        ->condition('selector_id', $this->selector_id)
        ->execute();
      
      $new_class = storage_class_load($form_item['class_id']);
      
      $message = 'Migrating ' . $num_files . ' ' . format_plural($num_files, 'file', 'files');
      $message .= ' to storage class <i>' . check_plain($new_class->name) . '</i>.';
      
      drupal_set_message($message);
      watchdog('storage', $message, NULL);
    }
    else {
    
      db_delete('storage_selector')
        ->condition('selector_id', $this->selector_id)
        ->condition('class_id', $form_item['current_class_id'])
        ->execute();
    }
  }

  public function storageClass() {

    return storage_class_load(db_select('storage_selector')
      ->fields('storage_selector', array('class_id'))
      ->condition('selector_id', $this->selector_id)
      ->condition('migrating', 0)
      ->execute()
      ->fetchField());
  }
  
  public function checkMigration() {
    // Is the selector fully migrated?
    $migrated = !db_select('storage')
      ->condition('selector_id', $this->selector_id)
      ->condition('status', STORAGE_STATUS_PROCESS_CRON)
      ->countQuery()
      ->execute()
      ->fetchField();

    if ($migrated) {
      // Remove the classes it was migrating from.
      db_delete('storage_selector')
        ->condition('selector_id', $this->selector_id)
        ->condition('migrating', '1')
        ->execute();
        
      $message = 'Storage selector ' . check_plain($this->selector_id);
      $message .= ' has been fully migrated to class <i>' . check_plain($this->storageClass()->name) . '</i>.';
      
      watchdog('storage', $message, NULL);
    }
  }

  /**
   * Adds a storage to the selector.
   *
   * If the selector's class has an initial container, then an instance of the
   * storage will be created there.
   *
   * @param $options
   *   An associative array of additional options, with the following keys:
   *     - 'source_uri'
   *       URI to get the file from.
   *     - 'keep_source_uri'
   *       source_uri will be stored in the database.
   *     - 'file_id'
   *       If the same file is already in the system, its file_id can be
   *       specified.
   *     - 'filename'
   *       The file's filename.
   *     - 'parent_id'
   *       The storage_id of the storage this storage is derived from.
   *     - 'created' (default current time)
   *       Unix timestamp of when the storage was created.
   *     - 'data'
   *       An array of data about the file.
   *
   * @return
   *   Storage of the added file.
   *
   * @throws StorageException
   *   When it isn't possible to add the storage to the selector.
   */
  public function storageAdd(array $options = array()) {
    // Add defaults.
    $options += array(
      'selector_id' => $this->selector_id,
      'source_uri' => NULL,
      'keep_source_uri' => FALSE,
      'file_id' => NULL,
      'filename' => NULL,
      'parent_id' => NULL,
      'created' => time(),
      'data' => array(),
    );

    $initial_container = $this->storageClass()->initial_container;

    if ($initial_container) {
      $options['initial_container_id'] = $initial_container->container_id;
    }

    if ($options['source_uri']) {
      if ($initial_container) {
        $source_uri = $options['source_uri'];   // TODO: Make this URI local.

        if (!$options['file_id']) {
          $options['file_id'] = _storage_file_id($source_uri, $options['filename'], $new_file);
        }
      }

      if (!$options['keep_source_uri']) {
        $options['source_uri'] = NULL;
      }
    }
    elseif ($options['filename']) {
      // Store the filename so it can be in the generator URL.
      $options['data']['filename'] = $options['filename'];
    }

    drupal_write_record('storage', $options);
    $storage = storage_load($options['storage_id']);

    if (isset($source_uri)) {
      // If the storage's file is already known to the system, then the
      // instance might already exist.
      $exists = $initial_container->instanceExists($storage);

      if (!$exists) {
        try {
          $initial_container->instanceCreate($storage, $source_uri);
        }
        catch (StorageException $e) {
          $message = 'Failed to create initial instance of file ' . $storage->filename;
          $message .= ' in storage container <i>' . check_plain($initial_container->name()) . '</i>.';

          if (user_access('administer storage')) {
            drupal_set_message($message);
          }

          $message .= ' <br />' . $storage->logInfo();
          watchdog('storage', $message, NULL, WATCHDOG_WARNING);

          $storage->remove();
          throw new StorageException();
        }
      }
    }

    $message = 'File added: ' . $storage->filename . '<br />';
    $message .= $storage->logInfo();

    watchdog('storage', $message);

    return $storage;
  }
}
