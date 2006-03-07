This module allows you to set access permissions for various categories based
on user role.  

This works as a hook into node_access and follows the same general structure
as the node_access module.  It's got permissions for view, update and delete
for each category on the system.  

API functions: There are two ways you can check whether or not a user has
access for a given taxonomy.  First, you can call node_access on a given node
and if taxonomy_access is enabled it will take category permissions into
account when determining whether or not the user has access to the given node.
The second way is to call the function taxonomy_access which takes two
parameters, an operation and a taxonomy, with a third optional paramater of a
userid to use instead of the current user.  It returns whether or not the user
is authorized, almost identical to node_access.

DATABASE TABLES: Module creates two tables in database: 'term_access' and
'term_access_defaults'

(PLEASE NOTE: For HEAD version, there is no need to patch the taxonomy.module.)

For more information about how to control access permissions with the Taxonomy
Access Control module, see the module's help page (admin/help/taxonomy_access).

Please, send bug reports, feature requests, or other comments about Taxonomy
Access Control module: http://drupal.org/project/issues/taxonomy_access

You can also contact me personally:
Keve  ( http://drupal.org/user/13163/contact )
