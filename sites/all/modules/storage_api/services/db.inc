<?php

class StorageDB extends StorageContainer implements StorageContainerInterface {

  private function setActiveDB() {

    db_set_active($this->settings['db']);
    return new StorageDBActive();
  }

  public function serviceSettingsDefault() {
    return array(
      'db' => 'default',
      'table' => 'storage_contents',
    );
  }

  public function serviceSettingsForm($already_exists) {
    global $databases;

    foreach ($databases as $key => $database) {
      $options[$key] = $key;
    }

    $items['db'] = array(
      '#type' => 'select',
      '#title' => t('Database'),
      '#options' => $options,
      '#required' => TRUE,
      '#validated' => TRUE,
      '#default_value' => $this->settings['db'],
    );

    $items['table'] = array(
      '#type' => 'textfield',
      '#title' => t('Table'),
      '#description' => t('Without prefix or {}.'),
      '#required' => TRUE,
      '#validated' => TRUE,
      '#default_value' => $this->settings['table'],
    );

    return $items;
  }

  public function serviceSettingsValidate($already_exists) {
    $active = $this->setActiveDB();

    if (!$this->settings['table']) {
      form_set_error('settings][table', t('Database table field is required.'));
      return;
    }
    
    if (!$already_exists && db_table_exists($this->settings['table'])) {
      form_set_error('settings][table', t("Database table already exists."));
    }

    if ($already_exists && !db_table_exists($this->settings['table'])) {
      form_set_error('settings][table', t("Database table does not exist."));
    }
  }

  public function serviceCreate() {
    $active = $this->setActiveDB();
    
    $spec = array(
      'fields' => array(
        'file_id' => array('type' => 'int', 'unsigned' => TRUE, 'not null' => TRUE),
        'contents' => array('type' => 'blob', 'size' => 'big', 'not null' => TRUE),
      ),
      'primary key' => array('file_id'),
    );

    db_create_table($this->settings['table'], $spec);
  }

  public function serviceInfo() {

    $info = array(
      t('Database') => $this->settings['db'],
      t('Table') => $this->settings['table'],
    );

    return $info;
  }

  public function serviceDestroy() {
    $active = $this->setActiveDB();

    db_drop_table($this->settings['table']);
  }

  public function serviceInstanceCreate(Storage $storage, $uri) {
    $active = $this->setActiveDB();
    
    db_insert($this->settings['table'])
      ->fields(array(
        'file_id' => $storage->file_id,
        'contents' => file_get_contents($uri),
        ))
      ->execute();

    return $storage->file_id;
  }
  
  public function serviceListReferences() {
    $active = $this->setActiveDB();
  
    return db_select($this->settings['table'])
      ->fields($this->settings['table'], array('file_id'))
      ->execute()
      ->fetchCol();
  }

  public function serviceInstanceExists($reference) {
    $active = $this->setActiveDB();

    return db_select($this->settings['table'])
      ->condition('file_id', $reference)
      ->countQuery()
      ->execute()
      ->fetchField();
  }

  public function serviceInstanceGetContents($reference) {
    $active = $this->setActiveDB();

    $contents = db_select($this->settings['table'])
      ->fields($this->settings['table'], array('contents'))
      ->condition('file_id', $reference)
      ->execute()
      ->fetchField();

    if ($contents === FALSE) {
      throw new StorageException();
    }

    return $contents;
  }

  public function serviceInstanceDestroy($reference) {
    $active = $this->setActiveDB();

    db_delete($this->settings['table'])
      ->condition('file_id', $reference)
      ->execute();
  }

  public function serviceInstanceOutput($reference) {

    echo $this->serviceInstanceGetContents($reference);
  }
}


class StorageDBActive {

  public function __destruct() {
    db_set_active();
  }
}

