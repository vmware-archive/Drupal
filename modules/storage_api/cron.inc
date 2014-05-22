<?php


function _storage_remove_containers() {

  $result = db_select('storage_class_container', NULL, array('fetch' => PDO::FETCH_ASSOC))
    ->fields('storage_class_container')
    ->condition('remove', 1)
    ->execute();

  foreach ($result as $container_instance) {
    $class = storage_class_load($container_instance['class_id']);
    $container = storage_container_load($container_instance['container_id']);

    try {
      $class->removeContainer($container);
    }
    catch (StorageException $e) {}
  }
}


function _storage_check_files() {

  $storage_ids = db_select('storage')
    ->fields('storage', array('storage_id'))
    ->condition('status', STORAGE_STATUS_PROCESS_CRON)
    ->orderBy('storage_id')
    ->execute()
    ->fetchCol();

  foreach ($storage_ids as $storage_id) {
    try {
      storage_load($storage_id)->check();
    }
    catch (StorageException $e) {}
  }
}


function _storage_check_migrations() {

  // Find selectors that are being migrated.
  $selector_ids = db_select('storage_selector', NULL, array('fetch' => PDO::FETCH_ASSOC))
    ->fields('storage_selector', array('selector_id'))
    ->distinct()
    ->condition('migrating', 1)
    ->orderBy('selector_id')
    ->execute()
    ->fetchCol();

  foreach ($selector_ids as $selector_id) {
    storage_selector($selector_id)->checkMigration();
  }
}


function _storage_container_maintenance() {

  $result = db_select('storage_container', NULL, array('fetch' => PDO::FETCH_ASSOC))
    ->condition('storage_container.status', STORAGE_CONTAINER_STATUS_ACTIVE)
    ->fields('storage_container')
    ->execute();

  foreach ($result as $container) {
    storage_container_new($container)->serviceMaintenance();
  }
}


function _storage_destroy_containers() {

  $result = db_select('storage_container', NULL, array('fetch' => PDO::FETCH_ASSOC))
    ->condition('storage_container.status', STORAGE_CONTAINER_STATUS_DESTROY)
    ->fields('storage_container')
    ->execute();

  foreach ($result as $container) {
    try {
      $container = storage_container_new($container);
      $container->serviceDestroy();
      $container->delete();
    }
    catch (StorageException $e) {}
  }
}


function _storage_cron() {
  _storage_remove_containers();
  _storage_check_files();
  _storage_check_migrations();
  _storage_container_maintenance();
  _storage_destroy_containers();
}

