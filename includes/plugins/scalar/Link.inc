<?php


namespace ai_field_automator\includes\plugins\scalar;

use ai_field_automator\includes\AIFieldAutomatorBase;

/**
 * @file
 * Automator plugin for link/url fields.
 */
class AIFieldAutomatorLink extends AIFieldAutomatorBase {

  /**
   * {@inheritdoc}
   */
  public function helpText() {
    return t('Extracts URLs (and optional link text) from context.');
  }

  /**
   * {@inheritdoc}
   */
  public function allowedFieldTypes() {
    return ['link_field', 'link', 'url'];
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
      $prompt .= "\n\nDo not include explanations. Return RFC8259 JSON only in this exact format: [{\"value\":{\"uri\":\"The raw url\",\"title\":\"The link text if available\"}}]";
      $text = $this->callAI($prompt, $config);
      if ($text === '') {
        continue;
      }

      $rows = $this->decodeJsonPayload($text);
      if (empty($rows) && preg_match('/https?:\/\/[^\s"<>\)\]]+/i', $text, $m)) {
        $rows = [['value' => ['uri' => $m[0], 'title' => '']]];
      }

      foreach ($rows as $row) {
        $value = $row['value'] ?? $row;
        if (!is_array($value)) {
          continue;
        }
        $uri = trim((string) ($value['uri'] ?? ''));
        if (!filter_var($uri, FILTER_VALIDATE_URL)) {
          continue;
        }
        $values[] = [
          'uri' => $uri,
          'title' => trim((string) ($value['title'] ?? '')),
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
    $columns = array_keys($field_info['columns'] ?? []);
    $uri_key = in_array('url', $columns, TRUE) || empty($columns)
      ? 'url'
      : (in_array('uri', $columns, TRUE)
        ? 'uri'
        : (in_array('value', $columns, TRUE) ? 'value'
          : ($columns[0] ?? 'url')));
    $title_key = in_array('title', $columns, TRUE) || empty($columns) ? 'title' : NULL;

    $items = [];
    foreach ($values as $delta => $value) {
      $items[$delta][$uri_key] = $value['url'] ?? $value['uri'] ?? '';
      if ($title_key !== NULL) {
        $items[$delta][$title_key] = $value['title'] ?? '';
      }
    }
    $entity->{$field_name}[LANGUAGE_NONE] = $items;
  }

}

