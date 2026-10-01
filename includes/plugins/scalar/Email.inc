<?php


namespace ai_field_automator\includes\plugins\scalar;

use ai_field_automator\includes\AIFieldAutomatorBase;

/**
 * @file
 * Automator plugin for email fields.
 */
class AIFieldAutomatorEmail extends AIFieldAutomatorBase {

  /**
   * {@inheritdoc}
   */
  public function helpText() {
    return t(
      'Generates an email address and stores the first valid email found.'
    );
  }

  /**
   * {@inheritdoc}
   */
  public function allowedFieldTypes() {
    return ['email'];
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
      $prompt .= "\n\nDo not include explanations. Return RFC8259 JSON only in this exact format: [{\"value\":\"plain email address\"}]";
      $text = $this->callAI($prompt, $config);
      if ($text === '') {
        continue;
      }

      $rows = $this->decodeJsonPayload($text);
      if (empty($rows)) {
        $rows = [['value' => $text]];
      }
      foreach ($rows as $row) {
        $candidate = (string) ($row['value'] ?? '');
        if (preg_match(
          '/[A-Z0-9._%+\-]+@[A-Z0-9.\-]+\.[A-Z]{2,}/i',
          $candidate,
          $m
        )
        ) {
          $email = strtolower($m[0]);
          if (filter_var($email, FILTER_VALIDATE_EMAIL)) {
            $values[] = $email;
          }
        }
      }
    }
    return $values;
  }

  /**
   * {@inheritdoc}
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
    $field_info = field_info_field($field_name) ?: [];
    $column = !empty($field_info['columns']) ? array_keys($field_info['columns'])[0] : 'email';
    $items = [];
    foreach ($values as $delta => $value) {
      $items[$delta][$column] = $value;
    }
    $entity->{$field_name}[LANGUAGE_NONE] = $items;
  }

}
