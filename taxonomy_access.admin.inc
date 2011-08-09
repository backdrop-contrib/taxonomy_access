<?php
/**
 * @file
 * Administrative interface for taxonomy access control.
 */

/**
 * Menu callback (for form at TAXONOMY_ACCESS_CONFIG path).
 * Renders the TAC permissions administration form.
 *
 * @param $op
 *   (optional) The operation to perform, if any.  Defaults to NULL.
 *   Options are 'edit' or 'delete' (access rules for the role).
 * @param $rid
 *   (optional) The role ID of the role to configure.  Defaults to NULL.
 * @param $arg
 *   (optional) Additional arguments.  Defaults to NULL.  (Not used.)
 *
 * @return
 *     Form to render.
 */
function taxonomy_access_admin($op = NULL, $rid = NULL, $arg = NULL) {
  $roles = _taxonomy_access_user_roles();
  if (is_numeric($rid) AND isset($roles[$rid])) {
    switch ($op) {
    case 'edit':
      return drupal_get_form('taxonomy_access_admin_form', $rid);
    case 'delete':
      return drupal_get_form('taxonomy_access_admin_delete_role', $rid);
    }
  }
  elseif (!isset($op) AND !isset($rid)) {
    return theme_taxonomy_access_admin();
  }
  /**
   * @todo
   *     Something odd happens here.
   */
  else return drupal_not_found();
}

/**
 * Renders the main administration page, with links to configure each role.
 *
 * @return
 *     Themed table with one row for each role.
 * @todo
 *     Can we move the configuration items into hook_menu?
 */
function theme_taxonomy_access_admin() {
  $roles = _taxonomy_access_user_roles();

  // Render role/permission overview:
  $header = array(t('Role'), array('data' => '&nbsp;'));
  $rows = array();

  $active_role_ids = db_query('SELECT rid FROM {taxonomy_access_default} WHERE vid = 0')->fetchCol();
  $active = array();
  foreach ($active_role_ids as $rid) {
    $active[$rid] = TRUE;
  }
  foreach ($roles as $rid => $name) {
    $ops = array();
    if (!empty($active[$rid])) {
      //only allow delete for "extra" roles
      if ($rid != DRUPAL_ANONYMOUS_RID && $rid != DRUPAL_AUTHENTICATED_RID) {
        $ops[] = l(t("disable"), TAXONOMY_ACCESS_CONFIG . "/delete/$rid");
      }
      $ops[] = l(t("edit"), TAXONOMY_ACCESS_CONFIG . "/edit/$rid");
    }
    else {
      $ops = array(l(t("enable"), TAXONOMY_ACCESS_CONFIG . "/edit/$rid"));
    }
    $rows[] = array($name, array('data' => implode(' | ', $ops), 'align' => 'right'));
  }

  return theme('table', array('header' => $header, 'rows' => $rows));
}

/**
 * Form callback to delete all access rules for a particular role.
 *
 * @param $rid
 *   The role ID to disable.
 */
function taxonomy_access_admin_delete_role($form, $form_state, $rid) {
  if (is_numeric($rid)
      && $rid != DRUPAL_ANONYMOUS_RID
      && $rid != DRUPAL_AUTHENTICATED_RID) {
    $role_global_default = db_query(
      'SELECT grant_view, grant_update, grant_delete
       FROM {taxonomy_access_default}
       WHERE vid = 0 AND rid = :rid', array(':rid' => $rid))
      ->fetchAssoc();
    if ($role_global_default) {
      $form['rid'] = array(
        '#type' => 'value',
        '#value' => $rid,
      );

      return confirm_form($form,
        t('Are you sure you want to delete all grant rules for role %rid?',
          array('%rid' => $rid)),
        TAXONOMY_ACCESS_CONFIG,
        t('This action cannot be undone.'),
        t('Delete all'),
        t('Cancel'));
    }
  }

  return drupal_not_found();
}

/**
 * Form callback to delete all access rules for a particular role.
 *
 * @param $rid
 *   The role id to disable.
 */
function taxonomy_access_admin_delete_role_submit($form, $form_state) {
  $rid = $form_state['values']['rid'];

  if (is_numeric($rid)
      && $rid != DRUPAL_ANONYMOUS_RID
      && $rid != DRUPAL_AUTHENTICATED_RID) {
    $role_global_default = db_query(
      'SELECT grant_view, grant_update, grant_delete
       FROM {taxonomy_access_default}
       WHERE vid = 0 AND rid = :rid', array(':rid' => $rid))
      ->fetchAssoc();
    if ($role_global_default) {
      if ($form_state['values']['confirm']) {

        // Compare this role's global default node access to auth user role.
        $auth_global_default = db_query(
          'SELECT grant_view, grant_update, grant_delete
           FROM {taxonomy_access_default}
           WHERE vid = 0 AND rid = :rid', array(':rid' => DRUPAL_AUTHENTICATED_RID))
          ->fetchAssoc();

        if ($role_global_default == $auth_global_default) {
          $affected_nodes = _taxonomy_access_get_nodes_for_role($rid);
        }
        else {
          // If the global default is different, this role's access changes on
          // all uncontrolled nodes, so flag node access for rebuild.
          _taxonomy_access_flag_rebuild();
        }
        taxonomy_access_delete_role_grants($rid);
        if (!empty($affected_nodes)) {
          taxonomy_access_affected_nodes($affected_nodes);
        }

        drupal_set_message(t('All term access rules deleted for role %rid.',
            array('%rid' => $rid)));
        drupal_goto(TAXONOMY_ACCESS_CONFIG);
      }
    }
  }
}

/**
 * Assembles a row of grant options for a term or default on the admin form.
 *
 * @param (array) $grants
 *   (optional) An array of grants to use as form defaults.  Defaults to NULL.
 */
function taxonomy_access_admin_build_row(array $grants = NULL) {
  $form['#title'] = '';
  $form['#tree'] = TRUE;
  foreach (array('view', 'update', 'delete') as $grant) {
    $form[$grant] = array(
      '#type' => 'radios',
      // 1: Allow, 0: Ignore, 2: Deny
      '#options' => array('1' => '', '0' => '', '2' => ''),
      '#default_value' => is_string($grants['grant_' . $grant]) ? $grants['grant_' . $grant] : '0',
      '#required' => TRUE,
    );
  }
  foreach (array('create', 'list') as $grant) {
    $form[$grant] = array(
      '#type' => 'checkbox',
      '#default_value' => is_string($grants['grant_' . $grant]) ? $grants['grant_' . $grant] : '0',
    );
  }
  return $form;
}

/**
 * Form for managing grants by role.
 *
 * @param $rid
 *   (optional) The role ID.  Defaults to NULL.
 */
function taxonomy_access_admin_form($form, $form_state, $rid = NULL) {
  // Preserve the original state of the list flag.
  $flag_state = taxonomy_access_disable_list();

  // Disable list filtering while preparing this form.
  taxonomy_access_disable_list(TRUE);

  // Fetch all default grants.
  $default_grants = db_query('SELECT * FROM {taxonomy_access_default} WHERE rid = :rid', array(':rid' => $rid))->fetchAllAssoc('vid', PDO::FETCH_ASSOC);

  // If we are adding a role, no global default is set yet, so insert it now.
  if (empty($default_grants[0]) && isset($rid)) {
    // Assemble a $row object for Schema API.
    $row = new stdClass();
    $row->vid = 0;
    $row->rid = $rid;

    // Insert the row with defaults for all grants.
    drupal_write_record('taxonomy_access_default', $row);
  }

  // Fetch all grants.
  $grants = db_query('SELECT * FROM {taxonomy_access_term} WHERE rid = :rid', array(':rid' => $rid))->fetchAllAssoc('tid', PDO::FETCH_ASSOC);

  $form['instructions'] = array(
    '#markup' => _taxonomy_access_admin_instructions_html(),
    '#weight' => '20',
  );
  $form['rid'] = array('#type' => 'value', '#value' => $rid);
  $form['grants'] = $form['selected_terms'] = $form['selected_defaults'] = array('#tree' => TRUE);

  // Global default.
  $global_defaults = empty($default_grants[0]) ? NULL : $default_grants[0];
  $form['vocabs'][0]['#title'] = 'Global';
  $form['grants'][0][0] = taxonomy_access_admin_build_row($global_defaults);
  $form['selected_defaults'][0] = array(
     '#type' => 'checkbox',
     '#disabled' => TRUE,
     '#title' => t('<em>default</em>'),
     '#description' => t("can't be disabled without disabling TAC for this role"),
  );

  foreach (taxonomy_get_vocabularies() as $vid => $vocabulary) {
    $form['vocabs'][$vid]['#title'] = check_plain($vocabulary->name);
    if (isset($default_grants[$vid])) {
      $form['grants'][$vid][0] = taxonomy_access_admin_build_row($default_grants[$vid]);
      $form['selected_defaults'][$vid] = array(
         '#type' => 'checkbox',
         '#title' => t('<em>default</em>'),
      );
    }
    else {
      $add_items[$vocabulary->name]["default $vid"] = t('*default*');
    }

    if ($tree = taxonomy_get_tree($vid)) {
      foreach ($tree as $term) {
        if (isset($grants[$term->tid])) {
          $form['grants'][$vid][$term->tid] = taxonomy_access_admin_build_row($grants[$term->tid]);
          $form['selected_terms'][$term->tid] = array(
             '#type' => 'checkbox',
             '#title' => str_repeat('&nbsp;&nbsp;', $term->depth) . check_plain($term->name),
          );
        }
        else {
          $add_items[$vocabulary->name]["term $term->tid"] = str_repeat('-', $term->depth) . check_plain($term->name);
        }
      }
    }
  }
  // New grant row.
  if (isset($add_items)) {
    $form['new']['grants'] = taxonomy_access_admin_build_row();
    $form['new']['#tree'] = TRUE;
    $form['new']['item'] = array(
      '#type' => 'select',
      '#options' => $add_items,
    );
    $form['new']['recursive'] = array(
      '#type' => 'checkbox',
      '#title' => t('with children'),
      '#description' => t('Add child terms recursively with these values.'),
    );
    $form['new']['add'] = array(
      '#type' => 'submit',
      '#value' => t('Add'),
    );
  }

  $form['actions'] = array('#type' => 'actions');

  $form['actions']['delete'] = array(
    '#type' => 'submit',
    '#value' => t('Delete selected'),
  );

  $form['actions']['submit'] = array(
    '#type' => 'submit',
    '#value' => t('Save all'),
  );

  // Restore list flag to previous state.
  taxonomy_access_disable_list($flag_state);

  return $form;
}

/**
 * Renders the permission matrix user form for a given user role.
 *
 * @return
 *     String containing rendered HTML form in table.
 */
function theme_taxonomy_access_admin_form($variables) {
  $form = $variables['form'];

  $roles = _taxonomy_access_user_roles();
  $header = array(
    array( 'data' => t('Category'), 'colspan' => 3),
    array( 'data' => t('View'), 'colspan' => 4),
    array( 'data' => t('Update'), 'colspan' => 4),
    array( 'data' => t('Delete'), 'colspan' => 4),
    array( 'data' => t('Add Tag')),
    array( 'data' => t('View Tag')),
  );
  $sub_header = array(
    '&nbsp;<strong>' . t('<acronym title="Allow">A</acronym>') . '</strong>',
    '&nbsp;<strong>' . t('<acronym title="Ignore">I</acronym>') . '</strong>',
    '&nbsp;<strong>' . t('<acronym title="Deny">D</acronym>') . '</strong>',
    '&nbsp;',
  );
  $sub_header = array_merge(array('&nbsp;'), $sub_header, $sub_header, $sub_header);
  $sub_header = array_pad($sub_header, 15, '&nbsp;');
  $node_grant_types = array('view', 'update', 'delete');

  $radios = array('1' => t('Allow'), '0' => t('Ignore'), '2' => t('Deny'));

  drupal_set_title(t('Grants for %role', array('%role' => $roles[$form['rid']['#value']])), PASS_THROUGH);

  $rows = array();

  foreach (array_keys($form['vocabs']) as $vid) {
    if (is_numeric($vid) AND isset($form['grants'][$vid])) {
      $row = $sub_header;
      $row[0] = array('data' => '<h3>' . check_plain($form['vocabs'][$vid]['#title']) . '</h3>', 'colspan' => 3);
      $rows[] = $row;
      foreach (array_keys($form['grants'][$vid]) as $tid) {
        if (is_numeric($tid)) {
          $select_key = $tid? 'selected_terms' : 'selected_defaults';
          $select_id = $tid? $tid : $vid;
          $row = array(
            array('data' => drupal_render($form[$select_key][$select_id]), 'colspan' => 3),
          );
          foreach ($node_grant_types as $grant) {
            foreach (array_keys($radios) as $key) {
              // I need this hack to display radio buttons horizontally (instead of standard form 'radios')
              $row[] = array('data' => drupal_render($form['grants'][$vid][$tid][$grant][$key]));
            }
            $row[] = '&nbsp;';
          }
          foreach (array('create', 'list') as $grant) {
            $row[] = array('data' => drupal_render($form['grants'][$vid][$tid][$grant]));
          }
          $rows[] = $row;
        }
      }
    }
  }
  if (isset($form['new'])) {
    $row = $sub_header;
    $row[0] = array('data' => '<h3>' . t('New') . '</h3>', 'colspan' => 3);
    $rows[] = $row;

    $row = array(
      array(
        'data' => drupal_render($form['new']['item'])
        . drupal_render($form['new']['recursive']),
        'colspan' => '2',
      ),
      drupal_render($form['new']['add']),
    );
    foreach ($node_grant_types as $grant) {
      foreach (array_keys($radios) as $key) {
        // I need this hack to display radio buttons horizontally (instead of standard form 'radios')
        $row[] = array('data' => drupal_render($form['new']['grants'][$grant][$key]));
      }
      $row[] = '&nbsp;';
    }
    foreach (array('create', 'list') as $grant) {
      $row[] = array('data' => drupal_render($form['new']['grants'][$grant]));
    }
    $rows[] = $row;
    $row = array();
  }


  $output = '';
  $output .= theme('table', array('header' => $header, 'rows' => $rows));
  $output .= drupal_render_children($form);

  return $output;
}

/**
 * Submit handler for the administration form at TAXONOMY_ACCESS_CONFIG.
 *
 * @todo
 *     Refactor 'Save all' case to reduce queries?
 */
function taxonomy_access_admin_form_submit($form, &$form_state) {
  $values = $form_state['values'];

  switch ($values['op']) {

    case t('Delete selected'):
      if (isset($values['selected_terms']) && is_array($values['selected_terms'])) {
        $disable_tids = array();
        foreach ($values['selected_terms'] as $tid => $enabled) {
          if ($enabled) {
            $disable_tids[] = $tid;
          }
        }

        if (!empty($disable_tids)) {
          taxonomy_access_delete_term_grants($disable_tids, $values['rid']);
          unset($disable_tids);
        }
      }

      if (isset($values['selected_defaults']) && is_array($values['selected_defaults'])) {
        $disable_vids = array();
        foreach ($values['selected_defaults'] as $vid => $enabled) {
          if ($enabled) {
            $disable_vids[] = $vid;
          }
        }

        if (!empty($disable_vids)) {
          taxonomy_access_delete_default_grants($disable_vids, $values['rid']);
          unset($disable_tids);
        }
      }

      break;

    case t('Add'):
      $new = $values['new'];
      list($type, $id) = explode(' ', $new['item']);
      if ($type == 'term') {
        if ($new['recursive'] == 1) {
          taxonomy_access_set_recursive_grants($id, $values['rid'], $new['grants']);
        }
        else {
          taxonomy_access_set_term_grants($id, $values['rid'], $new['grants']);
        }
      }
      elseif ($type == 'default') {
        taxonomy_access_set_default_grants($id, $values['rid'], $new['grants']);
      }

      break;

    case t('Save all'):
      foreach ($values['grants'] as $vid => $rows) {
        foreach ($rows as $tid => $grants) {

          // Check the default values for this row.
          $defaults = array();
          foreach ($grants as $grant_name => $value) {
            $defaults[$grant_name] =
              $form['grants'][$vid][$tid][$grant_name]['#default_value'];
          }

          // Proceed if the user changed the row (values differ from defaults).
          if ($defaults != $grants) {

            // If the grants for node access match the defaults, then we
            // can skip updating node access records for this row.
            $skip_nodes = TRUE;
            foreach (array('view', 'update', 'delete') as $op) {
              if ($defaults[$op] != $grants[$op]) {
                $skip_nodes = FALSE;
              }
            }
            if ($tid == 0) {
              taxonomy_access_set_default_grants($vid, $values['rid'], $grants, $skip_nodes);
            }
            else {
              taxonomy_access_set_term_grants($tid, $values['rid'], $grants, $skip_nodes);
            }
          }
        }
      }

      $form_state['redirect'] = TAXONOMY_ACCESS_CONFIG;
      break;
  }
}

/**
 * Generates HTML markup with form instructions for the admin form.
 *
 * @return
 *     Instructions HTML string.
 */
function _taxonomy_access_admin_instructions_html() {
  $instructions = '';
  $instructions .= ''
    . "<br /><br />"
    . "<div class=\"instructions\">"
    . "<h2>" . t("Explanation of fields") . "</h2>"
    . _taxonomy_access_grant_help_table()
    . "<p>"
    . t('Options for View, Update, and Delete are <em>Allow</em> (<acronym title="Allow">A</acronym>), <em>Ignore</em> (<acronym title="Ignore">I</acronym>), and <em>Deny</em> (<acronym title="Deny">D</acronym>).')
    . "</p>\n\n"
    . "<ul>\n"
    . "<li>"
    . t('<em>Deny</em> (<acronym title="Deny">D</acronym>) overrides <em>Allow</em> (<acronym title="Allow">A</acronym>) within this role.')
    . "</li>"
    . "<li>"
    . t('Both <em>Allow</em> (<acronym title="Allow">A</acronym>) and <em>Deny</em> (<acronym title="Deny">D</acronym>) override <em>Ignore</em> (<acronym title="Ignore">I</acronym>) within this role.')
    . "</li>"
    . "<li>"
    . t('If a user has <strong>multiple roles</strong>, an <em>Allow</em> (<acronym title="Allow">A</acronym>) from another role <strong>will</strong> override a <em>Deny</em> (<acronym title="Deny">D</acronym>) here.')
    . "</li>"
    . "</ul>\n\n"
    ;
  if (arg(4) != DRUPAL_ANONYMOUS_RID && arg(4) != DRUPAL_AUTHENTICATED_RID) {
    // Role other than Anonymous or Authenticated
    $instructions .= ''
      . "<p>"
      . t('<strong>Remember:</strong> This role <strong>will</strong> inherit permissions from the <em>authenticated user</em> role.  Be sure to <a href="@url">configure the authenticated user</a> properly.',
        array("@url" => url(
            TAXONOMY_ACCESS_CONFIG
            . "/edit/"
            .  DRUPAL_AUTHENTICATED_RID)))
      . "</p>\n\n"
      ;
  }
  $instructions .= ''
    . "<p>"
    . t('For more information and for troubleshooting guidelines, see the <a href="@help">help page</a> and the !readme.',
      array(
        '@help' => url('admin/help/taxonomy_access'),
        '!readme' => "<code>README.txt</code>"
      ))
    . "</p>\n\n"
    . "</div>\n\n"
    ;

  return $instructions;

}
