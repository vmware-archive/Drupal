<?php


class StorageService {
  public $service_id;
  public $name;
  public $class;
  public $htaccess;
  public $copy;
  public $serve;
  public $serve_secure;

  public function __construct($service_id) {
    $this->service_id = $service_id;

    $services = storage_service_info();
    foreach($services[$this->service_id] as $key => $value) {
      $this->$key = $value;
    }
  }

  public function check() {
    list($module, $service) = explode(':', $this->service_id);
    module_invoke($module, 'storage_service_check_' . $service);
  }

  public function flushServingContainer() {

    $selector_subquery = db_select('storage_container');
    $selector_subquery->join('storage_class_container', NULL, 'storage_container.container_id = storage_class_container.container_id');
    $selector_subquery->join('storage_selector', NULL, 'storage_class_container.class_id = storage_selector.class_id');
    
    $selector_subquery->fields('storage_selector', array('selector_id'))
      ->condition('storage_container.service_id', $this->service_id);
      
    db_update('storage')
      ->expression('serving_container', 'NULL')
      ->condition('selector_id', $selector_subquery, 'IN')
      ->execute();

    cache_clear_all();
  }
}
