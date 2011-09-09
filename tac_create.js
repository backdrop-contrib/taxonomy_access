/**
 * Disable disallowed terms in taxonomy fields.
 *
 * @see http://jsdemystified.drupalgardens.com/
 */
Drupal.behaviors.tac_create = {};
Drupal.behaviors.tac_create.attach = function(context, settings) {
  var $ = jQuery;
  var $fields = $(Drupal.settings.taxonomy_access);

  $.each($fields, function(i, field) {
    var fieldname = "." + field.field;
    $.each(field.disallowed_tids, function(j, tid) {
      selector = fieldname + " [value='" + tid + "']";
      label_selector = fieldname + " [value='" + tid + "']" + " + label";
      $(selector).attr('disabled','disabled');
      $(label_selector).attr('class','option disabled');
    });
  });
}
