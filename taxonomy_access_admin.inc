<?php
// $Id$

/**
 * @file
 * Administrative interface for taxonomy access control.
 */

/**
 * Cache roles
 */
function _user_roles() {
  static $roles;
  if (!is_array($roles)) {
    $roles = user_roles();
  }
  return $roles;
}

/**
 * Menu callback; presents the category permissions page of TAC (admin/user/taxonomy_access).
 */
function taxonomy_access_admin($op = NULL, $rid=NULL, $arg=NULL) {
  $roles = _user_roles();
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
  //TODO something odd happens here
  else return drupal_not_found();
}

/**
 * Renders the main page of category permissions
 */
function theme_taxonomy_access_admin() {
  $roles = _user_roles();

  // Render role/permission overview:
  $header = array(t('Role'), array('data' => '&nbsp;'));
  $rows = array();
  
  $result = db_query('SELECT rid FROM {term_access_defaults} WHERE vid=0 ');
  $active = array();
  while ($role = db_fetch_array($result)) {
    $active[$role['rid']] = TRUE;
  }
  foreach ($roles as $rid => $name) {
    $ops = array();
    if ($active[$rid]) {
      //only allow delete for "extra" roles
      if ($rid > 2) {
        $ops[] = l(t("disable"), "admin/user/taxonomy_access/delete/$rid");
      }
      $ops[] = l(t("edit"), "admin/user/taxonomy_access/edit/$rid");
    }
    else {
      $ops = array(l(t("enable"), "admin/user/taxonomy_access/edit/$rid"));
    }
    $rows[] = array($name, array('data' => implode(' | ', $ops), 'align' => 'right'));
  }

  return theme('table', $header, $rows);
}

function taxonomy_access_admin_delete_role($form_state, $rid) {
  if (is_numeric($rid) AND $rid > 2 AND db_fetch_array(db_query('SELECT rid FROM {term_access_defaults} WHERE vid=0 AND rid=%d', $rid))) {
    if ($_POST['confirm']) {
      // issue #167977 - klance
      $affected_nodes = _taxonomy_access_get_nodes_for_role($rid);
      db_query('DELETE FROM {term_access} WHERE rid=%d', $rid);
      db_query('DELETE FROM {term_access_defaults} WHERE rid=%d', $rid);
      // issue #167977 - klance
      //node_access_rebuild();
      _taxonomy_access_node_access_update($affected_nodes);            
      drupal_set_message(t('All term access rules deleted for role %rid.', array('%rid' => $rid)));
      drupal_goto('admin/user/taxonomy_access');
    }
    else {
      return confirm_form($form,
                          t('Are you sure you want to delete all grant rules for role %rid?', array('%rid' => $rid)),
                          'admin/user/taxonomy_access', t('This action cannot be undone.'),
                          t('Delete all'), t('Cancel'));
    }
  }
  else return drupal_not_found();
}

// TODO: clarify list VS create grants
function taxonomy_access_admin_build_row($grants = NULL) {
  $form['#title'] = $title;
  $form['#tree'] = TRUE;
  foreach (array('view', 'update', 'delete') as $grant) {
    $form[$grant] = array(
      '#type' => 'radios', 
      '#options' => array('1' => '', '0' => '', '2' => ''), //1: Allow, 0: Ignore, 2: Deny
      '#default_value' => is_string($grants['grant_'. $grant]) ? $grants['grant_'. $grant] : '0',
      '#required' => TRUE,
     );
  }
  foreach (array('create', 'list') as $grant) {
    $form[$grant] = array(
      '#type' => 'checkbox', 
      '#default_value' => is_string($grants['grant_'. $grant]) ? $grants['grant_'. $grant] : '0',
     );
  }
  return $form;
}

/**
 * Form for managing grants by role.
 */
function taxonomy_access_admin_form($form_state, $rid = NULL) {
  // Fetch all default grants
  $result = db_query('SELECT * FROM {term_access_defaults} WHERE rid = %d', $rid);
  while ($row = db_fetch_array($result)) {
    $default_grants[$row['vid']] = $row;
  }

  // Fetch all grants
  $result = db_query('SELECT * FROM {term_access} WHERE rid = %d', $rid);
  while ($row = db_fetch_array($result)) {
    $grants[$row['tid']] = $row;
  }

  $form['rid'] = array('#type' => 'value', '#value' => $rid);
  $form['grants'] = $form['selected_terms'] = $form['selected_defaults'] = array('#tree' => TRUE);
  
  //Global default
  $form['vocabs'][0]['#title'] = 'Global';
  $form['grants'][0][0] = taxonomy_access_admin_build_row($default_grants[0]);
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
  //New grant row
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
  
  $form['delete'] = array(
    '#type' => 'submit',
    '#value' => t('Delete selected'),
  );

  $form['submit'] = array(
    '#type' => 'submit',
    '#value' => t('Save all'),
  );

  return $form;
}

/**
 * Renders the permission matrix user form for choosen user role.
 */
function theme_taxonomy_access_admin_form($form) {
  $roles = _user_roles();
  $header = array(
    array( 'data' => t('Category'), 'colspan' => 3),
    array( 'data' => t('View'), 'colspan' => 4),
    array( 'data' => t('Update'), 'colspan' => 4),
    array( 'data' => t('Delete'), 'colspan' => 4),
    array( 'data' => t('Create')),
    array( 'data' => t('List')),
  );
  $sub_header = array(
    '&nbsp;<strong>'. t('A') .'</strong>',
    '&nbsp;<strong>'. t('I') .'</strong>',
    '&nbsp;<strong>'. t('D') .'</strong>',
    '&nbsp;',
  );
  $sub_header = array_merge(array('&nbsp;'), $sub_header, $sub_header, $sub_header);
  $sub_header = array_pad($sub_header, 15, '&nbsp;');
  $node_grant_types = array('view', 'update', 'delete');

  $radios = array('1' => t('Allow'), '0' => t('Ignore'), '2' => t('Deny'));

  drupal_set_title(t('Grants for %role', array('%role' => $roles[$form['rid']['#value']])));

  $rows = array();

  foreach (array_keys($form['vocabs']) as $vid) {
    if (is_numeric($vid) AND isset($form['grants'][$vid])) {
      $row = $sub_header;
      $row[0] = array('data' => '<h3>'. check_plain($form['vocabs'][$vid]['#title']) .'</h3>', 'colspan' => 3);
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
    $row[0] = array('data' => '<h3>'. t('New') .'</h3>', 'colspan' => 3);
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

  $output .= theme('table', $header, $rows);
  $output .= drupal_render($form);

  return $output;
}

function taxonomy_access_admin_form_submit($form, &$form_state) {
  $values = $form_state['values'];

  switch ($values['op']) {
  case t('Delete selected'):
    if (is_array($values['selected_terms'])) {
      foreach ($values['selected_terms'] as $tid => $enabled) {
        if ($enabled) {
          // issue #167977 - klance
          $affected_nodes = _taxonomy_access_get_nodes_for_term($tid);
          db_query('DELETE FROM {term_access} WHERE rid = %d AND tid = %d', $values['rid'], $tid);
          // issue #167977 - klance
          _taxonomy_access_node_access_update($affected_nodes);
        }
      }
    }
    if (is_array($values['selected_defaults'])) {
      foreach ($values['selected_defaults'] as $vid => $enabled) {
        if ($enabled) {
          // issue #167977 - klance
          $affected_nodes = _taxonomy_access_get_nodes_for_vocabulary($vid, $values['rid']);
          db_query('DELETE FROM {term_access_defaults} WHERE rid = %d AND vid = %d', $values['rid'], $vid);
          // issue #167977 - klance
          _taxonomy_access_node_access_update($affected_nodes);
        }
      }
    }
    // issue #167977 - klance
    //node_access_rebuild();
    break;
  case t('Add'):
    $new = $values['new'];
    list($type, $id) = explode(' ', $new['item']);
    if ($type == 'term') {
      if ($new['recursive'] == 1) {
		taxonomy_access_recursive_grant_update($id, $values['rid'], $new['grants']);
	  } else {
	    taxonomy_access_grant_update($id, $values['rid'], $new['grants']);
	  }
    }
    elseif ($type == 'default') {
      taxonomy_access_defaults_update($id, $values['rid'], $new['grants']);
    }
    // issue #167977 - klance
    //node_access_rebuild();
    break;
  case t('Save all'):
    foreach ($values['grants'] as $vid => $rows) {
      foreach ($rows as $tid => $grants) {
        if ($tid == 0) {
          taxonomy_access_defaults_update($vid, $values['rid'], $grants);
        }
        else {
          taxonomy_access_grant_update($tid, $values['rid'], $grants);
        }
      }
    }
    // issue #167977 - klance
    //node_access_rebuild();
    drupal_goto('admin/user/taxonomy_access');
  }
}

/**
 * Updates permissions for a role for a term
 * @param $tid
 *   The term to add the permission for.
 * @param $rid
 *   The role id to add the permission for.
 * @param $grants
 *   A hash of the grants in the form of $grants['perm'] = boolean
 *   A value of 1 will grant the permission for this user and term.
**/
function taxonomy_access_grant_update($tid, $rid = NULL, $grants = NULL) {
  if (!isset($tid) OR !is_numeric($rid)) {
    return FALSE;
  }
  
  $ta_sql = "INSERT INTO {term_access} (tid";
  $ta_sql_values = " VALUES ($tid";
  if (isset($rid)) {
    $ta_sql .= ",rid";
    $ta_sql_values .= ",$rid";
  }
  $sql = "";
  if (isset($grants)) {
    foreach ($grants as $perm => $value) {
      $sql .= ",grant_$perm";
      $ta_sql_values .= is_array($value) ? ",". $value[0] : ",$value";
    }
    $sql .= ")";
    $ta_sql_values .= ")";
  }
  else {
    $sql .= ")";
    $ta_sql_values .= ")";
  }
  $ta_sql .= $sql . $ta_sql_values;
  
  // issue #167977 - klance
  $affected_nodes = _taxonomy_access_get_nodes_for_term($tid);
  db_query("DELETE FROM {term_access} WHERE tid=%d AND rid=%d", $tid, ($rid));
  db_query($ta_sql);  // insert into term_access
  // issue #167977 - klance
  _taxonomy_access_node_access_update($affected_nodes);
}

/**
 * Recursively updates permissions for a role for a term
 */
function taxonomy_access_recursive_grant_update($tid, $rid = null, $grants = null) {
  // run the original
  taxonomy_access_grant_update($tid, $rid, $grants);
  // process the children
  $ran_tids = array(); // tids that have been processed
  $run_tids = array($tid); // tids that are in the queue to be processed
  while(count($run_tids) > 0) { // while there are tids to be processed, run the following
	foreach($run_tids as $run_key => $run_tid) {
	  // some basic loop protection
	  if (array_search($run_tid, $ran_tids) == true) {
		drupal_set_message("Loop detected for tid <b>".$run_tid."</b>. Stopping.");
		$run_tids = array(); // stop the execution
	  } else {
		$result = db_query('SELECT th.tid FROM {term_hierarchy} th WHERE th.parent = %d', $run_tid);
		// if this tid has children, update grants and queue the children up for processing
		while ($row = db_fetch_array($result)) {
		  taxonomy_access_grant_update($row['tid'], $rid, $grants);
		  $run_tids[] = $row['tid'];
		}
		unset($run_tids[$run_key]); // remove this tid from queue
		$ran_tids[] = $run_tid; // mark this tid as processed
	  }
	}
  }
}

/**
 * Updates default permissions for a role for a vocabulary
 * @param $vid
 *   The vocab to add the permission for.
 * @param $rid
 *   The role id to add the permission to.
 * @param $grants
 *   A hash of the grants in the form of $grants['perm'] = boolean
 *   A value of 1 will grant the permission for this user and term.
**/
function taxonomy_access_defaults_update($vid, $rid = NULL, $grants = NULL) {
  if (!isset($vid) OR !is_numeric($rid)) {
    return FALSE;
  }
  
  $ta_sql = "INSERT INTO {term_access_defaults} (vid";
  $ta_sql_values = " VALUES ($vid";
  if (isset($rid)) {
    $ta_sql .= ",rid";
    $ta_sql_values .= ",$rid";
  }
  $sql = "";
  if (isset($grants)) {
    foreach ($grants as $perm => $value) {
      $sql .= ",grant_$perm";
      $ta_sql_values .= ",$value";
    }
    $sql .= ")";
    $ta_sql_values .= ")";
  }
  else {
    $sql .= ")";
    $ta_sql_values .= ")";
  }
  $ta_sql .= $sql . $ta_sql_values;

  // issue #167977 - klance
  $affected_nodes = _taxonomy_access_get_nodes_for_vocabulary($vid, $rid);
  db_query("DELETE FROM {term_access_defaults} WHERE vid=%d AND rid=%d", $vid, $rid);
  db_query($ta_sql);  // insert into term_access_defaults
  // issue #167977 - klance
  _taxonomy_access_node_access_update($affected_nodes);
}
