<?php

class StorageRackspace extends StorageContainer implements StorageContainerInterface {
  private $conn;

  private function log_exception(Exception $e, $dsm = TRUE) {
    watchdog('storage_rackspace', 'php-cloudfiles exception: @msg', array('@msg' => $e->getMessage()), WATCHDOG_ERROR);
    
    if ($dsm && user_access('administer storage')) {
      drupal_set_message(t('php-cloudfiles exception: @msg', array('@msg' => $e->getMessage())), 'error');
    }
  }

  private function init() {
    if (module_exists('libraries') && function_exists('libraries_load')) {
      $library = libraries_load('php-cloudfiles');
    }

    if (!isset($library) || $library['loaded'] == FALSE) {
      watchdog('storage_rackspace', 'php-cloudfiles not found', array(), WATCHDOG_ERROR);
      
      if (user_access('administer storage')) {
        drupal_set_message(t('php-cloudfiles not found. Check the <a href="!path">status report</a>.', 
          array('!path' => url('admin/reports/status'))), 'error');
      }
      
      throw new StorageException();
    }

    // Use the UK host if necessary.
    $auth_host = ($this->settings['locality'] == 'us') ? US_AUTHURL : UK_AUTHURL;
    $auth = new CF_Authentication($this->settings['username'], $this->settings['api_key'], NULL, $auth_host);
    $auth->ssl_use_cabundle();

    try {
      $auth->authenticate();
    }
    catch (AuthenticationException $e) {
      $this->log_exception($e, FALSE);
      
      if (user_access('administer storage')) {
        drupal_set_message(t("Invalid locality, username or access key."), 'error');
      }

      throw new StorageException();
    }
    catch (Exception $e) {
      $this->log_exception($e);
      throw new StorageException();
    }

    $this->conn = new CF_Connection($auth);
    $this->conn->ssl_use_cabundle();
  }

  private function cf_container() {
    $cf_containers = &drupal_static(__FUNCTION__, array());
    
    if (isset($cf_containers[$this->container_id])) {
      return $cf_containers[$this->container_id];
    }

    $this->init();
    
    try {
      $cf_container = $this->conn->get_container($this->settings['cf_container']);
    }
    catch (Exception $e) {
      $this->log_exception($e);
      throw new StorageException();
    }

    $cf_containers[$this->container_id] = $cf_container;

    return $cf_container;
  }

  public function serviceSettingsDefault() {
    return array(
      'locality' => 'us',
      'username' => '',
      'cf_container' => '',
      'base_url' => '',
    );
  }

  private function localities() {
    return array(
      'us' => t("United States"),
      'uk' => t("United Kingdom"),
    );
  }

  public function serviceSettingsForm($already_exists) {
    $items['credentials_info'] = array(
      '#markup' => t('Get your credentials !link.', array('!link' => l(t('here'), 
        'http://www.rackspace.com/cloud/cloud_hosting_products/files/', 
        array('attributes' => array('target' => '_blank')))))
    );

    $items['locality'] = array(
      '#type' => 'select',
      '#title' => t("Locality"),
      '#description' => t("The locality of the Rackspace Cloud website the account was created on."),
      '#options' => $this->localities(),
      '#default_value' => $this->settings['locality'],
    );

    $items['username'] = array(
      '#type' => 'textfield',
      '#title' => t('Username'),
      '#required' => TRUE,
      '#validated' => TRUE,
      '#default_value' => $this->settings['username'],
    );

    $items['api_key'] = array(
      '#type' => 'password',
      '#title' => t('API key'),
      '#required' => TRUE,
      '#validated' => TRUE,
    );

    $items['cf_container'] = array(
      '#type' => 'textfield',
      '#title' => t('Cloud Files container'),
      '#required' => TRUE,
      '#validated' => TRUE,
      '#default_value' => $this->settings['cf_container'],
    );

    if (!$this->access_control) {
      $replacements = array(
        '!link' => l(t('CNAME'), 'http://en.wikipedia.org/wiki/CNAME_record', array(
          'attributes' => array('target' => '_blank')
        ))
      );
      $description = t("Points to the Rackspace domain name for the container (leave blank to obtain automatically), or a !link alias of it.", $replacements);

      $items['base_url'] = array(
        '#type' => 'textfield',
        '#title' => t("Base URL"),
        '#description' => $description,
        '#default_value' => $this->settings['base_url'],
      );
    }

    return $items;
  }

  public function serviceSettingsValidate($already_exists) {
    if (!$this->settings['username']) {
      form_set_error('settings][username', t('Username field is required.'));
    }

    if (!$this->settings['api_key']) {
      form_set_error('settings][api_key', t('API key field is required.'));
    }
    
    if (!$this->settings['cf_container']) {
      form_set_error('settings][cf_container', t('Cloud Files container name field is required.'));
    }
    
    if (form_get_errors()) {
      return;
    }
    
    try {
      $this->init();
    }
    catch (StorageException $e) {
      form_set_error('settings');
      return;
    }

    try {
      $this->conn->get_container($this->settings['cf_container']);
    }
    catch (NoSuchContainerException $e) {
      if ($already_exists) {
        form_set_error('settings][cf_container', t("Cloud Files container %name does not exist.",
          array('%name' => $this->settings['cf_container'])));
      }
      return;
    }
    catch (Exception $e) {
      $this->log_exception($e);
      form_set_error('settings');
      return;
    }

    if (!$already_exists) {
      form_set_error('settings][cf_container', t("Cloud Files container %name already exists.",
        array('%name' => $this->settings['cf_container'])));
    }
  }

  public function serviceCreate() {
    $this->init();
    
    try {
      $cf_container = $this->conn->create_container($this->settings['cf_container']);
    
      if ($this->access_control) {
        $cf_container->make_private();    // strangely, this has to be done explicity, or it might be public
      }
      else {
        $cf_container->make_public(60 * 60 * 24 * 3);    // 3 days is the maximum ttl that can be set in the api
      }
    }
    catch (Exception $e) {
      $this->log_exception($e);
      throw new StorageException();
    }
  }

  public function servicePostCreate() {
    // Get the base URL from Rackspace if necessary.
    if (!$this->access_control && !$this->settings['base_url']) {
      // Store base_url. This happens in this method so it will be stored for
      // containers that already exist at creation time.
      $this->settings['base_url'] = $this->cf_container()->cdn_uri;
      return TRUE;
    }
  }

  public function serviceUpdate() {
    // Get the base URL from Rackspace if necessary.
    if (!$this->access_control && !$this->settings['base_url']) {
      $this->settings['base_url'] = $this->cf_container()->cdn_uri;
    }
  }

  public function serviceInfo() {
    $localities = $this->localities();

    $info = array(
      t('Locality') => $localities[$this->settings['locality']],
      t('Username') => $this->settings['username'],
      t('Container') => $this->settings['cf_container'],
    );

    return $info;
  }
  
  public function serviceDestroy() {
    $this->init();

    try {
      // Check if the container actually exists.
      try {
        $this->conn->get_container($this->settings['cf_container']);
      }
      catch (NoSuchContainerException $e) {
        return;
      }

      // Delete the container.
      $this->conn->delete_container($this->settings['cf_container']);
    }
    catch (Exception $e) {
      $this->log_exception($e);
      throw new StorageException();
    }
  }

  public function serviceListReferences() {
    $cf_container = $this->cf_container();

    try {
      return $cf_container->list_objects();
    }
    catch(Exception $e) {
      $this->log_exception($e);
      throw new StorageException();
    }
  }

  public function serviceInstanceCreate(Storage $storage, $uri) {
    $reference = $this->uniqueFilename($storage->filename);

    $cf_container = $this->cf_container();

    try {
      $object = $cf_container->create_object($reference);
      $object->content_type = $storage->mimetype;
      $object->set_etag(base64_encode($storage->md5));
      $object->load_from_filename($uri, FALSE);

      $object->metadata = array(
        'storage_id' => $storage->storage_id,
        'file_id' => $storage->file_id,
        'filename' => $storage->filename,
      );

      $object->sync_metadata();
    }
    catch(Exception $e) {
      log_exception($e);

      try {
        $cf_container->delete_object($reference);
      }
      catch(Exception $e) {}

      throw new StorageException();
    }

    return $reference;
  }
  
  public function serviceInstanceExists($reference) {
    $cf_container = $this->cf_container();

    try {
      $object = $cf_container->get_object($reference);
    }
    catch(Exception $e) {
      return FALSE;
    }

    return TRUE;
  }

  public function serviceInstanceDestroy($reference) {
    $cf_container = $this->cf_container();

    try {
      $cf_container->delete_object($reference);
    }
    catch(Exception $e) {
      $this->log_exception($e);
      throw new StorageException();
    }
  }
  
  public function serviceInstanceGetUri($reference) {
    $cf_container = $this->cf_container();
    $temp_uri = new StorageTempURI();
    
    try {
      $object = $cf_container->get_object($reference);
      $object->save_to_filename($temp_uri);
    }
    catch(Exception $e) {
      $this->log_exception($e);
      throw new StorageException();
    }

    return $temp_uri;
  }
}
