<?php

function storage_admin_classes() {
  $header = array(t("Class"), t("Files"), t("Size"));
  $rows = array();

  $class_ids = db_select('storage_class')
    ->fields('storage_class')
    ->orderBy('storage_class.name')
    ->orderBy('storage_class.class_id')
    ->execute()
    ->fetchCol();

  foreach ($class_ids as $class_id) {
    $class = storage_class_load($class_id);

    $rows[] = array(
      $class->link(),
      $class->count(),
      $class->size(TRUE),
    );
  }

  if (empty($rows)) {
    $rows[] = array(array('data' => t("No classes configured."), 'colspan' => 3, 'class' => 'message'));
  }

  return theme('table', array(
    'header' => $header,
    'rows' => $rows,
    'attributes' => array('style' => 'width: auto;'),
  ));
}

function storage_admin_containers() {
  $header = array(t("Container"), t("Access control"), t("Status"), t("Files"), t("Size"));
  $rows = array();

  $result = db_select('storage_container', NULL, array('fetch' => PDO::FETCH_ASSOC))
    ->fields('storage_container')
    ->orderBy('name')
    ->orderBy('container_id')
    ->execute();

  foreach ($result as $container) {
    $container = storage_container_new($container);

    $rows[] = array(
      $container->link(),
      $container->access_control ? t("Yes") : t("No"),
      $container->status(),
      $container->count(),
      $container->size(TRUE),
    );
  }

  if (empty($rows)) {
    $rows[] = array(array('data' => t("No containers exist."), 'colspan' => 6, 'class' => 'message'));
  }

  return theme('table', array(
    'header' => $header,
    'rows' => $rows,
    'attributes' => array('style' => 'width: auto;'),
  ));
}

function storage_admin() {
  return storage_admin_classes() . storage_admin_containers();
}

