<?php


namespace ai_field_automator\includes\plugins\structured;

use ai_field_automator\includes\AIFieldAutomatorBase;

/**
 * Generates structured metatag JSON values.
 */
class AIFieldAutomatorMetatag extends AIFieldAutomatorBase {

  /**
   * {@inheritdoc}
   */
  public function helpText() {
    return t('Generates metatag values as JSON for metatag fields.');
  }

  /**
   * {@inheritdoc}
   */
  public function allowedFieldTypes() {
    return ['metatag'];
  }

  /**
   * {@inheritdoc}
   */
  public function generate($entity, $field_name, array $config) {
    $prompts = $this->buildPrompts($entity, $config);
    if (empty($prompts)) {
      return [];
    }

    $keys = $this->getRequestedKeys($config);
    $template = array_fill_keys($keys, '');

    $values = [];
    foreach ($prompts as $prompt) {
      $prompt .= "\n\nReturn RFC8259 JSON only in this format: [{\"value\":"
        . json_encode($template) . "}]";
      $text = $this->callAI($prompt, $config);
      if ($text === '') {
        continue;
      }
      $rows = $this->decodeJsonPayload($text);
      if (empty($rows)) {
        continue;
      }
      if ($this->isAssoc($rows)) {
        $rows = [$rows];
      }
      foreach ($rows as $row) {
        $candidate = isset($row['value']) && is_array($row['value'])
          ? $row['value'] : $row;
        if (!is_array($candidate)) {
          continue;
        }
        $filtered = [];
        foreach ($keys as $key) {
          if (isset($candidate[$key])
            && trim((string) $candidate[$key]) !== ''
          ) {
            $filtered[$key] = trim((string) $candidate[$key]);
          }
        }
        if (!empty($filtered)) {
          $values[] = json_encode(
            $filtered,
            JSON_UNESCAPED_UNICODE | JSON_UNESCAPED_SLASHES
          );
        }
      }
    }

    return $values;
  }

  /**
   * {@inheritdoc}
   */
  public function checkIfEmpty(array $value, array $config = []) {
    if (empty($value[0]['value'])) {
      return [];
    }

    $existing = json_decode($value[0]['value'], TRUE);
    if (!is_array($existing)) {
      return [];
    }

    foreach ($this->getRequestedKeys($config) as $key) {
      if (empty($existing[$key])) {
        return [];
      }
    }

    return $value;
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
    if (empty($values[0])) {
      return;
    }
    $field_info = field_info_field($field_name) ?: [];
    $column = !empty($field_info) ? $this->getPrimaryColumn($field_info) : 'value';
    $val = is_array($values[0]) ? json_encode($values[0]) : (string) $values[0];
    $entity->{$field_name}[LANGUAGE_NONE][0][$column] = $val;
  }

  /**
   * Returns metatag keys to generate.
   */
  protected function getRequestedKeys(array $config) {
    $raw = $config['metatag_keys'] ?? [];
    if (!is_array($raw)) {
      $raw = preg_split(
        '/\s*,\s*/',
        trim((string) $raw),
        -1,
        PREG_SPLIT_NO_EMPTY
      );
    }
    $keys = array_values(array_unique(array_filter(array_map('trim', $raw))));
    if (empty($keys)) {
      $keys = ['title', 'description', 'og:title', 'og:description'];
    }
    return $keys;
  }

  /**
   * Returns TRUE if array is associative.
   */
  protected function isAssoc(array $array) {
    return array_keys($array) !== range(0, count($array) - 1);
  }

  /**
   * {@inheritdoc}
   */
  public function verifyValue($entity, $value, $field_name, array $config) {
    if (!is_scalar($value) || trim((string) $value) === '') {
      return FALSE;
    }
    $decoded = json_decode((string) $value, TRUE);
    return is_array($decoded) && !empty($decoded);
  }

}
