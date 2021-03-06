<?php

/**
 * @file
 * Implements the Add Tag (create) grant on editing forms.
 *
 * These functions need to be included in three circumstances:
 * - Form building for forms with taxonomy fields.
 *   - taxonomy_access_field_widget_form_alter()
 *   - taxonomy_access_field_widget_taxonomy_autocomplete_form_alter()
 * - Form validation for forms with taxonomy fields.
 *   - taxonomy_access_autocomplete_validate()
 *   - taxonomy_access_options_validate()
 *   - taxonomy_access_field_attach_validate()
 * - Taxonomy autocomplete AJAX requests.
 *   - taxonomy_access_autocomplete()
 */

/**
 * @defgroup tac_create Taxonomy Access Control: Add tag (create) permission
 * @{
 * Implement access control for taxonomy terms on node editing forms.
 */

/**
 * Implements the create grant for autocomplete fields.
 *
 * - Denies access if the user cannot alter the field values.
 * - Determines whether the user can autocreate new terms for the field.
 * - Removes default values disallowed by create.
 * - Adds information on autocreate and disallowed defaults to the element so
 *   it is available to the validator.
 * - Adds the custom validator.
 * - Sets a custom autocomplete path to filter autocomplete by create.
 *
 * Some of the logic here is borrowed from taxonomy_autocomplete_validate().
 *
 * @see taxonomy_access_field_widget_taxonomy_autocomplete_form_alter()
 */
function _taxonomy_access_autocomplete_alter(&$element, &$form_state, $context) {

  // Collect a list of terms and filter out those disallowed by create.
  $filtered = array();
  foreach ($context['items'] as $item) {
    $filtered[$item['tid']] = $item;
  }
  $disallowed_defaults = taxonomy_access_create_disallowed(array_keys($filtered));
  foreach ($disallowed_defaults as $tid) {
    unset($filtered[$tid]);
  }

  // Assemble a list of all vocabularies for the field.
  $vocabularies = array();
  foreach ($context['field']['settings']['allowed_values'] as $tree) {
    if ($vocab = taxonomy_vocabulary_load($tree['vocabulary'])) {
      $vocabularies[] = $vocab->name;
    }
  }

  // Determine whether the user has create for any terms in the given vocabs.
  $allowed_terms = FALSE;
  foreach ($vocabularies as $vocabulary) {
    $terms = taxonomy_access_user_create_terms_by_vocab($vocabulary);
    if (!empty($terms)) {
      $allowed_terms = TRUE;
      break;
    }
  }

  // Filter the vocabularies to vocabs in which the user may create new terms.
  $allowed_vocabularies = taxonomy_access_create_default_allowed($vocabularies);

  // If the field already has the maximum number of values, and all of these
  // values are disallowed, deny access to the field.
  if ($context['field']['cardinality'] != FIELD_CARDINALITY_UNLIMITED) {
    if (sizeof($disallowed_defaults) >= $context['field']['cardinality']) {
      $element['#access'] = FALSE;
    }
  }

  // If the user may not create any terms on this field, deny access.
  if (empty($allowed_vocabularies) && !$allowed_terms) {
    $element['#access'] = FALSE;
  }

  // Set the default value from the filtered item list.
  $element['#default_value'] =
    taxonomy_access_autocomplete_default_value($filtered);
    
  // Custom validation.  Set values for the validator indicating:
  // 1. Whether the user can autocreate terms in this field (vocab. default).
  // 2. Which tids were removed due to create restrictions.
  $element['#allow_autocreate'] = empty($allowed_vocabularies) ? FALSE : TRUE;
  $element['#disallowed_defaults'] = $disallowed_defaults;
  $element['#element_validate'] =
    array('taxonomy_access_autocomplete_validate');

  // Use a custom autocomplete path to filter by create rather than list.
  $element['#autocomplete_path'] =
    'taxonomy_access/autocomplete/' . $context['field']['field_name'];

  unset($context);
}


/**
 * Implements the create grant for options widgets.
 *
 * - Denies access if the user cannot alter the field values.
 * - Attaches jQuery to disable values disallowed by create.
 * - Adds the disallowed values from the element so they are available to the
 *   custom validator.
 * - Adds the custom validator.
 *
 * We use jQuery to disable the options because of FAPI limitations:
 * @see http://drupal.org/node/284917
 * @see http://drupal.org/node/342316
 * @see http://drupal.org/node/12089
 *
 * @see taxonomy_access_field_widget_form_alter()
 */
function _taxonomy_access_options_alter(&$element, &$form_state, $context) {

  // Check for an existing entity ID.
  $entity_id = 0;
  if (!empty($form_state['build_info']['args'][0])) {
    $info = entity_get_info($context['instance']['entity_type']);
    $pseudo_entity = (object) $form_state['build_info']['args'][0];
    if (isset($pseudo_entity->{$info['entity keys']['id']})) {
      $entity_id = $pseudo_entity->{$info['entity keys']['id']};
    }
  }
  // Collect a list of terms and determine which are allowed
  $tids = array_keys($element['#options']);

  // Ignore the "none" option if present.
  $key = array_search('_none', $tids);
  if ($key !== FALSE) {
    unset($tids[$key]);
  }

  $allowed_tids = taxonomy_access_create_allowed($tids);
  $disallowed_tids = taxonomy_access_create_disallowed($tids);

  // If no options are allowed, deny access to the field.
  if (empty($allowed_tids)) {
    $element['#access'] = FALSE;
  }

  // On node creation, simply remove disallowed default values.
  if (!$entity_id) {
    $disallowed_defaults = array();
    if (is_array($element['#default_value'])) {
      foreach ($element['#default_value'] as $key => $value) {
        if (in_array($value, $disallowed_tids)) {
          unset($element['#default_value'][0]);
        }
      }
    }
    elseif (in_array($element['#default_value'], $disallowed_tids)) {
      unset($element['#default_value']);
    }
  }
  // If the node already exists, check:
  // 1. Whether the field already has the maximum number of values
  // 2. Whether all of these values are disallowed.
  // If both these things are true, then the user cannot edit the field's
  // value, so disallow access.
  else {
    $defaults =
      is_array($element['#default_value'])
      ? $element['#default_value']
      : array($element['#default_value'])
      ;

    $disallowed_defaults =
      array_intersect($defaults, $disallowed_tids);

    if ($context['field']['cardinality'] != FIELD_CARDINALITY_UNLIMITED) {
      if (sizeof($disallowed_defaults) >= $context['field']['cardinality']) {
        $element['#access'] = FALSE;
      }
    }
  }

  // If there are disallowed, terms, add CSS and JS for jQuery.
  // We use jQuery because FAPI does not currently support attributes
  // for individual options.
  if (!empty($disallowed_tids)) {

    // Add a css class to the field that we can use in jQuery.
    $class_name = 'tac_' . $element['#field_name'];
    $element['#attributes']['class'][] = $class_name;

    // Add js for disabling create options.
    $settings[] = array(
      'field' => $class_name,
      'disallowed_tids' => $disallowed_tids,
      'disallowed_defaults' => $disallowed_defaults,
    );
    $element['#attached']['js'][] =
      backdrop_get_path('module', 'taxonomy_access') . '/js/tac_create.js';
    $element['#attached']['js'][] = array(
      'data' => array('taxonomy_access' => $settings),
      'type' => 'setting',
    );
  }

  $element['#disallowed_defaults'] = $disallowed_defaults;
  $element['#element_validate'] = array('taxonomy_access_options_validate');
}

/**
 * Retrieve terms that the current user may create.
 *
 * @return array|true
 *   An array of term IDs, or TRUE if the user may create all terms.
 *
 * @see taxonomy_access_user_create_terms_by_vocab()
 * @see _taxonomy_access_user_term_grants()
 */
function taxonomy_access_user_create_terms() {
  // Cache the terms the current user can create.
  $terms = &backdrop_static(__FUNCTION__, NULL);
  if (is_null($terms)) {
    $terms = _taxonomy_access_user_term_grants(TRUE);
  }
  return $terms;
}

/**
 * Retrieve terms that the current user may create in specific vocabularies.
 *
 * @param varchar $vocabulary
 *   A vocabulary name to use as a filter.
 *
 * @return array|true
 *   An array of term IDs, or TRUE if the user may create all terms.
 *
 * @see taxonomy_access_user_create_terms()
 * @see _taxonomy_access_user_term_grants()
 */
function taxonomy_access_user_create_terms_by_vocab($vocabulary) {
  // Cache the terms the current user can create per vocabulary.
  static $terms = array();
  if (!isset($terms[$vocabulary])) {
    $terms[$vocabulary] = _taxonomy_access_user_term_grants(TRUE, array($vocabulary));
  }
  return $terms[$vocabulary];
}

/**
 * Retrieve terms that the current user may create.
 *
 * @return array|true
 *   An array of term IDs, or TRUE if the user may create all terms.
 *
 * @see _taxonomy_access_create_defaults()
 */
function taxonomy_access_user_create_defaults() {
  // Cache the terms the current user can create.
  static $vocabularies = NULL;
  if (is_null($vocabularies)) {
    $vocabularies = _taxonomy_access_create_defaults();
  }
  return $vocabularies;
}

/**
 * Check a list of term IDs for terms the user may not create.
 *
 * @param array $tids
 *   The array of term IDs.
 *
 * @return array
 *   An array of disallowed term IDs.
 */
function taxonomy_access_create_disallowed(array $tids) {
  $all_allowed = taxonomy_access_user_create_terms();

  // If the user's create grant info is exactly TRUE, no terms are disallowed.
  if ($all_allowed === TRUE) {
    return array();
  }

  return array_diff($tids, $all_allowed);
}

/**
 * Filter a list of term IDs to terms the user may create.
 *
 * @param array $tids
 *   The array of term IDs.
 *
 * @return array
 *   An array of disallowed term IDs.
 */
function taxonomy_access_create_allowed(array $tids) {
  $all_allowed = taxonomy_access_user_create_terms();

  // If the user's create grant info is exactly TRUE, all terms are allowed.
  if ($all_allowed === TRUE) {
    return $tids;
  }

  return array_intersect($tids, $all_allowed);
}

/**
 * Filter a list of vocab IDs to those in which the user may create by default.
 *
 * @param array $vocabularies
 *   The array of vocabulary names.
 *
 * @return array
 *   An array of disallowed vocabulary names.
 */
function taxonomy_access_create_default_allowed(array $vocabularies) {
  $all_allowed = taxonomy_access_user_create_defaults();

  // If the user's create grant info is exactly TRUE, all terms are allowed.
  if ($all_allowed === TRUE) {
    return $vocabularies;
  }

  return array_intersect($vocabularies, $all_allowed);
}


/**
 * Retrieve vocabularies in which the current user may create terms.
 *
 * @param object|null $account
 *   (optional) The account for which to retrieve grants.  If no account is
 *   passed, the current user will be used.  Defaults to NULL.
 *
 * @return array
 *   An array of term IDs, or TRUE if the user has the grant for all terms.
 */
function _taxonomy_access_create_defaults($account = NULL) {

  // If the user can administer taxonomy, return TRUE for a global grant.
  if (user_access('administer taxonomy', $account)) {
    return TRUE;
  }

  // Build a term grant query.
  $query = _taxonomy_access_grant_query(array('create'), TRUE);

  // Select term grants for the current user's roles.
  if (is_null($account)) {
    global $user;
    $account = $user;
  }
  $query
    ->fields('tadg', array('vocabulary'))
    ->groupBy('tadg.vocabulary')
    ->condition('tadg.role', array_keys($account->roles), 'IN')
    ;

  // Fetch term IDs.
  $r = $query->execute()->fetchAll();
  $vocabularies = array();

  // If there are results, initialize a flag to test whether the user
  // has the grant for all terms.
  $grants_for_all_vocabs = empty($r) ? FALSE : TRUE;

  foreach ($r as $record) {
    // If the user has the grant, add the term to the array.
    if ($record->grant_create) {
      $vocabularies[] = $record->vocabulary;
    }
    // Otherwise, flag that the user does not have the grant for all terms.
    else {
      $grants_for_all_vocabs = FALSE;
    }
  }

  // If the user has the grant for all terms, return TRUE for a global grant.
  if ($grants_for_all_vocabs) {
    return TRUE;
  }

  return $vocabularies;
}

/**
 * Autocomplete menu callback: filter allowed terms by create, not list.
 *
 * For now we essentially duplicate the code from taxonomy.module, because
 * it calls backdrop_json_output without providing the logic separately.
 *
 * @see taxonomy_autocomplete()
 *
 * Page callback: Outputs JSON for taxonomy autocomplete suggestions.
 *
 * This callback outputs term name suggestions in response to Ajax requests
 * made by the taxonomy autocomplete widget for taxonomy term reference
 * fields. The output is a JSON object of plain-text term suggestions, keyed by
 * the user-entered value with the completed term name appended.  Term names
 * containing commas are wrapped in quotes.
 *
 * For example, suppose the user has entered the string 'red fish, blue' in the
 * field, and there are two taxonomy terms, 'blue fish' and 'blue moon'. The
 * JSON output would have the following structure:
 * @code
 *   {
 *     "red fish, blue fish": "blue fish",
 *     "red fish, blue moon": "blue moon",
 *   };
 * @endcode
 *
 * @param $field_name
 *   The name of the term reference field.
 * @param $tags_typed
 *   (optional) A comma-separated list of term names entered in the
 *   autocomplete form element. Only the last term is used for autocompletion.
 *   Defaults to '' (an empty string).
 *
 * @see taxonomy_menu()
 * @see taxonomy_field_widget_info()
 */
function taxonomy_access_autocomplete($field_name = '', $tags_typed = '') {
  // Enforce that list grants do not filter the autocomplete.
  taxonomy_access_disable_list();

  // Make sure the field exists and is a taxonomy field.
  if (!($field = field_info_field($field_name)) || $field['type'] !== 'taxonomy_term_reference') {
    // Error string. The JavaScript handler will realize this is not JSON and
    // will display it as debugging information.
    print t('Taxonomy field @field_name not found.', array('@field_name' => $field_name));
    exit;
  }

  // The user enters a comma-separated list of tags. We only autocomplete the last tag.
  $tags_typed = backdrop_explode_tags($tags_typed);
  $tag_last = backdrop_strtolower(array_pop($tags_typed));

  $term_matches = array();
  if ($tag_last != '') {
    // Part of the criteria for the query come from the field's own settings.
    $vocabularies = array();
    $vocabularies = taxonomy_vocabulary_load_multiple(FALSE);
    foreach ($field['settings']['allowed_values'] as $tree) {
      $vocabulary_names[] = $vocabularies[$tree['vocabulary']]->name;
    }

    $query = db_select('taxonomy_term_data', 't');
    $query->addTag('translatable');
    $query->addTag('taxonomy_term_access');
    //$query->addTag('term_access');

    // Do not select already entered terms.
    if (!empty($tags_typed)) {
      $query->condition('t.name', $tags_typed, 'NOT IN');
    }
    // Select rows that match by term name.
    $vocabulary_list = isset($field['settings']['allowed_values']) ? $field['settings']['allowed_values'] : array();
    $tags_return = $query
      ->fields('t', array('tid', 'name'))
      ->condition('t.vocabulary', $vocabulary_list)
      ->condition('t.name', '%' . db_like($tag_last) . '%', 'LIKE')
      ->range(0, 10)
      ->execute()
      ->fetchAllKeyed();

    // Unset suggestions disallowed by create grants.
    $disallowed = taxonomy_access_create_disallowed(array_keys($tags_return));
    foreach ($disallowed as $tid) {
      unset($tags_return[$tid]);
    }

    $prefix = count($tags_typed) ? backdrop_implode_tags($tags_typed) . ', ' : '';

    foreach ($tags_return as $tid => $name) {
      $n = $name;
      // Term names containing commas or quotes must be wrapped in quotes.
      if (strpos($name, ',') !== FALSE || strpos($name, '"') !== FALSE) {
        $n = '"' . str_replace('"', '""', $name) . '"';
      }
      $term_matches[$prefix . $n] = check_plain($name);
    }
  }

  // Displayed as JSON by delivery callback backdrop_json_deliver().
  return $term_matches;
}


/**
 * Validates taxonomy autocomplete values for create grants.
 *
 * For now we essentially duplicate the code from taxonomy.module, because
 * it calls form_set_value() without providing the logic separately.
 *
 * We use two properties set in hook_field_widget_form_alter():
 *  - $element['#allow_autocreate']
 *  - $element['#disallowed_defaults']
 *
 * @todo
 *   Specify autocreate per vocabulary?
 *
 * @see taxonomy_autocomplete_validate()
 * @see taxonomy_access_autocomplete()
 * @see taxonomy_access_field_widget_taxonomy_autocomplete_form_alter()
 */
function _taxonomy_access_autocomplete_validate($element, &$form_state) {
  $langcode = LANGUAGE_NONE;
  if (isset($form_state['values']['langcode'])) {
    $langcode = $form_state['values']['langcode'];
  }
  // Autocomplete widgets do not send their tids in the form, so we must detect
  // them here and process them independently.
  $value = array();
  if ($tags = $element['#value']) {
    // Collect candidate vocabularies.
    $field = field_widget_field($element, $form_state);
    $instance = field_widget_instance($element, $form_state);
    $vocabularies = array();
    foreach ($field['settings']['allowed_values'] as $tree) {
      if ($vocabulary = taxonomy_vocabulary_load($tree['vocabulary'])) {
        $vocabularies[$vocabulary->machine_name] = $vocabulary;
      }
    }

    // Translate term names into actual terms.
    $typed_terms = backdrop_explode_tags($tags);
    foreach ($typed_terms as $typed_term) {
      // See if the term exists in the chosen vocabulary and return the tid;
      // otherwise, create a new 'autocreate' term for insert/update.
      if ($possibilities = taxonomy_term_load_multiple(array(), array('name' => trim($typed_term), 'vocabulary' => array_keys($vocabularies)))) {
        $term = array_pop($possibilities);
      }
      // Only autocreate if the user has create for the vocab. default.
      elseif ($element['#allow_autocreate']) {
        $vocabulary = reset($vocabularies);
        $term = array(
          'tid' => 'autocreate',
          'vocabulary' => $vocabulary->machine_name,
          'langcode' => LANGUAGE_NONE,
          'name' => $typed_term,
        );
        if (module_exists('language') && $vocabulary->language == TAXONOMY_LANGUAGE_ENABLED  && $instance['widget']['settings']['follow_content_language']) {
          $term['langcode'] = $langcode;
        }
      }
      // If they cannot autocreate and this is a new term, set an error.
      else {
        form_error(
          $element,
          t('You may not create new tags in %name.',
            array('%name' => t($element['#title']))
          )
        );
      }
      if ($term) {
        $value[] = (array) $term;
      }
    }
  }

  // Add in the terms that were disallowed.
  // taxonomy.module expects arrays, not objects.
  $disallowed = taxonomy_term_load_multiple($element['#disallowed_defaults']);
  foreach ($disallowed as $key => $term) {
    $disallowed[$key] = (array) $term;
  }
  $value = array_merge($value, $disallowed);

  // Subsequent validation will be handled by hook_field_attach_validate().
  // Set the value in the form.
  form_set_value($element, $value, $form_state);
}

/**
 * Form element validation handler for taxonomy option fields.
 *
 * We use a property set in hook_field_widget_form_alter():
 *  - $element['#disallowed_defaults']
 *
 * @see options_field_widget_validate()
 * @see taxonomy_access_field_widget_form_alter()
 */
function _taxonomy_access_options_validate($element, &$form_state) {
  if ($element['#required'] && $element['#value'] == '_none') {
    form_error($element, t('!name field is required.', array('!name' => $element['#title'])));
  }

  // Clone the element and add in disallowed defaults.
  $el = $element;
  if (!empty($element['#disallowed_defaults'])) {
    if (empty($el['#value'])) {
      $el['#value'] = $element['#disallowed_defaults'];
    }
    elseif (is_array($el['#value'])) {
      $el['#value'] = array_unique(array_merge($el['#value'], $element['#disallowed_defaults']));
    }
    else {
      $el['#value'] = array_unique(array_merge(array($el['#value']), $element['#disallowed_defaults']));
    }
  }

  // Transpose selections from field => delta to delta => field, turning
  // multiple selected options into multiple parent elements.
  $items = _options_form_to_storage($el);

  // Subsequent validation will be handled by hook_field_attach_validate().
  // Set the value in the form.
  form_set_value($element, $items, $form_state);
}

/**
 * Default value re-generation for autocomplete fields.
 *
 * @param array $items
 *   An array of values from form build info, filtered by create grants.
 *
 * @return string
 *   Field default value.
 *
 * @see taxonomy_field_widget_form()
 */
function taxonomy_access_autocomplete_default_value(array $items) {
  // Preserve the original state of the list flag.
  $flag_state = taxonomy_access_list_enabled();

  // Enforce that list grants do not filter the options list.
  taxonomy_access_disable_list();

  // Assemble list of tags.
  $tags = array();
  foreach ($items as $item) {
    $tags[$item['tid']] = isset($item['taxonomy_term']) ? $item['taxonomy_term'] : taxonomy_term_load($item['tid']);
  }

  // Assemble the default value using taxonomy.module.
  $tags = taxonomy_implode_tags($tags);

  // Restore list flag to previous state.
  if ($flag_state) {
    taxonomy_access_enable_list();
  }

  return $tags;
}

/**
 * Validates form submissions of taxonomy fields for create grants.
 *
 * @todo
 *   Use field label and term names in errors rather than field name and tids.
 *
 * @see http://drupal.org/node/1220212
 * @see entity_form_field_validate()
 */
function _taxonomy_access_field_validate($entity_type, $entity, &$errors) {
  // Check for a pre-existing entity (i.e., the entity is being updated).
  $old_fields = FALSE;

  // The entity is actually a "pseudo-entity," and the user profile form
  // neglects to include the uid. So, we need to load it manually.
  if ($entity_type == 'user') {
    // Some modules which extend the user profile form cause additional
    // validation to happen with "pseudo-entities" that do not include the
    // name. So, check if it exists.
    if (isset($entity->name)) {
      if ($account = user_load_by_name($entity->name)) {
        $entity->uid = $account->uid;
      }
    }
  }

  list($entity_id, , $bundle) = entity_extract_ids($entity_type, $entity);
  if ($entity_id) {
    // Load the entity.
    $old_entity = entity_load($entity_type, array($entity_id));
    $old_entity = $old_entity[$entity_id];

    // Fetch the original entity's taxonomy fields.
    $old_fields =
      _taxonomy_access_entity_fields($entity_type, $old_entity, $bundle);
  }

  // Fetch the updated entity's taxonomy fields.
  $new_fields =
    _taxonomy_access_entity_fields($entity_type, $entity, $bundle);

  // Set errors if there are any disallowed changes.
  $changes = _taxonomy_access_compare_fields($new_fields, $old_fields);

  // We care about the overall value list, so delta is not important.
  $delta = 0;

  // Check each field and langcode for disallowed changes.
  foreach ($changes as $field_name => $langcodes) {
    foreach ($langcodes as $langcode => $disallowed) {
      if ($disallowed) {
        if (!empty($disallowed['added'])) {
          $text = 'You may not add the following tags to %field: %tids';
          $errors[$field_name][$langcode][$delta][] = array(
            'error' => 'taxonomy_access_disallowed_added',
            'message' => t($text, array(
              '%field' => $field_name,
              '%tids' => implode(', ', $disallowed['added']),
            )),
          );
        }
        if (!empty($disallowed['removed'])) {
          $text = 'You may not remove the following tags from %field: %tids';
          $errors[$field_name][$langcode][$delta][] = array(
            'error' => 'taxonomy_access_disallowed_removed',
            'message' => t($text, array(
              '%field' => $field_name,
              '%tids' => implode(', ', $disallowed['removed']),
            )),
          );
        }
      }
    }
  }
}


/**
 * Helper function to extract the taxonomy fields from an entity.
 *
 * @param object $entity
 *   The entity object.
 *
 * @return array
 *   An associative array of field information, containing:
 *   - field_list: A flat array of all this entity's taxonomy fields, with the
 *     format $field_name => $field_name.
 *   - langcodes: A flat array of all langcodes in this entity's fields, with
 *     the format $langcode => $langcode.
 *   - data: An associative array of non-empty fields:
 *     - $field_name: An associative array keyed by langcode.
 *       - $langcode: Array of field values for this field name and langcode.
 *
 * @see http://drupal.org/node/1220168
 */
function _taxonomy_access_entity_fields($entity_type, $entity, $bundle) {
  // Maintain separate lists of field names and langcodes for quick comparison.
  $fields = array();
  $fields['field_list'] = array();
  $fields['langcodes'] = array();
  $fields['data'] = array();

  // If there is no entity, return the empty structure.
  if (!$entity) {
    return $fields;
  }

  // Get a list of possible fields for the bundle.
  // The bundle does not contain the field type (see #122016), so our only use
  // for it is the field names.
  $possible = array_keys(field_info_instances($entity_type, $bundle));

  // Sort through the entity for relevant field data.
  foreach ($entity as $field_name => $field) {

    // Only proceed if this element is a valid field for the bundle.
    if (in_array($field_name, $possible, TRUE)) {

      // Check whether each entity field is a taxonomy field.
      $info = field_info_field($field_name);
      if ($info['type'] == 'taxonomy_term_reference') {
        foreach ($field as $langcode => $values) {

          // Add non-empty fields to the lists.
          if (!empty($values)) {
            $fields['langcodes'][$langcode] = $langcode;
            $fields['field_list'][$field_name] = $field_name;
            $fields['data'][$field_name][$langcode] = $values;
          }
          unset($values);
        }
      }
    }
    unset($info);
    unset($field);
  }

  unset($entity);

  return $fields;
}

/**
 * Helper function to compare field values and look for disallowed changes.
 *
 * @param array $new
 *   An associative array of the updated field information as returned by
 *   _taxonomy_access_entity_fields().
 * @param array $old
 *   (optional) An associative array of the original field information,
 *   or FALSE if there is no original field data.  Defaults to FALSE.
 *
 * @return array
 *   An array of disallowed changes, with the structure:
 *   - $field_name: An associative array keyed by langcode.
 *     - $langcode: Disallowed changes for this field name and langcode,
 *       or FALSE if none.
 *       - 'added' => An array of added terms that are disallowed.
 *       - 'removed' => An array of removed termss that are disallowed.
 *
 * @see _taxonomy_access_entity_fields()
 * @see _taxonomy_access_disallowed_changes()
 */
function _taxonomy_access_compare_fields($new, $old = FALSE) {
  $disallowed_changes = array();

  // If there are no original fields, simply process new.
  if (!$old) {
    foreach ($new['data'] as $field_name => $langcodes) {
      foreach ($langcodes as $langcode => $values) {
        $changes = _taxonomy_access_disallowed_changes($values, array());
        if ($changes) {
          if (!isset($disallowed_changes[$field_name])) {
            $disallowed_changes[$field_name] = array();
          }
          $disallowed_changes[$field_name][$langcode] = $changes;
        }
      }
    }
  }

  // Otherwise, aggregate and compare field data.
  else {
    $all_fields = $new['field_list'] + $old['field_list'];
    $all_langcodes = $new['langcodes'] + $old['langcodes'];

    foreach ($all_fields as $field_name) {
      foreach ($all_langcodes as $langcode) {
        $new_values = array();
        if (isset($new['field_list'][$field_name])
           && isset($new['data'][$field_name][$langcode])) {
          $new_values = $new['data'][$field_name][$langcode];
        }
        $old_values = array();
        if (isset($old['field_list'][$field_name])
           && isset($old['data'][$field_name][$langcode])) {
          $old_values = $old['data'][$field_name][$langcode];
        }
        $changes = _taxonomy_access_disallowed_changes($new_values, $old_values);
        if ($changes) {
          if (!isset($disallowed_changes[$field_name])) {
            $disallowed_changes[$field_name] = array();
          }
          $disallowed_changes[$field_name][$langcode] = $changes;
        }
      }
    }
  }

  unset($old);
  unset($new);
  return $disallowed_changes;
}

/**
 * Helper function to check for term reference changes disallowed by create.
 *
 * @param array $new_field
 *   The entity or form values of the updated field.
 * @param array $old_field
 *   The entity or form values of the original field.
 *
 * @return array|false
 *   Returns FALSE if there are no disallowed changes.  Otherwise, an array:
 *   - 'added' => An array of added terms that are disallowed.
 *   - 'removed' => An array of removed termss that are disallowed.
 */
function _taxonomy_access_disallowed_changes(array $new_field, array $old_field) {

  // Assemble a list of term IDs from the original entity, if any.
  $old_tids = array();
  foreach ($old_field as $old_item) {
    // Some things are NULL for some reason.
    if ($old_item['tid']) {
      $old_tids[] = $old_item['tid'];
    }
  }

  // Assemble a list of term IDs from the updated entity.
  $new_tids = array();
  foreach ($new_field as $delta => $new_item) {
    // Some things are NULL for some reason.
    // Allow the special tid "autocreate" so users can create new terms.
    if ($new_item['tid'] && ($new_item['tid'] != 'autocreate')) {
      $new_tids[$delta] = $new_item['tid'];
    }
  }

  // Check for added tids, and unset ones the user may not add.
  $added = array_diff($new_tids, $old_tids);
  $may_not_add = taxonomy_access_create_disallowed($added);

  // Check for removed tids, and restore ones the user may not remove.
  $removed = array_diff($old_tids, $new_tids);
  $may_not_remove = taxonomy_access_create_disallowed($removed);

  // If there were any disallowed changes, return them.
  if (!empty($may_not_add) || !empty($may_not_remove)) {
    return array('added' => $may_not_add, 'removed' => $may_not_remove);
  }

  // Return FALSE if all changes were valid.
  return FALSE;
}

/**
 * End of "defgroup tac_create".
 * @}
 */
