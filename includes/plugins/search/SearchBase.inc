<?php


namespace ai_field_automator\includes\plugins\search;

use ai_field_automator\includes\AIFieldAutomatorBase;

/**
 * @file
 * Shared helpers for Search API-backed automator plugins.
 */
abstract class AIFieldAutomatorSearchBase extends AIFieldAutomatorBase {

  /**
   * {@inheritdoc}
   */
  public function ruleIsAllowed($entity, $field_name, array $config) {
    if (!module_exists('search_api') || !function_exists('search_api_query')
      || !function_exists('search_api_index_load')
    ) {
      return FALSE;
    }
    if (empty($config['search_index'])) {
      return FALSE;
    }
    $index = search_api_index_load($config['search_index']);
    return !empty($index) && !empty($index->enabled);
  }

  /**
   * Executes a Search API query for a single prompt string.
   *
   * @param string $query_text
   *   Prompt/context text.
   * @param array  $config
   *   Automator config.
   *
   * @return array
   *   Array with keys "index" and "results", or empty array on failure.
   */
  protected function runSearchQuery($query_text, array $config) {
    $query_text = trim((string) $query_text);
    if ($query_text === '' || empty($config['search_index'])) {
      return [];
    }

    $index_id = $config['search_index'];
    $index = search_api_index_load($index_id);
    if (!$index || empty($index->enabled)) {
      return [];
    }

    $offset = max(0, (int) ($config['search_offset'] ?? 0));
    $limit = max(1, (int) ($config['search_max_results'] ?? 10));

    try {
      $query = search_api_query($index_id, [
        'parse mode' => 'single',
        // Search API AI backends (Milvus/Pinecone) read this option.
        'top_k'      => $offset + $limit,
      ]);
      $query->keys($query_text);
      $query->range($offset, $limit);
      $response = $query->execute();
      $results = !empty($response['results']) && is_array($response['results'])
        ? $response['results'] : [];

      return [
        'index' => $index,
        'results' => $results,
      ];
    }
    catch (\Exception $e) {
      watchdog(
        'ai_field_automator',
        'Search query failed on index %index: @msg',
        [
          '%index' => $index_id,
          '@msg'   => $e->getMessage(),
        ],
        WATCHDOG_WARNING
      );
      return [];
    }
  }

  /**
   * Applies configured minimum score filtering.
   */
  protected function passesMinimumScore(array $result, array $config) {
    if (empty($config['search_minimum_score'])) {
      return TRUE;
    }
    $score = (float) ($result['score'] ?? 0);
    return $score > (float) $config['search_minimum_score'];
  }

  /**
   * Normalizes a result entity value into an object.
   */
  protected function normalizeResultEntity($entity) {
    if (is_array($entity)) {
      $entity = reset($entity);
    }
    return is_object($entity) ? $entity : NULL;
  }

  /**
   * Flattens scalar/array values into a single text string.
   */
  protected function flattenTextValue($value) {
    if (is_scalar($value)) {
      return trim((string) $value);
    }
    if (!is_array($value)) {
      return '';
    }

    $parts = [];
    foreach ($value as $item) {
      $flat = $this->flattenTextValue($item);
      if ($flat !== '') {
        $parts[] = $flat;
      }
    }
    return trim(implode("\n", $parts));
  }

  /**
   * Parses result IDs like "123" or "node/123".
   */
  protected function parseResultIdentifier($value) {
    $parsed = [
      'entity_type' => '',
      'id'          => 0,
    ];

    $value = trim((string) $value);
    if ($value === '') {
      return $parsed;
    }
    if (ctype_digit($value)) {
      $parsed['id'] = (int) $value;
      return $parsed;
    }
    if (preg_match('/^(?:entity:)?([a-z_]+)[:\/](\d+)$/i', $value, $matches)) {
      $parsed['entity_type'] = $matches[1];
      $parsed['id'] = (int) $matches[2];
    }
    return $parsed;
  }

  /**
   * Best-effort entity ID extraction from a loaded entity object.
   */
  protected function parseEntityIdFromObject($entity, $target_type = '') {
    if (!is_object($entity)) {
      return 0;
    }

    if ($target_type && function_exists('entity_extract_ids')) {
      try {
        [$id] = entity_extract_ids($target_type, $entity);
        if (!empty($id)) {
          return (int) $id;
        }
      }
      catch (\Exception $e) {
        // Fall through to common Backdrop entity key checks.
      }
    }

    if (isset($entity->nid) && (!$target_type || $target_type === 'node')) {
      return (int) $entity->nid;
    }
    if (isset($entity->tid)
      && (!$target_type
        || $target_type === 'taxonomy_term')
    ) {
      return (int) $entity->tid;
    }
    if (isset($entity->uid) && (!$target_type || $target_type === 'user')) {
      return (int) $entity->uid;
    }
    if (isset($entity->item_id)
      && (!$target_type
        || $target_type === 'paragraphs_item')
    ) {
      return (int) $entity->item_id;
    }

    return 0;
  }

}
