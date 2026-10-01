<?php


namespace ai_field_automator\includes\plugins\domain;

use ai_field_automator\includes\AIFieldAutomatorBase;

/**
 * Automator for chart-config style fields.
 */
class AIFieldAutomatorChartFromText extends AIFieldAutomatorBase {

  /**
   * {@inheritdoc}
   */
  public function helpText() {
    return t('Converts text context into chart-ready tabular CSV data.');
  }

  /**
   * {@inheritdoc}
   */
  public function allowedFieldTypes() {
    return ['chart_config'];
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
      $prompt .= "\n\nReturn CSV only, no explanations and no markdown.";
      $prompt .= "\nUse ';' as delimiter. First row must be headers, following rows data.";
      $text = $this->callAI($prompt, $config);
      if (!is_string($text) || trim($text) === '') {
        continue;
      }
      $clean = trim(str_replace(['```csv', '```'], '', $text));
      if ($clean !== '') {
        $values[] = $clean;
      }
    }

    return $values;
  }

  /**
   * {@inheritdoc}
   */
  public function checkIfEmpty(array $value, array $config = []) {
    if (empty($value[0]['config']['series']['data_collector_table'][0][0]['data'])) {
      return [];
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
    $columns = !empty($field_info['columns']) ? array_keys($field_info['columns']) : ['value'];
    $primary = !empty($field_info) ? $this->getPrimaryColumn($field_info) : 'value';
    $csv = (string) $values[0];

    // Generic fallback: store raw CSV in primary column.
    $item = [$primary => $csv];

    // Chart-config-style storage: build data collector table when possible.
    if (in_array('config', $columns, TRUE)) {
      $table = $this->csvToTable($csv);
      if (!empty($table)) {
        $item = [
          'config' => [
            'series' => [
              'data_collector_table' => $table,
            ],
          ],
        ];
      }
    }

    $entity->{$field_name}[LANGUAGE_NONE][0] = $item;
  }

  /**
   * Converts CSV text to chart data_collector_table structure.
   */
  protected function csvToTable($csv) {
    $rows = preg_split('/\r\n|\r|\n/', trim($csv));
    if (empty($rows)) {
      return [];
    }

    $table = [];
    foreach ($rows as $row_index => $line) {
      $line = trim($line);
      if ($line === '') {
        continue;
      }
      $delimiter = (substr_count($line, ';') >= substr_count($line, ',')) ? ';'
        : ',';
      $cells = str_getcsv($line, $delimiter);
      foreach ($cells as $cell_index => $cell) {
        $table[$row_index][$cell_index]['data'] = trim(
          $cell,
          " \t\n\r\0\x0B\""
        );
      }
    }

    return !empty($table) ? array_values($table) : [];
  }

  /**
   * {@inheritdoc}
   */
  public function verifyValue($entity, $value, $field_name, array $config) {
    if (!is_scalar($value) || trim((string) $value) === '') {
      return FALSE;
    }
    $rows = preg_split('/\r\n|\r|\n/', trim((string) $value));
    return count(array_filter($rows, 'strlen')) >= 2;
  }

}
