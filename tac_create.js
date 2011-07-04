// Using the closure to map jQuery to $. 
(function ($) {
  // Store our function as a property of Drupal.behaviors.
  Drupal.behaviors.tac_create = {
    attach: function(context, settings) {
      var $fields = $(Drupal.settings.taxonomy_access);
      
      $.each($fields, function(i, field) {
          var fieldname = "." + field.field;
          $.each(field.dis, function(j, tid) {
              selector = fieldname + " [value='" + tid + "']";
              $(selector).attr('disabled','disabled');
            });
        });
    }
  }
}(jQuery));