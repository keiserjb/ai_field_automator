<?php

namespace ai_field_automator\includes\plugins\reference;

/**
 * @file
 * Automator plugin for taxonomy classification using calibrated decision models.
 */
class AIFieldAutomatorTaxonomyClassification extends AIFieldAutomatorTaxonomyTerms {

  /**
   * {@inheritdoc}
   */
  public function helpText() {
    return t(
      'Classifies content into existing taxonomy terms using calibrated probabilistic decision models.'
    );
  }

  /**
   * {@inheritdoc}
   */
  public function allowedFieldTypes() {
    return ['taxonomy_term_reference', 'list_text'];
  }

  /**
   * {@inheritdoc}
   */
  public function generate($entity, $field_name, array $config) {
    $prompts = $this->buildPrompts($entity, $config);
    if (empty($prompts)) {
      return [];
    }

    $term_map = $this->getAllowedTerms($field_name);
    if (empty($term_map)) {
      $field = field_info_field($field_name);
      if (!empty($field['settings']['allowed_values']) && is_array($field['settings']['allowed_values'])) {
        $term_map = $field['settings']['allowed_values'];
      }
    }

    if (empty($term_map)) {
      return [];
    }

    $term_names = array_values($term_map);
    $model = !empty($config['model']) ? $config['model'] : NULL;
    $custom_prompt = !empty($config['taxonomy_classification_prompt'])
      ? $config['taxonomy_classification_prompt']
      : 'Which category best describes this content?';

    $values = [];
    foreach ($prompts as $prompt) {
      $question = [
        'id' => 'category_selection',
        'type' => 'choice',
        'prompt' => $custom_prompt,
        'options' => $term_names,
      ];

      $chosen = ai_decide($prompt, $question, $model);
      if ($chosen !== NULL && $chosen !== '') {
        $values[] = $chosen;
      }
    }

    return array_values(array_unique($values));
  }

}
