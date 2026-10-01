<?php


namespace ai_field_automator\includes\plugins\agent;

use ai_field_automator\includes\AIFieldAutomatorBase;

/**
 * @file
 * Automator plugin that delegates field generation to an ai_agent run.
 */
class AIFieldAutomatorAgentField extends AIFieldAutomatorBase {

  /**
   * {@inheritdoc}
   */
  public function helpText() {
    return t(
      'Delegates field generation to a configured AI agent (ReAct loop with tools). '
      . 'Select an agent and provide an optional task instruction.'
    );
  }

  /**
   * {@inheritdoc}
   */
  public function allowedFieldTypes() {
    return [
      'text',
      'text_long',
      'text_with_summary',
      'string',
      'string_long',
      'taxonomy_term_reference',
      'entityreference',
      'field_collection',
    ];
  }

  /**
   * {@inheritdoc}
   */
  public function generate($entity, $field_name, array $config) {
    $agent_id = $config['agent_id'] ?? '';
    if (!$agent_id || !module_exists('ai_agents') || !function_exists('ai_agents_run')) {
      return [];
    }

    $context = $this->buildEntityContext($entity, $config);
    $task    = $config['agent_task'] ?? "Generate a value for the field '{$field_name}'. Return text only.";
    $message = ($context !== '' ? $context . "\n\nTask: " : 'Task: ') . $task;

    $result = ai_agents_run($agent_id, $message);

    if (!empty($result['error']) || empty($result['answer'])) {
      watchdog('ai_field_automator', 'Agent run failed for field %field: %err', [
        '%field' => $field_name,
        '%err'   => $result['error'] ?? 'no answer',
      ], WATCHDOG_WARNING);
      return [];
    }

    if (!empty($result['status']) && $result['status'] === 'needs_approval') {
      return [];
    }

    return [trim($result['answer'])];
  }

  /**
   * Builds entity context string from the configured base field.
   */
  protected function buildEntityContext($entity, array $config) {
    $base = $config['base_field'] ?? '_title';
    if ($base === '_title') {
      $type = $config['entity_type'] ?? 'node';
      $label = method_exists($entity, 'label') ? $entity->label() : ($entity->title ?? ($entity->name ?? (function_exists('entity_label') ? entity_label($type, $entity) : '')));
      return 'Title: ' . $label;
    }
    if ($base === '_rendered_entity') {
      return $this->extractEntityContextText($entity, $config['entity_type'] ?? '');
    }
    $items = $entity->{$base}[LANGUAGE_NONE] ?? [];
    $parts  = [];
    foreach ($items as $item) {
      $text = $this->extractItemText($item);
      if ($text !== '') {
        $parts[] = $text;
      }
    }
    return implode("\n\n", $parts);
  }

}
