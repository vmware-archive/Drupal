<?php

/**
 * @file
 * Administration form setup for S3 File System.
 */

/**
 * Builds the Settings form.
 */
function s3fs_settings() {
  $config = _s3fs_get_config();
  $form = array();
  // I'd like to be able to pull this information directly from the SDK, but
  // I couldn't find a good way to get the human-readable region names.
  $region_map = array(
    '' => 'Default',
    'us-east-1' => 'US Standard (us-east-1)',
    'us-west-1' => 'US West - Northern California  (us-west-1)',
    'us-west-2' => 'US West - Oregon (us-west-2)',
    'eu-west-1' => 'EU - Ireland  (eu-west-1)',
    'ap-southeast-1' => 'Asia Pacific - Singapore (ap-southeast-1)',
    'ap-southeast-2' => 'Asia Pacific - Sydney (ap-southeast-2)',
    'ap-northeast-1' => 'Asia Pacific - Tokyo (ap-northeast-1)',
    'sa-east-1' => 'South America - Sao Paulo (sa-east-1)',
  );
  
  $form['s3fs_credentials'] = array(
    '#type' => 'fieldset',
    '#title' => t('Amazon Web Services Credentials'),
    '#description' => t("To configure your Amazon Web Services credentials, enter the values in the appropriate fields below.
      You may instead set \$conf['awssdk2_access_key'] and \$conf['awssdk2_secret_key'] in your site's settings.php file.
      Values set in settings.php will override the values in these fields."),
    '#collapsible' => TRUE,
    '#collapsed' => FALSE,
  );
  
  $form['s3fs_credentials']['s3fs_awssdk2_access_key'] = array(
    '#type' => 'textfield',
    '#title' => t('Amazon Web Services Access Key'),
    '#default_value' => ($access_key = !empty($config['awssdk2_access_key']) ? $config['awssdk2_access_key'] : ''),
  );
  // Override with awssdk2_access_key from settings.php, if set.
  $form['s3fs_credentials']['s3fs_awssdk2_access_key']['#default_value'] = variable_get('awssdk2_access_key', $access_key);
  
  $form['s3fs_credentials']['s3fs_awssdk2_secret_key'] = array(
    '#type' => 'textfield',
    '#title' => t('Amazon Web Services Secret Key'),
    '#default_value' => ($secret_key = !empty($config['awssdk2_secret_key']) ? $config['awssdk2_secret_key'] : ''),
  );
  // Override with awssdk2_secret_key from settings.php, if set.
  $form['s3fs_credentials']['s3fs_awssdk2_secret_key']['#default_value'] = variable_get('awssdk2_secret_key', $secret_key);
  
  $form['s3fs_credentials']['s3fs_use_instance_profile'] = array(
    '#type' => 'checkbox',
    '#title' => t('Use EC2 Instance Profile Credentials'),
    '#default_value' => !empty($config['use_instance_profile']) ? $config['use_instance_profile'] : FALSE,
    '#description' => t('If your Drupal site is running on an Amazon EC2 server, you may use the Instance Profile Credentials from that server
      rather than setting your AWS credentials directly.'),
  );
  $form['s3fs_credentials']['s3fs_awssdk2_default_cache_config'] = array(
    '#type' => 'textfield',
    '#title' => t('Default Cache Location'),
    '#description' => t('The default cache location for your EC2 Instance Profile Credentials.'),
    '#default_value' => ($cache_config = !empty($config['awssdk2_default_cache_config']) ? $config['awssdk2_default_cache_config'] : ''),
    '#states' => array(
      'visible' => array(
        ':input[id=edit-s3fs-use-instance-profile]' => array('checked' => TRUE),
      ),
    ),
  );
  // Override with awssdk2_default_cache_config from settings.php, if set.
  $form['s3fs_credentials']['s3fs_awssdk2_default_cache_config']['#default_value'] = variable_get('awssdk2_default_cache_config', $cache_config);
  
  $form['s3fs_bucket'] = array(
    '#type'           => 'textfield',
    '#title'          => t('S3 Bucket Name'),
    '#default_value'  => !empty($config['bucket']) ? $config['bucket'] : '',
    '#required'       => TRUE,
  );
  $form['s3fs_region'] = array(
    '#type'          => 'select',
    '#options'       => $region_map,
    '#title'         => t('S3 Region'),
    '#description'   => t('The region in which your bucket resides. Be careful to specify this accurately, as you may see strange behavior if the region is set wrong.'),
    '#default_value' => !empty($config['region']) ? $config['region'] : '',
  );
  $form['s3fs_use_cname'] = array(
    '#type'          => 'checkbox',
    '#title'         => t('Enable CNAME'),
    '#description'   => t('Serve files from a custom domain by using an appropriately named bucket, e.g. "mybucket.mydomain.com".'),
    '#default_value' => !empty($config['use_cname']) ? $config['use_cname'] : FALSE,
  );
  $form['s3fs_cname_settings_fieldset'] = array(
    '#type' => 'fieldset',
    '#title' => t('CNAME Settings'),
    '#states' => array(
      'visible' => array(
        ':input[id=edit-s3fs-use-cname]' => array('checked' => TRUE),
      ),
    ),
  );
  $form['s3fs_cname_settings_fieldset']['s3fs_domain'] = array(
    '#type'          => 'textfield',
    '#title'         => t('CDN Domain Name'),
    '#description'   => t('If serving files from CloudFront, the bucket name can differ from the domain name.'),
    '#default_value' => !empty($config['domain']) ? $config['domain'] : '',
  );
  $form['s3fs_use_https'] = array(
    '#type'          => 'checkbox',
    '#title'         => t('Always serve files from S3 via HTTPS'),
    '#description'   => t('Forces S3 File System to always generate HTTPS URLs for files in your bucket, e.g. "https://mybucket.s3.amazonaws.com/smiley.jpg".<br>
      Without this setting enabled, URLs for your files will use the same scheme as the page they are served from.'),
    '#default_value' => !empty($config['use_https']) ? $config['use_https'] : FALSE,
  );
  $form['s3fs_use_customhost'] = array(
    '#type'          => 'checkbox',
    '#title'         => t('Use a Custom Host'),
    '#description'   => t('Connect to an S3-compatible storage service other than Amazon.'),
    '#default_value' => !empty($config['use_customhost']) ? $config['use_customhost'] : FALSE,
  );
  $form['s3fs_costomhost_settings_fieldset'] = array(
    '#type' => 'fieldset',
    '#title' => t('Custom Host Settings'),
    '#states' => array(
      'visible' => array(
        ':input[id=edit-s3fs-use-customhost]' => array('checked' => TRUE),
      ),
    ),
  );
  $form['s3fs_costomhost_settings_fieldset']['s3fs_hostname'] = array(
    '#type'          => 'textfield',
    '#title'         => t('Hostname'),
    '#description'   => t('Custom service hostname, e.g. "objects.dreamhost.com".'),
    '#default_value' => !empty($config['hostname']) ? $config['hostname'] : '',
    '#states'        => array(
      'visible' => array(
        ':input[id=edit-s3fs-use-customhost]' => array('checked' => TRUE),
      ),
    ),
  );
  $form['s3fs_ignore_cache'] = array(
    '#type'          => 'checkbox',
    '#title'         => t('Ignore the file metadata cache'),
    '#description'   => t("If you need to debug a problem with S3, you may want to temporarily ignore the file metadata cache.
       This will make all filesystem reads hit S3 instead of the cache.<br>
       <b>This causes s3fs to work extremely slowly, and should never be enabled on a production site.</b>"),
    '#default_value' => !empty($config['ignore_cache']) ? $config['ignore_cache'] : FALSE,
  );
  $form['s3fs_prefix'] = array(
    '#type'           => 'textfield',
    '#title'          => t('Partial Refresh Prefix'),
    '#default_value'  => !empty($config['prefix']) ? $config['prefix'] : '',
    '#description'   => t('If you want the "Refresh file metadata cache" action to refresh only some of the contents of your bucket, provide a file path prefix in this field.<br>
      For example, setting this option to "images/" will refresh only the files with a URI that matches s3://images/*. This setting is case sensitive.'),
  );
  
  $form['s3fs_presigned_urls'] = array(
    '#type' => 'textarea',
    '#title' => t('Presigned URLs'),
    '#description' => t('A list of timeouts and paths that should be delivered through a presigned url.<br>
      Enter one value per line, in the format timeout|path. e.g. "60|private_files/*". Paths use regex patterns as per !link.
      If no timeout is provided, it defaults to 60 seconds.<br>
      <b>This feature does not work when "Enable CNAME" is used.</b>',
      array('!link' => l(t('preg_match'), 'http://php.net/preg_match'))),
    '#default_value' => !empty($config['presigned_urls']) ? $config['presigned_urls'] : '',
    '#rows' => 5,
  );
  $form['s3fs_saveas'] = array(
    '#type' => 'textarea',
    '#title' => t('Force Save As'),
    '#description' => t('A list of paths for which users will be forced to save the file, rather than displaying it in the browser.<br>
      Enter one value per line. e.g. "video/*". Paths use regex patterns as per !link.<br>
      <b>This feature does not work when "Enable CNAME" is used.</b>',
      array('!link' => l(t('preg_match'), 'http://php.net/preg_match'))),
    '#default_value' => !empty($config['saveas']) ? $config['saveas'] : '',
    '#rows' => 5,
  );
  $form['s3fs_torrents'] = array(
    '#type' => 'textarea',
    '#title' => t('Torrents'),
    '#description' => t('A list of paths that should be delivered via BitTorrent.<br>
      Enter one value per line, e.g. "big_files/*". Paths use regex patterns as per !link.<br>
      <b>Paths which are already set as Presigned URLs or Forced Save As cannot be delivered as torrents.</b>',
      array('!link' => l(t('preg_match'), 'http://php.net/preg_match'))),
    '#default_value' => !empty($config['torrents']) ? $config['torrents'] : '',
    '#rows' => 5,
  );
  
  // Once the system settings form has been fully set up, add our additional
  // submit callback.
  $form = system_settings_form($form);
  $form['#submit'][] = '_s3fs_amazons3_api_compatibility_submit';
  return $form;
}

/**
 * Validates the values on the admin form.
 */
function s3fs_settings_validate($form, &$form_state) {
  $config = _s3fs_get_config();
  $values = _s3fs_convert_form_state_to_config($form_state);
  $config = array_merge($config, $values);
  _s3fs_validate_config($config);
}

/**
 * Supplemental submit callback for the "Save" button on the admin form.
 *
 * Copies all s3fs_* variables to variables with amazons3_* names,
 * in order to preserve API compatibility with AmazonS3.
 * AmazonS3 can be found here: https://drupal.org/project/amazons3
 */
function _s3fs_amazons3_api_compatibility_submit($form, &$form_state) {
  variable_set('amazons3_bucket', variable_get('s3fs_bucket'));
}

/**
 * Converts a $form_state array to a configuration settings array.
 */
function _s3fs_convert_form_state_to_config($form_state) {
  $config = array();
  foreach ($form_state['values'] as $name => $value) {
    // If the name starts with 's3fs_'
    if (strncmp($name, 's3fs_', 5) === 0) {
      $shortname = substr($name, 5);
      $config[$shortname] = $value;
    }
  }
  return $config;
}

/**
 * Builds the Actions form.
 */
function s3fs_actions() {
  $form = array();
  
  $form['s3fs_refresh_cache'] = array(
    '#type' => 'fieldset',
    '#description' => t("The file metadata cache keeps track of every file that S3 File System writes to (and deletes from) the S3 bucket,
      so that queries for data about those files (checks for existence, filetype, etc.) don't have to hit S3.
      This speeds up many operations, most noticeably anything related to images and their derivatives."),
    '#title' => t('File Metadata Cache'),
  );
  $form['s3fs_refresh_cache']['refresh'] = array(
    '#type' => 'submit',
    '#suffix' => '<div class="refresh">' . t("This button queries S3 for the metadata of <i><b>all</b></i> the files in your site's bucket, and saves it to the database.
      This may take a while for buckets with many thousands of files. <br>
      It should only be necessary to use this button if you've just installed S3 File System and you need to cache all the pre-existing files in your bucket,
      or if you need to restore your metadata cache from scratch for some other reason.") . '</div>',
    '#value' => t('Refresh file metadata cache'),
    '#attached' => array(
      'css' => array(
        // Push the button closer to its own description, and push the disable
        // checkbox away from the description.
        '#edit-refresh {margin-bottom: 0; margin-top: 1em;} div.refresh {margin-bottom: 1em;}' => array('type' => 'inline')
      ),
    ),
    '#submit' => array('_s3fs_refresh_cache_submit'),
  );
  
  return $form;
}

/**
 * Submit callback for the "Refresh file metadata cache" button.
 */
function _s3fs_refresh_cache_submit($form, &$form_state) {
  $config = _s3fs_get_config();
  _s3fs_refresh_cache($config);
}
