<?php


namespace ai_field_automator\includes\plugins\text;

use ai_field_automator\includes\AIFieldAutomatorBase;

/**
 * @file
 * Automator plugin for long text / body / text_with_summary fields.
 */
class AIFieldAutomatorLongText extends AIFieldAutomatorBase {

  /**
   * {@inheritdoc}
   */
  public function helpText() {
    return t(
      'Generates a longer AI response and stores it in a text_long or body field. HTML is preserved if the field uses a text format.'
    );
  }

  /**
   * {@inheritdoc}
   */
  public function allowedFieldTypes() {
    return ['text_long', 'text_with_summary'];
  }

  /**
   * {@inheritdoc}
   */
  public function generate($entity, $field_name, array $config) {
    $prompts = $this->buildPrompts($entity, $config);
    if (empty($prompts)) {
      return [];
    }

    $values = [];
    foreach ($prompts as $prompt) {
      $text = $this->callAI($prompt, $config);
      if ($text !== '') {
        $values[] = $text;
      }
    }
    return $values;
  }

  /**
   * {@inheritdoc}
   *
   * For text_with_summary fields we also populate the summary if it is empty.
   */
  public function storeValues(
    $entity,
    array $values,
    $field_name,
    array $config
  ) {
    if (empty($values)) {
      return;
    }
    $lang = LANGUAGE_NONE;
    $field_info = field_info_field($field_name);
    $is_summary_field = (!empty($field_info['type']) && $field_info['type'] === 'text_with_summary');

    $items = [];
    foreach ($values as $delta => $value) {
      $items[$delta]['value'] = $value;
      $format = !empty($config['text_format']) ? $config['text_format']
        : filter_default_format();
      $items[$delta]['format'] = $format;
      if ($is_summary_field) {
        // Auto-generate summary: first 500 characters stripped of tags.
        $items[$delta]['summary'] = truncate_utf8(
          strip_tags($value),
          500,
          TRUE,
          TRUE
        );
      }
    }

    $entity->{$field_name}[$lang] = $items;
  }

}
