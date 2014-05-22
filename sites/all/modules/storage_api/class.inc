<?php


class StorageClass {

  public $name;
  public $options;

  /**
   * @throws StorageException
   *   When $class is not an array and the class cannot be found.
   */
  public function __construct($class = NULL) {
  
    if (!isset($class)) {
      return;
    }
  
    if (!is_array($class)) {
    
      $class = db_select('storage_class')
        ->fields('storage_class')
        ->condition('class_id', $class)
        ->execute()
        ->fetchAssoc();

      if (!$class) {
        throw new StorageException();
      }

      storage_unserialize_class_options($class['options']);
    }
    
    foreach ($class as $key => $value) {
      $this->$key = $value;
    }
      
    try {
      if (isset($this->options['initial_container_id'])) {
        $this->initial_container = storage_container_load($this->options['initial_container_id']);
      }
    }
    catch (StorageException $e) {}
  }

  public function path() {
    return 'admin/structure/storage/class/' . $this->class_id;
  }
  
  public function link() {
    return l($this->name, $this->path());
  }
  
  public function count() {
    $query = db_select('storage_selector');
    $query->join('storage', NULL, 'storage_selector.selector_id = storage.selector_id');

    return $query->condition('storage_selector.class_id', $this->class_id)
      ->countQuery()
      ->execute()
      ->fetchField();
  }

  public function size($format = FALSE) {
    $query = db_select('storage_selector');
    $query->join('storage', NULL, 'storage_selector.selector_id = storage.selector_id');
    $query->join('storage_file', NULL, 'storage.file_id = storage_file.file_id');
    $query->addExpression('SUM(storage_file.size)');

    $size = (int) $query->condition('storage_selector.class_id', $this->class_id)
      ->execute()
      ->fetchField();

    return $format ? storage_format_byte_count($size) : $size;
  }

  public function selectorIds() {

    return db_select('storage_selector')
      ->fields('storage_selector', array('selector_id'))
      ->condition('class_id', $this->class_id)
      ->execute()
      ->fetchCol();
  }

  public function processStorages() {

    $selector_ids = $this->selectorIds();

    if (empty($selector_ids)) {
      return;
    }

    db_update('storage')
      ->fields(array('status' => STORAGE_STATUS_PROCESS_CRON))
      ->expression('serving_container', 'NULL')
      ->condition('selector_id', $selector_ids)
      ->execute();
  }

  public function addContainer(StorageContainerInterface $container) {
  
    db_insert('storage_class_container')
      ->fields(array(
        'class_id' => $this->class_id,
        'container_id' => $container->container_id,
      ))
      ->execute();
    
    if (empty($this->initial_container)) {
      $this->options['initial_container_id'] = $container->container_id;
      $this->update();
    }

    $this->processStorages();

    $msg = 'Storage container !container has been added to class !class.';
    $args = array(
      '!container' => $container->link(), 
      '!class' => $this->link(),
    );
    
    drupal_set_message(t($msg, $args));
    watchdog('storage', $msg, $args, WATCHDOG_NOTICE, l(t('view'), $this->path()));
  }

  /**
   * Ensure it is safe to remove the container from the class.
   *
   * @throws StorageException
   *   When the container cannot be safely removed from the class.
   */
  private function removeContainerEnsureSafe(StorageContainerInterface $container) {
    $selector_ids = $this->selectorIds();

    // If the class has no storages, then it is definately safe to remove a
    // container.
    if (empty($selector_ids)) {
      return;
    }

    // Get a distinct list of all file_ids of instances in other containers
    // in the class.
    $subquery = db_select('storage_class_container');
    $subquery->join('storage_instance', NULL, 'storage_class_container.container_id = storage_instance.container_id');

    $subquery->condition('storage_class_container.class_id', $this->class_id)
      ->condition('storage_class_container.container_id', $container->container_id, '<>')
      ->fields('storage_instance', array('file_id'))
      ->distinct();

    // Are there any instances of storages in the class that are in the container to be
    // removed, but not in another container in the class?
    $query = db_select('storage');
    $query->join('storage_instance', NULL, 'storage.file_id = storage_instance.file_id');

    $count = $query->condition('storage.file_id', $subquery, 'NOT IN')
      ->condition('storage.selector_id', $selector_ids)
      ->condition('storage_instance.container_id', $container->container_id)
      ->countQuery()
      ->execute()
      ->fetchField();

    // If any data-loss would occur, abort.
    if ($count > 0) {
      throw new StorageException();
    }
  }

  /**
   * Removes a container from the class.
   *
   * @param $container
   *   The container to be removed.
   *
   * @throws StorageException
   *   When the container cannot be safely removed from the class.
   */
  public function removeContainer(StorageContainerInterface $container) {

    // Ensure it is safe to remove the container from the class.
    $this->removeContainerEnsureSafe($container);

    db_delete('storage_class_container')
      ->condition('class_id', $this->class_id)
      ->condition('container_id', $container->container_id)
      ->execute();

    $this->processStorages();

    cache_clear_all();

    $message = "Storage container %container has been removed from class %class.";
    $replacements = array(
      '%container' => $container->name(),
      '%class' => $this->name,
    );

    drupal_set_message(t($message, $replacements));
    watchdog('storage', $message, $replacements, WATCHDOG_NOTICE, l(t('view'), $this->path()));
  }
  
  public function insert() {
    drupal_write_record('storage_class', $this);
  }

  public function update() {
    drupal_write_record('storage_class', $this, 'class_id');
  }
  
  public function create() {
    $this->insert();
    
    $message = "Storage class !class has been created.";
    $replacements = array('!class' => $this->link());

    drupal_set_message(t($message, $replacements));
    watchdog('storage', $message, $replacements, WATCHDOG_NOTICE, l(t('view'), $this->path()));
  }

  public function delete() {

    db_delete('storage_class')
      ->condition('class_id', $this->class_id)
      ->execute();

    db_delete('storage_class_container')
      ->condition('class_id', $this->class_id)
      ->execute();

    $message = 'Storage class <i>' . $this->name . '</i> has been deleted.';

    drupal_set_message($message);
    watchdog('storage', $message, NULL);
  }

  public function formElements() {

    $form['name'] = array(
      '#title' => t('Name'),
      '#type' => 'textfield',
      '#required' => TRUE,
      '#default_value' => $this->name,
    );
    
    $form['options'] = array(
      '#tree' => TRUE,
      'redirect' => array(
        '#title' => t('Serve by redirection'),
        '#type' => 'checkbox',
        '#default_value' => $this->options['redirect'],
      ),
      'serve_source_uri' => array(
        '#title' => t('Serve source URL'),
        '#type' => 'checkbox',
        '#default_value' => $this->options['serve_source_uri'],
        '#description' => t('If no instances are available, serve the source url.'),
      ),
    );

    // Are data URIs supported?
    $description = '';

    if (!storage_test_theme_image_data_uri()) {
      $description = t("Not supported by this installation of Drupal.") . '<br />';
    }
    $description .= t("See !link.", array('!link' => storage_data_uri_doc_link()));

    $form['options']['data_uri'] = array(
      '#title' => t("Serve as data URI"),
      '#type' => 'checkbox',
      '#default_value' => $this->options['data_uri'],
      '#description' => $description,
    );

    return $form;
  }

  public function selectorCount() {

    return db_select('storage_selector')
      ->condition('class_id', $this->class_id)
      ->countQuery()
      ->execute()
      ->fetchField();
  }
}

