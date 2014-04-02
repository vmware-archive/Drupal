<?php

/**
* @file
* Hooks provided by Storage.
*/

/**
* @addtogroup hooks
* @{
*/

/**
 * Generate a file.
 *
 * @param $storage
 *   The storage to be generated.
 * @return
 *   Filepath to the generated file.
 */
function hook_storage_generate(Storage $storage) {
}

/**
 * Determine the current user's access to a file.
 *
 * @param $storage
 *   Storage that is attempting to be accessed.
 * @return
 *   Whether access is permitted or not.
 *   - TRUE for permitted.
 *   - FALSE for denied.
 *   - NULL to inherit the parent's access.
 */
function hook_storage_access(Storage $storage) {
}

/**
 * Alter the current user's access to a file that belongs to another module.
 *
 * @param $storage
 *   Storage that is attempting to be accessed.
 * @return
 *   Access alteration.
 *   - TRUE for permitted.
 *   - FALSE for denied.
 *   - NULL for don't alter.
 */
function hook_storage_access_alter(Storage $storage) {
}

/**
 * Provides information about the storage service.
 *
 * @return
 *   An associative array, with the following keys:
 *     - 'name'
 *       Translated name of the storage service.
 *     - 'class'
 *     - 'htaccess'
 *     - 'copy'
 *     - 'serve'
 *     - 'serve_secure'
 */
function hook_storage_service_info() {
}
