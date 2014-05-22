<?php

class StorageFS extends StorageContainer implements StorageContainerInterface {

  public function serviceSettingsDefault() {
    return array(
      'path' => '',
      'base_url' => '',
    );
  }

  public function serviceSettingsForm($already_exists) {
    if (!$this->settings['path']) {
      $this->settings['path'] = storage_new_local_storage_path();
    }

    $items['path'] = array(
      '#type' => 'textfield',
      '#title' => t('Directory path'),
      '#description' => t("Can be absolute or relative."),
      '#default_value' => $this->settings['path'],
      '#required' => TRUE,
      '#validated' => TRUE,
    );

    if ($already_exists) {
      $items['path']['#description'] .= '<br />' . 
        t('If this setting is changed, the directory must be moved manually to the new path.');
    }

    if (!$this->access_control) {
      $description  = "The URL of the directory path specified above (typically exactly the same).<br/>";
      $description .= "Can be absolute or relative.";

      if (!$this->settings['base_url']) {
        $this->settings['base_url'] = $this->settings['path'];
      }

      $items['base_url'] = array(
        '#type' => 'textfield',
        '#title' => t('Base URL'),
        '#description' => t($description),
        '#required' => TRUE,
        '#validated' => TRUE,
        '#default_value' => $this->settings['base_url'],
      );
    }

    return $items;
  }
  
  public function serviceSettingsValidate($already_exists) {

    if (!$this->settings['path']) {
      form_set_error('settings][path', t('Directory path field is required.'));
      return;
    }
    
    if (!$already_exists && file_prepare_directory($this->settings['path'], 0)) {
      form_set_error('settings][path', t("Directory already exists."));
    }

    if ($already_exists && !file_prepare_directory($this->settings['path'], 0)) {
      form_set_error('settings][path', t("Directory does not exist."));
    }

    if (!$this->access_control && !$this->settings['base_url']) {
      form_set_error('settings][base_url', t('Base URL field is required.'));
    }
  }

  public function serviceCreate() {
    if (!file_prepare_directory($this->settings['path'], FILE_CREATE_DIRECTORY | FILE_MODIFY_PERMISSIONS)) {
      form_set_error('settings][path', t('The directory %directory could not be created. Please check permissions.',
        array('%directory' => $this->settings['path'])));

      throw new StorageException();
    }
  }
  
  public function serviceInfo() {
    $info['Free space'] = storage_format_byte_count(@disk_free_space($this->settings['path']));
    $info['Directory'] = realpath($this->settings['path']);
    return $info;
  }
  
  public function serviceDestroy() {
    return @rmdir($this->settings['path']);
  }
  
  private function instancePath($reference) {
    return $this->settings['path'] . '/' . $reference;
  }
  
  private function ensureDirectoryPathExists($reference) {
    $dirs = explode('/', $reference);
    $basename = array_pop($dirs);
    $path = '';
    
    foreach ($dirs as $dir) {
      $count = 0;
      $test_path = $path . $dir;

      while (!is_dir($this->instancePath($test_path)) &&
        !@mkdir($this->instancePath($test_path)))
      {
        $count++;
        $test_path = $path . $dir . '_' . $count;
      }
      
      $path = $test_path . '/';
    }
    
    return $path . $basename;
  }
  
  public function serviceInstanceCreate(Storage $storage, $uri) {

    $reference = $this->uniqueFilename($this->ensureDirectoryPathExists($storage->filename));
    $path = $this->instancePath($reference);

    $success = copy($uri, $path);

    if ($success) {
      return $reference;
    }
    else {
      throw new StorageException();
    }
  }
  
  private function scanDirectory($dir) {
    $files = array();
    $handle = opendir($dir);
    
    if (!$handle) {
      throw new StorageException();
    }
    
    while (($filename = readdir($handle)) !== FALSE) {

      if ($filename == '.' || $filename == '..') {
        continue;
      }
    
      $filepath = $dir . '/' . $filename;
      
      if (is_dir($filepath)) {
      
        foreach ($this->scanDirectory($filepath) as $subfilename) {
          $files[] = $filename . '/' . $subfilename;
        }
      }
      else {
        $files[] = $filename;
      }
    }

    closedir($handle);
    
    return $files;
  }
  
  public function serviceListReferences() {
  
    return $this->scanDirectory($this->settings['path']);
  }

  public function serviceInstanceExists($reference) {
    
    return @stat($this->instancePath($reference)) !== FALSE;
  }

  public function serviceInstanceGetUri($reference) {

    return $this->instancePath($reference);
  }
  
  public function serviceInstanceDestroy($reference) {
      
    @drupal_unlink($this->instancePath($reference));
    
    $dirs = explode('/', $reference);
    array_pop($dirs);
    
    while (count($dirs)) {
    
      if (!@rmdir($this->instancePath(implode('/', $dirs)))) {
        break;
      }

      array_pop($dirs);
    }
  }
}

