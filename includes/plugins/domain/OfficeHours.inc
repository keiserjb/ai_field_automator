<?php


namespace ai_field_automator\includes\plugins\domain;

use ai_field_automator\includes\AIFieldAutomatorBase;

/**
 * Automator for office-hours fields.
 */
class AIFieldAutomatorOfficeHours extends AIFieldAutomatorBase {

  /**
   * {@inheritdoc}
   */
  public function helpText() {
    return t('Extracts office/opening hours from text.');
  }

  /**
   * {@inheritdoc}
   */
  public function allowedFieldTypes() {
    return ['office_hours', 'officehours'];
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
      $prompt .= "\n\nReturn RFC8259 JSON only in this format: [{\"value\":{\"day\":\"1\",\"starthours\":\"HHMM\",\"endhours\":\"HHMM\"}}]";
      $prompt .= "\nUse Monday=1 through Sunday=7 for the day value.";
      $prompt .= "\nOnly include days that are open.";
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
        $candidate = isset($row['value']) ? $row['value'] : $row;
        if (!is_array($candidate)) {
          continue;
        }
        if (empty($candidate['day']) || empty($candidate['starthours'])
          || empty($candidate['endhours'])
        ) {
          continue;
        }
        $day = (int) preg_replace('/[^0-9]/', '', (string) $candidate['day']);
        if ($day < 1 || $day > 7) {
          continue;
        }
        $values[] = [
          'day' => (string) $day,
          'starthours' => preg_replace(
            '/[^0-9]/',
            '',
            (string) $candidate['starthours']
          ),
          'endhours' => preg_replace(
            '/[^0-9]/',
            '',
            (string) $candidate['endhours']
          ),
        ];
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
    $columns = !empty($field_info['columns']) ? array_keys($field_info['columns']) : ['day', 'starthours', 'endhours'];
    $items = [];
    foreach ($values as $value) {
      $item = [];
      $item[$this->findColumn($columns, ['day', 'weekday', 'dow'], 0)]
        = (int) ($value['day'] ?? 0);
      $item[$this->findColumn($columns, ['starthours', 'start', 'open'], 1)]
        = (int) ($value['starthours'] ?? 0);
      $item[$this->findColumn($columns, ['endhours', 'end', 'close'], 2)]
        = (int) ($value['endhours'] ?? 0);
      $items[] = $item;
    }

    $entity->{$field_name}[LANGUAGE_NONE] = $items;
  }

  /**
   * Finds first matching column by keyword.
   */
  protected function findColumn(
    array $columns,
    array $keywords,
    $fallback_index = 0
  ) {
    foreach ($columns as $column) {
      $norm = preg_replace('/[^a-z0-9]/', '', strtolower($column));
      foreach ($keywords as $keyword) {
        if (strpos($norm, preg_replace('/[^a-z0-9]/', '', strtolower($keyword)))
          !== FALSE
        ) {
          return $column;
        }
      }
    }
    return isset($columns[$fallback_index]) ? $columns[$fallback_index]
      : $columns[0];
  }

  /**
   * Returns TRUE if array is associative.
   */
  protected function isAssoc(array $array) {
    return array_keys($array) !== range(0, count($array) - 1);
  }

}
