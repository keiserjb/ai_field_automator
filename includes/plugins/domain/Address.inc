<?php


namespace ai_field_automator\includes\plugins\domain;

use ai_field_automator\includes\AIFieldAutomatorBase;

/**
 * Automator for address-style fields.
 */
class AIFieldAutomatorAddress extends AIFieldAutomatorBase {

  /**
   * {@inheritdoc}
   */
  public function helpText() {
    return t('Extracts structured address components from context.');
  }

  /**
   * {@inheritdoc}
   */
  public function allowedFieldTypes() {
    return ['address', 'addressfield'];
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
      $prompt .= "\n\nReturn RFC8259 JSON only with this exact shape: ";
      $prompt .= "[{\"value\":{\"country_code\":\"2-letter ISO code\",\"administrative_area\":\"state/region\",\"locality\":\"city\",\"postal_code\":\"postal code\",\"address_line1\":\"street line 1\",\"address_line2\":\"street line 2\",\"organization\":\"organization\",\"given_name\":\"first name\",\"family_name\":\"last name\"}}]";
      $prompt .= "\nOnly include values that are present in the context.";

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
        if (empty($candidate['country_code'])
          && empty($candidate['address_line1'])
          && empty($candidate['locality'])
        ) {
          continue;
        }
        $values[] = $candidate;
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
    $columns = !empty($field_info['columns']) ? array_keys($field_info['columns']) : [
      'country',
      'thoroughfare',
      'premise',
      'locality',
      'administrative_area',
      'postal_code',
      'name_line',
      'first_name',
      'last_name',
    ];

    $items = [];
    foreach ($values as $value) {
      if (!is_array($value)) {
        continue;
      }
      $item = [];
      foreach ($columns as $column) {
        $mapped = $this->mapColumnValue($column, $value);
        if ($mapped !== '') {
          $item[$column] = $mapped;
        }
      }
      if (!empty($item)) {
        $items[] = $item;
      }
    }

    if (!empty($items)) {
      $entity->{$field_name}[LANGUAGE_NONE] = $items;
    }
  }

  /**
   * Maps AI output keys to address column names.
   */
  protected function mapColumnValue($column, array $value) {
    if (isset($value[$column]) && is_scalar($value[$column])) {
      return trim((string) $value[$column]);
    }

    $map = [
      'country' => 'country_code',
      'thoroughfare' => 'address_line1',
      'premise' => 'address_line2',
      'sub_premise' => 'address_line2',
      'name_line' => 'organization',
      'first_name' => 'given_name',
      'last_name' => 'family_name',
    ];

    if (isset($map[$column]) && isset($value[$map[$column]])
      && is_scalar(
        $value[$map[$column]]
      )
    ) {
      return trim((string) $value[$map[$column]]);
    }

    if ($column === 'name_line') {
      $name = trim(
        (string) (($value['given_name'] ?? '') . ' ' . ($value['family_name'] ??
            ''))
      );
      return $name;
    }

    $column_norm = preg_replace('/[^a-z0-9]/', '', strtolower($column));
    foreach ($value as $key => $candidate) {
      if (!is_scalar($candidate)) {
        continue;
      }
      $key_norm = preg_replace('/[^a-z0-9]/', '', strtolower((string) $key));
      if ($key_norm === $column_norm) {
        return trim((string) $candidate);
      }
    }

    return '';
  }

  /**
   * Returns TRUE if array is associative.
   */
  protected function isAssoc(array $array) {
    return array_keys($array) !== range(0, count($array) - 1);
  }

}

