<?php


namespace ai_field_automator\includes\plugins\scalar;

use ai_field_automator\includes\AIFieldAutomatorBase;

/**
 * @file
 * Automator plugin for telephone fields.
 */
class AIFieldAutomatorTelephone extends AIFieldAutomatorBase {

  /**
   * {@inheritdoc}
   */
  public function helpText() {
    return t('Generates a phone number and stores normalized telephone text.');
  }

  /**
   * {@inheritdoc}
   */
  public function allowedFieldTypes() {
    return ['telephone', 'phone'];
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
      $prompt .= "\n\nDo not include explanations. Return RFC8259 JSON only in this exact format: [{\"value\":\"plain telephone number with plus and country code\"}]";
      $text = $this->callAI($prompt, $config);
      if ($text === '') {
        continue;
      }

      $rows = $this->decodeJsonPayload($text);
      if (empty($rows)) {
        $rows = [['value' => $text]];
      }
      foreach ($rows as $row) {
        $candidate = trim((string) ($row['value'] ?? ''));
        if (preg_match('/^\+\(?[0-9]{1,3}\)?[ ]?[0-9]{6,12}$/', $candidate)) {
          $values[] = $candidate;
        }
      }
    }
    return $values;
  }

}
