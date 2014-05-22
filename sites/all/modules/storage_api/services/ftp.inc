<?php


class StorageFTP extends StorageContainer implements StorageContainerInterface {

  private $ftpStream;

  private function login($change_directory = TRUE) {
  
    if (isset($this->ftpStream)) {
      return;
    }

    $this->ftpStream = ftp_connect($this->settings['host'], $this->settings['port'] ? $this->settings['port'] : NULL);
    
    if (!$this->ftpStream) {
      throw new StorageException();
    }
    
    if (!@ftp_login($this->ftpStream, $this->settings['username'], $this->settings['password'])) {
      ftp_close($this->ftpStream);
      throw new StorageException();
    }
    
    ftp_pasv($this->ftpStream, TRUE);
    
    if ($change_directory) {
      try {
        $this->changeDirectory($this->settings['directory']);
      }
      catch (StorageException $e) {
        ftp_close($this->ftpStream);
        throw new StorageException();
      }
    }
  }

  public function __destruct() {
  
    if ($this->ftpStream != NULL) {
      ftp_close($this->ftpStream);
    }
  }
  
  private function createDirectory($directory) {

    if (!ftp_mkdir($this->ftpStream, $directory)) {
      throw new StorageException();
    }
  }

  private function createDirectoryRecursive($path) {
    $original_dir = ftp_pwd($this->ftpStream);

    foreach (explode('/', $path) as $directory) {
      try {
        $this->changeDirectory($directory);
      }
      catch (StorageException $e) {
        $this->createDirectory($directory);
        $this->changeDirectory($directory);
      }
    }

    $this->changeDirectory($original_dir);
  }
  
  private function removeDirectory($directory) {
        
    if (!@ftp_rmdir($this->ftpStream, $directory)) {
      throw new StorageException();
    }
  }

  private function changeDirectory($directory) {
      
    if (!@ftp_chdir($this->ftpStream, $directory)) {
      throw new StorageException();
    }
  }
  
  private function setMode($filename, $mode) {
    
    if (!ftp_chmod($this->ftpStream, octdec(str_pad($mode, 4, '0', STR_PAD_LEFT)), $filename)) {
      throw new StorageException();
    }
  }

  public function serviceSettingsDefault() {
    return array(
      'host' => '',
      'port' => '21',
      'username' => '',
      'directory' => '',
      'mode' => '',
      'base_url' => '',
    );
  }

  public function serviceSettingsForm($already_exists) {

    $items['host'] = array(
      '#type' => 'textfield',
      '#title' => t('Host'),
      '#description' => t('The FTP server address. This parameter shouldn\'t have any trailing slashes and shouldn\'t be prefixed with ftp:// .'),
      '#required' => TRUE,
      '#validated' => TRUE,
      '#default_value' => $this->settings['host'],
    );

    $items['port'] = array(
      '#type' => 'textfield',
      '#title' => t('Port'),
      '#description' => t("The standard port is 21."),
      '#required' => TRUE,
      '#validated' => TRUE,
      '#maxlength' => 5,
      '#size' => 5,
      '#default_value' => $this->settings['port'],
    );
    
    $items['username'] = array(
      '#type' => 'textfield',
      '#title' => t('Username'),
      '#description' => t(''),
      '#required' => TRUE,
      '#validated' => TRUE,
      '#default_value' => $this->settings['username'],
    );

    $items['password'] = array(
      '#type' => 'password',
      '#title' => t('Password'),
    );

    $items['directory'] = array(
      '#type' => 'textfield',
      '#title' => t('Directory'),
      '#description' => t('The directory to be created on the FTP server.'),
      '#required' => TRUE,
      '#validated' => TRUE,
      '#default_value' => $this->settings['directory'],
    );
    
    $items['mode'] = array(
      '#type' => 'textfield',
      '#title' => t('File mode'),
      '#description' => t('Octal format, e.g. 644.<br />For serving, ensure the files are readable by the web server.<br />If omitted, the FTP server\'s default permissions will be used.'),
      '#maxlength' => 4,
      '#size' => 5,
      '#default_value' => $this->settings['mode'],
    );

    if (!$this->access_control) {
      $items['base_url'] = array(
        '#type' => 'textfield',
        '#title' => t('Base URL'),
        '#description' => t('The URL of the directory specified above.'),
        '#required' => TRUE,
        '#validated' => TRUE,
        '#default_value' => $this->settings['base_url'],
      );
    }

    return $items;
  }
  
  public function serviceSettingsValidate($already_exists) {

    if (!$this->settings['host']) {
      form_set_error('settings][host', t('Host field is required.'));
    }

    if (!$this->settings['port']) {
      form_set_error('settings][port', t('Port field is required.'));
    }

    if (!$this->settings['username']) {
      form_set_error('settings][username', t('Username field is required.'));
    }

    if (!$this->settings['directory']) {
      form_set_error('settings][directory', t('Directory field is required.'));
    }

    if (!$this->access_control && !$this->settings['base_url']) {
      form_set_error('settings][base_url', t('Base URL field is required.'));
    }

    if (form_get_errors()) {
      return;
    }

    try {
      $this->login(FALSE);
    }
    catch (StorageException $e) {
      form_set_error('settings', t('Failed to authenticate.'));
      return;
    }

    if (!$already_exists) {
      try {
        $this->changeDirectory($this->settings['directory']);
      }
      catch (StorageException $e) {
        return;
      }
      form_set_error('settings][directory', t("Directory already exists."));
    }
    else {
      try {
        $this->changeDirectory($this->settings['directory']);
      }
      catch (StorageException $e) {
        form_set_error('settings][directory', t("Directory does not exist."));
      }
    }
  }

  public function serviceCreate() {
    $this->login(FALSE);
    $this->createDirectoryRecursive($this->settings['directory']);
  }

  public function servicePostCreate() {
    $this->changeDirectory($this->settings['directory']);
  }

  public function serviceInfo() {
    $info = array(
      t('Host') => $this->settings['host'],
      t('Port') => $this->settings['port'] ? $this->settings['port'] : 21,
      t('Username') => $this->settings['username'],
      t('Directory') => $this->settings['directory'],
    );
    
    if ($this->settings['mode']) {
      $info[t('File mode')] = $this->settings['mode'];
    }

    if ($this->settings['base_url'] !== '') {
      $info[t('Base URL')] = url($this->settings['base_url'], array('absolute' => TRUE));
    }

    return $info;
  }
  
  public function serviceDestroy() {
    $this->login(FALSE);
  
    try {
      $this->removeDirectory($this->settings['directory']);
    }
    catch (StorageException $e) {}
  }
  
  private function enterFileDirectory($filename) {
    $components = explode('/', $filename);
    $basename = array_pop($components);
    $path = '';
    
    foreach ($components as &$directory) {
      $exists = FALSE;
      $count = 0;
      
      do {
        $potential = (!$count) ? $directory : ($directory . '_' . $count++);
        
        try {
          try {
            $this->changeDirectory($potential);
          }
          catch (StorageException $e) {
            $this->createDirectory($potential);
            $this->changeDirectory($potential);
          }
        }
        catch (StorageException $e) {
          $count++;
          continue;
        }
        
        break;
      }
      while (TRUE);

      $directory = $potential;      
    }
    
    $components[] = $basename;
    
    return implode('/', $components);
  }
 
  public function serviceInstanceCreate(Storage $storage, $uri) {
    $this->login();
    $original_dir = ftp_pwd($this->ftpStream);
        
    $reference = $this->uniqueFilename($this->enterFileDirectory($storage->filename));
    
    $components = explode('/', $reference);

    $result = ftp_put($this->ftpStream, array_pop($components), $uri, FTP_BINARY);
    $this->changeDirectory($original_dir);
    
    if (!$result) {
      throw new StorageException();
    }

    if ($this->settings['mode']) {
      $this->setMode($reference, $this->settings['mode']);
    }

    return $reference;
  }
  
  private function listItems($dir) {
    $files = array();
    
    $items = ftp_rawlist($this->ftpStream, $dir);
    
    foreach ($items as $item) {
    
      $mode = strtok($item, ' ');
      
      $i = 7;
      while ($i--) {
        strtok(' ');
      }
      
      $filename = strtok('');
      
      switch ($mode[0]) {
      
        case 'd':
          foreach ($this->listItems($dir . '/' . $filename) as $subfilename) {
            $files[] = $filename . '/' . $subfilename;
          }
          break;

        case '-':
          $files[] = $filename;
          break;
      }
    }

    return $files;
  }
  
  public function serviceListReferences() {
    $this->login();
    
    return $this->listItems('.');
  }

  public function serviceInstanceExists($reference) {
    $this->login();
    
    return ftp_size($this->ftpStream, $reference) != -1;
  }
  
  public function serviceInstanceDestroy($reference) {
    $this->login();
          
    ftp_delete($this->ftpStream, $reference);

    $dirs = explode('/', $reference);
    array_pop($dirs);
    
    while (count($dirs)) {
    
      try {
        $this->removeDirectory(implode('/', $dirs));
      }
      catch (StorageException $e) {
        break;
      }

      array_pop($dirs);
    }
  }
  
  public function serviceInstanceGetUri($reference) {
    $this->login();

    $temp_uri = new StorageTempURI();
    $result = ftp_get($this->ftpStream, $temp_uri, $reference, FTP_BINARY);
    
    if (!$result) {
      throw new StorageException();
    }

    return $temp_uri;
  }
}

