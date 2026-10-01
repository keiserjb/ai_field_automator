<?php


namespace ai_field_automator\includes\plugins\reference;

use ai_field_automator\includes\AIFieldAutomatorBase;
use TaxonomyTerm;

/**
 * @file
 * Automator plugin for taxonomy term reference fields.
 */
class AIFieldAutomatorTaxonomyTerms extends AIFieldAutomatorBase {

  /**
   * {@inheritdoc}
   */
  public function helpText() {
    return t(
      'Classifies content into existing taxonomy terms and can auto-create missing terms.'
    );
  }

  /**
   * {@inheritdoc}
   */
  public function allowedFieldTypes() {
    return ['taxonomy_term_reference'];
  }

  /**
   * {@inheritdoc}
   */
  public function generate($entity, $field_name, array $config) {
    $prompts = $this->buildPrompts($entity, $config);
    if (empty($prompts)) {
      return [];
    }

    $allow_create = !empty($config['taxonomy_auto_create']);
    $term_map = $this->getAllowedTerms($field_name);
    $use_tools = $this->shouldUseToolLookup($term_map, $config);

    // If there are no existing terms and auto-create is disabled, nothing to do.
    if (empty($term_map) && !$allow_create) {
      return [];
    }

    $term_names = array_values($term_map);
    $values = [];
    foreach ($prompts as $prompt) {
      if ($use_tools) {
        $messages = [
          [
            'role' => 'user',
            'content' => $prompt . "\n\nUse the taxonomy lookup tools when needed. Return RFC8259 JSON only in this exact format: [{\"value\":\"term name\"}]",
          ],
        ];
        $text = $this->callAIWithTools($messages, ['get_vocabularies', 'get_taxonomy_terms'], $config);
      }
      else {
        if (!empty($term_names)) {
          $prompt .= "\n\nAvailable terms:\n" . implode("\n", $term_names);
        }
        $prompt .= "\n\nDo not include explanations. Return RFC8259 JSON only in this exact format: [{\"value\":\"term name\"}]";
        $text = $this->callAI($prompt, $config);
      }
      if ($text === '') {
        continue;
      }

      $rows = $this->decodeJsonPayload($text);
      if (empty($rows)) {
        $rows = [['value' => $text]];
      }

      foreach ($rows as $row) {
        $candidate = trim((string) ($row['value'] ?? ''));
        if ($candidate === '') {
          continue;
        }
        $values[] = $this->cleanValue(
          $candidate,
          $config['taxonomy_cleanup'] ?? ''
        );
      }
    }

    $values = array_values(array_unique($values));
    if (!empty($config['taxonomy_search_similar_tags']) && !empty($values)
      && !empty($term_names)
    ) {
      $values = $this->searchSimilarTags($values, $term_names, $config);
    }

    return $values;
  }

  /**
   * Uses tools when the vocabulary is large enough to justify live lookups.
   */
  protected function shouldUseToolLookup(array $term_map, array $config) {
    if (empty($term_map) || !module_exists('ai_tools')) {
      return FALSE;
    }

    if (!empty($config['taxonomy_use_tools'])) {
      return TRUE;
    }

    return count($term_map) > 50;
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

    $term_map = $this->getAllowedTerms($field_name);
    $vids = $this->getAllowedVocabularies($field_name);
    $allow_create = !empty($config['taxonomy_auto_create']);

    $items = [];
    $created = [];
    foreach ($values as $value) {
      $tid = 0;
      if (is_numeric($value)) {
        $tid = (int) $value;
      }
      else {
        $tid = $this->findTidByName($value, $term_map);
      }
      if (!$tid && $allow_create && !empty($vids[0])) {
        $tid = $this->createTerm($value, $vids[0]);
        if ($tid) {
          $term_map[$tid] = $value;
          $created[] = $value;
        }
      }
      if ($tid) {
        $items[] = ['tid' => $tid];
      }
    }

    if (!empty($items)) {
      $entity->{$field_name}[LANGUAGE_NONE] = $items;
    }
  }

  /**
   * Returns [tid => name] for vocabularies allowed by this field.
   */
  protected function getAllowedTerms($field_name) {
    $map = [];
    foreach ($this->getAllowedVocabularies($field_name) as $machine_name) {
      $terms = taxonomy_get_tree($machine_name);
      foreach ($terms as $term) {
        $map[$term->tid] = $term->name;
      }
    }
    return $map;
  }

  /**
   * Returns allowed taxonomy vocabulary machine names for a field.
   */
  protected function getAllowedVocabularies($field_name) {
    $field = field_info_field($field_name);
    $allowed = $field['settings']['allowed_values'] ?? [];
    $out = [];
    foreach ($allowed as $item) {
      if (!empty($item['vocabulary'])) {
        $out[] = $item['vocabulary'];
      }
    }
    return array_values(array_unique($out));
  }

  /**
   * Find term ID by term name (case-insensitive).
   */
  protected function findTidByName($name, array $term_map) {
    foreach ($term_map as $tid => $label) {
      if (mb_strtolower((string) $label) === mb_strtolower((string) $name)) {
        return (int) $tid;
      }
    }
    return 0;
  }

  /**
   * Creates a term in the provided vocabulary machine name.
   */
  protected function createTerm($name, $vocabulary_machine_name) {
    $vocabulary = taxonomy_vocabulary_machine_name_load(
      $vocabulary_machine_name
    );
    if (!$vocabulary) {
      return 0;
    }

    $term = new TaxonomyTerm();
    $term->name = mb_substr($name, 0, 255);
    $term->vocabulary = $vocabulary->machine_name;
    $status = taxonomy_term_save($term);
    return $status === SAVED_NEW || $status === SAVED_UPDATED ? (int) $term->tid
      : 0;
  }

  /**
   * Applies optional value cleanup.
   */
  protected function cleanValue($value, $mode) {
    switch ($mode) {
      case 'lowercase':
        return mb_strtolower($value);

      case 'uppercase':
        return mb_strtoupper($value);

      case 'first_char':
        return ucfirst(mb_strtolower($value));

      default:
        return $value;
    }
  }

  /**
   * Uses an extra AI pass to map near-match tags to existing terms.
   */
  protected function searchSimilarTags(
    array $values,
    array $existing_terms,
    array $config
  ) {
    $prompt
      = "Based on available terms and proposed new terms, map new terms to contextually similar existing terms when appropriate.\n";
    $prompt .= "Do not include explanations. Return RFC8259 JSON only in this exact format: [{\"available_term\":\"existing term\",\"new_term\":\"new term\"}]\n\n";
    $prompt .= "Available terms:\n" . implode("\n", $existing_terms) . "\n\n";
    $prompt .= "New terms:\n" . implode("\n", $values) . "\n";

    $text = $this->callAI($prompt, $config);
    if ($text === '') {
      return $values;
    }
    $rows = $this->decodeJsonPayload($text);
    if (empty($rows)) {
      return $values;
    }

    foreach ($rows as $row) {
      $available = trim((string) ($row['available_term'] ?? ''));
      $new = trim((string) ($row['new_term'] ?? ''));
      if ($available === '' || $new === '' || $available === $new) {
        continue;
      }
      foreach ($values as $k => $value) {
        if ($value === $new) {
          $values[$k] = $available;
        }
      }
    }

    return array_values(array_unique($values));
  }

}
