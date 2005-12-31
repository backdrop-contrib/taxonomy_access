This module allows you to set access permissions for various categories based on user role.  

This works as a hook into node_access and follows the same general structure as the node_access module.  It's got permissions for view, update and delete for each category on the system.  

There are two ways you can check whether or not a user has access for a given taxonomy.  First, you can call node_access on a given node and if taxonomy_access is enabled it will take category permissions into account when determining whether or not the user has access to the given node.  The second way is to call the function taxonomy_access which takes two parameters, an operation and a taxonomy, with a third optional paramater of a userid to use instead of the current user.  It returns whether or not the user is authorized, almost identical to node_access.

  (PLEASE NOTE: For HEAD version, there is no need to patch the taxonomy.module anymore.)

TUTORIAL for Category Permissions page
('/admin/access/category')

  VIEW enables user to be able to access content (node) with given term.
     It also enables viewing the term name as well as accessing the node listings for the given term on pages e.g.:"taxonomy/term/", etc.
  UPDATE, DELETE enables user to 'Update' or 'Delete' nodes with that category.
  CREATE enables user to set that category when adding node on "create content" page. 

  In case of multicategory nodes, there is an OR operation btw TAC permissions.