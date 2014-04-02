<?php


/**
 * FAPI definition for the create container form.
 *
 * @ingroup forms
 * @see storage_create_container_form_validate()
 * @see storage_create_container_form_submit()
 */
function storage_create_container_form(array $form, array &$form_state) {

  drupal_set_title(t('Create container'));

  if (!isset($form_state['values']['step'])) {
    $step = 1;
    // Set defaults.
    $form_state['storage'] = array(
      'service_id' => 'storage:fs',
      'name' => '',
      'access_control' => FALSE,
      'already_exists' => FALSE,
    );
  }
  else {
    if ($form_state['clicked_button']['#value'] == t('Back')) {
      $step = $form_state['values']['step'] - 1;
    }
    else {
      $step = $form_state['values']['step'];
      
      if (!form_get_errors()) {
        $step++;
      }
    }
  }

  $form['step'] = array(
    '#type' => 'value',
    '#value' => $step
  );

  switch ($step) {

    case 1:
      $services = _storage_get_service_names();

      $form['service_id'] = array(
        '#type' => 'select',
        '#title' => t('Service'),
        '#options' => $services,
        '#default_value' => $form_state['storage']['service_id'],
      );
      
      $form['name'] = array(
        '#title' => t('Name'),
        '#type' => 'textfield',
        '#default_value' => $form_state['storage']['name'],
        '#description' => t("Optional"),
      );

      $form['access_control'] = array(
        '#type' => 'checkbox',
        '#title' => t('Access control'),
        '#default_value' => $form_state['storage']['access_control'],
        '#description' => t("Prevents files being served to users who are not authorised.<br />Depending on the service, this can have significant performance implications.<br />Even without this enabled, URLs will only be generated for files that the user has permission to access."),
      );

      $form['already_exists'] = array(
        '#type' => 'checkbox',
        '#title' => t('Already exists'),
        '#default_value' => $form_state['storage']['already_exists'],
      );

      $form['next'] = array(
        '#type' => 'submit',
        '#value' => t('Next')
      );

      break;
      
    case 2:
      $container = storage_container_new($form_state['storage'], FALSE);
      $service_name = check_plain($container->service()->name);

      $info = array(
        t("Service") => $service_name,
        t("Name") => check_plain($container->name),
        t("Access control") => $container->access_control ? t("Yes") : t("No"),
        t("Already exists") => $container->already_exists ? t("Yes") : t("No"),
      );

      $form['info'] = array(
        '#markup' => theme('storage_info_table', array('info' => $info))
      );

      $form['settings'] = array(
        '#type' => 'fieldset',
        '#title' => $service_name . t(' settings'),
        '#tree' => TRUE
      );
      
      $form['settings'] += $container->serviceSettingsForm($form_state['storage']['already_exists']);
      
      $form['back'] = array(
        '#type' => 'submit',
        '#value' => t('Back')
      );
      
      $form['create'] = array(
        '#type' => 'submit',
        '#value' => t('Create')
      );

      break;
  }
  
  return $form;
}


/**
 * Validate handler for the create container form.
 *
 * @see storage_create_container_form()
 * @see storage_create_container_form_submit()
 */
function storage_create_container_form_validate(array $form, array &$form_state) {

  switch ($form_state['values']['step']) {
  
    case 1:
      $form_state['storage']['name'] = $form_state['values']['name'];
      $form_state['storage']['service_id'] = $form_state['values']['service_id'];
      $form_state['storage']['access_control'] = $form_state['values']['access_control'];
      $form_state['storage']['already_exists'] = $form_state['values']['already_exists'];
      $form_state['storage']['settings'] = array();

      $service = storage_service_load($form_state['storage']['service_id']);

      try {
        $service->check();
      }
      catch (StorageException $e) {
        $message = check_plain($service->name) . ' ' . t('service is not set up properly.') . '<br />';
        $message .= $e->getMessage();
        form_set_error('service_id', $message);
      }

      break;
      
    case 2:
      $form_state['storage']['settings'] = $form_state['values']['settings'];

      if ($form_state['clicked_button']['#value'] == t('Back')) {
        break;
      }

      $container = storage_container_new($form_state['storage'], FALSE);
      $container->serviceSettingsValidate($form_state['storage']['already_exists']);

      if (!form_get_errors()) {
        try {
          $container->create($form_state['storage']['already_exists']);
          $form_state['storage'] = $container;
        }
        catch (StorageException $e) {
          form_set_error('settings', $e->getMessage());
        }
      }
      
      break;
  }
}


/**
 * Submit handler for the create container form.
 *
 * @see storage_create_container_form()
 * @see storage_create_container_form_validate()
 */
function storage_create_container_form_submit(array $form, array &$form_state) {

  if ($form_state['clicked_button']['#value'] != t('Create')) {
    $form_state['rebuild'] = TRUE;
    return;
  }
  
  $container = $form_state['storage'];
  
  $form_state['rebuild'] = FALSE;
  $form_state['redirect'] = $container->path();
  unset($form_state['storage']);
}


function _storage_container_render_classes(array $classes) {
  $links = array();

  foreach ($classes as $class) {
    $links[] = $class->link();
  }

  return count($links) ? implode(', ', $links) : t('None');
}


/**
 * FAPI definition for the container form.
 *
 * @ingroup forms
 * @see storage_container_form_suspend()
 * @see storage_container_form_unsuspend()
 */
function storage_container_form(array $form, array &$form_state, StorageContainerInterface $container) {
  $form['#container'] = $container;

  $info = array(
    t('Service') => check_plain($container->service()->name),
    t('Name') => check_plain($container->name),
    t('Access control') => $container->access_control ? t('Yes') : t('No'),
    t('External') => $container->external ? t('Yes') : t('No'),
    t('Status') => $container->status(),
    t('Classes') => _storage_container_render_classes($container->classes()),
    t('Files') => $container->count(),
    t('Size') => $container->size(TRUE),
  );

  $info += $container->serviceInfo();

  if (!$container->access_control && isset($container->settings['base_url'])) {
    $info[t("Base URL")] = url($container->settings['base_url'], array('absolute' => TRUE));
  }

  $form['status'] = array(
    '#markup' => theme('storage_info_table', array('info' => $info))
  );
  
  switch ($container->status) {

    case STORAGE_CONTAINER_STATUS_ACTIVE:
      $form['suspend'] = array(
        '#type' => 'submit',
        '#value' => t('Suspend'),
        '#submit' => array('storage_container_form_suspend'),
        '#weight' => 2,
      );
      break;

    case STORAGE_CONTAINER_STATUS_SUSPENDED:
      $form['unsuspend'] = array(
        '#type' => 'submit',
        '#value' => t('Unsuspend'),
        '#submit' => array('storage_container_form_unsuspend'),
        '#weight' => 2,
      );
      break;
  }

  return $form;
}


/**
 * Submit handler (suspend) for the container form.
 *
 * @see storage_container_form()
 * @see storage_container_form_unsuspend()
 */
function storage_container_form_suspend(array $form, array &$form_state) {
  $container = $form['#container'];

  $form_state['redirect'] = $container->path() . '/suspend';
}


/**
 * Submit handler (unsuspend) for the container form.
 *
 * @see storage_container_form()
 * @see storage_container_form_suspend()
 */
function storage_container_form_unsuspend(array $form, array &$form_state) {
  $container = $form['#container'];

  $form_state['redirect'] = $container->path() . '/unsuspend';
}


/**
 * FAPI definition for the container suspend form.
 *
 * @ingroup forms
 * @see storage_container_suspend_form_submit()
 */
function storage_container_suspend_form(array $form, array &$form_state, StorageContainerInterface $container) {
  $form['#container'] = $container;

  return confirm_form($form, 'Are you sure you want to suspend storage container <i>' . check_plain($container->name()) . '</i> ?', 
    $container->path(), t(''));
}


/**
 * Submit handler for the container suspend form.
 *
 * @see storage_container_suspend_form()
 */
function storage_container_suspend_form_submit(array $form, array &$form_state) {
  $container = $form['#container'];
  
  $container->status = STORAGE_CONTAINER_STATUS_SUSPENDED;
  $container->update();
  $container->flushServingContainer();

  $message = 'Storage container <i>' . check_plain($container->name()) . '</i> has been suspended.';

  drupal_set_message($message);
  watchdog('storage', $message, NULL, WATCHDOG_NOTICE, l(t('view'), $container->path()));

  $form_state['redirect'] = $container->path();
}


/**
 * FAPI definition for the container unsuspend form.
 *
 * @ingroup forms
 * @see storage_container_unsuspend_form_submit()
 */
function storage_container_unsuspend_form(array $form, array &$form_state, StorageContainerInterface $container) {
  $form['#container'] = $container;

  return confirm_form($form, 'Are you sure you want to unsuspend storage container <i>' . 
    check_plain($container->name()) . '</i> ?', $container->path(), t(''));
}


/**
 * Submit handler for the container unsuspend form.
 *
 * @see storage_container_unsuspend_form()
 */
function storage_container_unsuspend_form_submit(array $form, array &$form_state) {
  $container = $form['#container'];

  $container->status = STORAGE_CONTAINER_STATUS_ACTIVE;
  $container->update();
  $container->flushServingContainer();

  $message = 'Storage container <i>' . check_plain($container->name()) . '</i> has been unsuspended.';

  drupal_set_message($message);
  watchdog('storage', $message, NULL, WATCHDOG_NOTICE, l(t('view'), $container->path()));
  
  $form_state['redirect'] = $container->path();
}


/**
 * FAPI definition for the container destroy form.
 *
 * @ingroup forms
 * @see storage_container_destroy_form_submit()
 */
function storage_container_destroy_form(array $form, array &$form_state, StorageContainerInterface $container) {
  $form['#container'] = $container;

  $classes = $container->classes();
  $count = count($classes);
  
  if ($count > 0) {
    $output = t('This container is present in ');
    $output .= format_plural($count, 'class', 'classes') . ' ';
    $output .= _storage_container_render_classes($classes);
    $output .= t(' - destroying it now could cause data loss.');

    $form[] = array(
      '#markup' => $output,
    );
  }

  $form[] = array(
    '#markup' => t("<p>Are you sure you want to destroy this storage container?</p>")
  );
  
  $form['just_delete'] = array(
    '#type' => 'checkbox',
    '#title' => t('Just delete database records.'),
    '#description' => t('Checking this option means that there will be no communication with the service and the container itself will remain untouched.'),
    '#default_value' => $container->external,
    '#disabled' => $container->external,
  );

  $form['confirm'] = array(
    '#type' => 'submit',
    '#value' => t('Confirm'),
  );

  return $form;
}


/**
 * Submit handler for the container destroy form.
 *
 * @see storage_container_destroy_form()
 */
function storage_container_destroy_form_submit(array $form, array &$form_state) {
  $container = $form['#container'];

  if ($container->external || $form_state['values']['just_delete']) {
    $container->delete();
  }
  else {
    $container->destroy();
  }

  $form_state['redirect'] = 'admin/structure/storage';
}


/**
 * FAPI definition for the container edit form.
 *
 * @ingroup forms
 * @see storage_container_edit_form_submit()
 * @see storage_container_edit_form_validate()
 */
function storage_container_edit_form(array $form, array &$form_state, StorageContainerInterface $container) {
  $form['#container'] = $container;

  $form['name'] = array(
    '#title' => 'Name',
    '#type' => 'textfield',
    '#default_value' => $container->name,
    '#description' => t("Optional"),
  );

  $form['settings'] = array(
    '#type' => 'fieldset',
    '#title' => check_plain($container->service()->name) . t(' settings'),
    '#tree' => TRUE,
  );
  
  $form['settings'] += $container->serviceSettingsForm(TRUE);

  $form['external'] = array(
    '#title' => t('External'),
    '#type' => 'fieldset',
    '#collapsible' => !$container->external,
    '#collapsed' => TRUE,
    '#description' => t("It is important that two sites don't try to control the same container, otherwise data loss can occur. Any duplicate site must have it's remote containers marked as external."),
  );

  $form['external']['external'] = array(
    '#title' => t('This container is controlled by another site'),
    '#type' => 'checkbox',
    '#default_value' => $container->external,
    '#description' => t("<ul><li>files will not be propagated to this container</li><li>files will not be deleted from this container, but their database records will</li><li>files can still be served and retrieved from this container</li></ul>"),
  );

  $form['submit'] = array(
    '#type' => 'submit',
    '#value' => 'Save',
  );
  
  return $form;
}


/**
 * Validate handler for the container edit form.
 *
 * @see storage_container_edit_form()
 * @see storage_container_edit_form_submit()
 */
function storage_container_edit_form_validate(array $form, array &$form_state) {
  $container = $form['#container'];
  
  $container->settings = $form_state['values']['settings'] + $container->settings;
  $container->serviceSettingsValidate(TRUE);
}


/**
 * Submit handler for the container edit form.
 *
 * @see storage_container_edit_form()
 * @see storage_container_edit_form_validate()
 */
function storage_container_edit_form_submit(array $form, array &$form_state) {
  $container = $form['#container'];

  $container->name = $form_state['values']['name'];
  $container->settings = $form_state['values']['settings'] + $container->settings;
  $container->external = $form_state['values']['external'];

  $container->serviceUpdate();
  $container->update();
  $container->flushServingContainer();

  $message = 'Storage container <i>' . check_plain($container->name()) . '</i> has been updated.';

  drupal_set_message($message);
  watchdog('storage', $message, NULL, WATCHDOG_NOTICE, l(t('view'), $container->path()));

  $form_state['redirect'] = $container->path();
}

